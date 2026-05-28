# qcap2_video_encoder_t usage notes

`qcap2_video_encoder_t` is a C-compatible video encoding component that transforms raw video frames into compressed bitstream packets. It is backed by FFmpeg's `libavcodec`, supporting H.264 (`libx264`), H.265 (`libx265`), and MPEG-2 (`mpeg2video`) software encoders.

---

## Architectural & Design Principles

### 1. FFmpeg Codec Backend

The encoder wraps `AVCodecContext` from FFmpeg's `libavcodec` to perform the actual compression. On `start()`, it initializes the codec context based on the configured `qcap2_video_encoder_property_t`, selecting the appropriate codec, pixel format, rate control, and profile settings.

#### Codec Selection Strategy
1. **By name**: Maps `QCAP_ENCODER_FORMAT_H264` → `"libx264"`, `QCAP_ENCODER_FORMAT_H265` → `"libx265"`, `QCAP_ENCODER_FORMAT_MPEG2` → `"mpeg2video"`.
2. **Fallback by ID**: If the named codec is unavailable, falls back to `avcodec_find_encoder()` with the corresponding `AVCodecID`.

#### Pixel Format Negotiation
The encoder automatically negotiates the optimal pixel format between the source colorspace and the codec's supported formats:
- If the source format (e.g. `AV_PIX_FMT_YUV420P` for `QCAP_COLORSPACE_TYPE_I420`) is directly supported by the codec, it is used as-is.
- Otherwise, `AV_PIX_FMT_YUV420P` is preferred if the codec supports it.
- As a final fallback, the codec's first supported format is used.

#### Automatic Pixel Format Conversion
When the input frame's pixel format differs from the encoder's target format (e.g. pushing `BGR24` frames to an H.264 encoder), an internal `SwsContext` is lazily initialized to perform automatic conversion. This context is cached and reused across frames, and is re-created only when input dimensions or colorspace change.

---

### 2. Rate Control Modes

The encoder maps `QCAP_RECORD_MODE_*` values to FFmpeg rate control:

| QCAP Mode | FFmpeg Mapping |
|-----------|---------------|
| `QCAP_RECORD_MODE_CBR` | Sets `bit_rate`, `rc_max_rate`, `rc_min_rate` equally; configures `rc_buffer_size` |
| `QCAP_RECORD_MODE_VBR` | Sets target `bit_rate` only |
| `QCAP_RECORD_MODE_CQP` | Sets `global_quality` and enables `AV_CODEC_FLAG_QSCALE` |

Bitrate values in `qcap2_video_encoder_property_t` are specified in **kbps** and scaled to bps (×1000) for FFmpeg.

---

### 3. Low-Latency Encoding

For H.264 and H.265, the encoder is configured with `preset=ultrafast` and `tune=zerolatency` by default. This ensures:
- **Immediate packet output**: Each pushed frame produces a corresponding encoded packet without buffering delay.
- **No B-frames** (when `nBFrames=0`): Eliminates reordering latency.
- **Minimal CPU usage**: The ultrafast preset prioritizes speed over compression efficiency.

---

### 4. Extra Data (SPS/PPS/VPS)

After `start()`, the encoder extracts codec-specific extra data (e.g. H.264 SPS/PPS, H.265 VPS/SPS/PPS) from `AVCodecContext::extradata`. This data is available via `qcap2_video_encoder_get_extra_data()` and is required by downstream consumers (muxers, RTP packetizers) to initialize decoders.

Extra data can also be manually set via `qcap2_video_encoder_set_extra_data()` for scenarios where external initialization parameters are needed.

---

### 5. IDR Request

`qcap2_video_encoder_request_idr()` is a lock-free atomic operation that sets an internal flag. On the next `push()`, if the flag is set, the encoder forces the frame to be encoded as an I-frame (`AV_PICTURE_TYPE_I` with `AV_FRAME_FLAG_KEY`). This is useful for:
- Forcing keyframes for seek points
- Responding to client requests in streaming scenarios
- Ensuring clean entry points after error recovery

---

### 6. Dynamic Property Updates

While the encoder is running, `qcap2_video_encoder_set_dynamic_video_property()` allows runtime modification of:
- **Bitrate** (`nBitRate`): Applied immediately to `AVCodecContext::bit_rate`
- **GOP size** (`nGOP`): Applied immediately to `AVCodecContext::gop_size`
- **Record mode** and **quality**: Stored for reference

Note: Not all codec implementations honor mid-stream parameter changes. The effectiveness depends on the underlying FFmpeg codec.

---

### 7. Push/Pop Data Flow

```
Push (raw frame in)                    Pop (encoded packet out)
┌─────────────────┐                    ┌──────────────────┐
│ qcap2_av_frame_t│                    │qcap2_av_packet_t │
│ (I420/NV12/BGR) │                    │ (H.264/H.265     │
│ via rcbuffer     │──► Encoder ──────►│  compressed data)│
│                 │    (libx264)       │ via rcbuffer      │
└─────────────────┘                    └──────────────────┘
```

- **Input**: Raw video frames wrapped in `qcap2_rcbuffer_t` containing `qcap2_av_frame_t`
- **Output**: Compressed packets wrapped in `qcap2_rcbuffer_t` containing `qcap2_av_packet_t`
- **Packet ownership**: Each output `qcap2_rcbuffer_t` owns its packet data buffer via a custom `on_free_resource` callback using `qcap2_container_of()`

---

## Public API Function Semantics

### `qcap2_video_encoder_new()`
Creates a new video encoder instance with default configuration. Returns an opaque pointer.

### `qcap2_video_encoder_delete(qcap2_video_encoder_t* pThis)`
Stops the encoder (if running), releases all owned resources (codec context, property copies, extra data, queued packets), and frees the instance.

### `qcap2_video_encoder_set_video_property(pThis, pVideoEncoderProperty)`
Copies all encoder properties from the source into the encoder's internal storage. Must be called before `start()`. Configures codec format, resolution, frame rate, rate control, profile, B-frames, aspect ratio, etc.

### `qcap2_video_encoder_get_video_property(pThis, pVideoEncoderProperty)`
Retrieves the currently configured encoder properties.

### `qcap2_video_encoder_set_dynamic_video_property(pThis, pDynamicProperty)`
Sets dynamic properties that can be changed at runtime. If the encoder is running, bitrate and GOP changes are applied immediately to the codec context.

### `qcap2_video_encoder_get_dynamic_video_property(pThis, pDynamicProperty)`
Retrieves the current dynamic property values.

### `qcap2_video_encoder_get_extra_data(pThis, ppExtraData, pExtraDataSize)`
Returns a pointer to the codec extra data (SPS/PPS). Valid after `start()`. The pointer remains valid until `stop()` or `delete()`.

### `qcap2_video_encoder_set_extra_data(pThis, pExtraData, nExtraDataSize)`
Manually sets codec extra data. The data is copied internally.

### `qcap2_video_encoder_set_frame_count(pThis, nFrameCount)`
Sets the internal frame count configuration hint.

### `qcap2_video_encoder_set_frame_align(pThis, nFrameAlign)`
Sets the memory alignment for encoder-internal frame buffers (default: 16).

### `qcap2_video_encoder_set_frame_valign(pThis, nFrameVAlign)`
Sets the vertical alignment for encoder-internal frame buffers.

### `qcap2_video_encoder_set_packet_count(pThis, nPacketCount)`
Sets the packet count configuration hint.

### `qcap2_video_encoder_set_max_packet_size(pThis, nMaxPacketSize)`
Sets the maximum encoded packet size hint.

### `qcap2_video_encoder_set_multithread(pThis, bMultiThread)`
Enables multi-threaded encoding. When enabled, FFmpeg auto-selects the thread count.

### `qcap2_video_encoder_set_event(pThis, pEvent)`
Registers a `qcap2_event_t` to be notified when encoded packets are available.

### `qcap2_video_encoder_set_num_cores(pThis, nNumCores)`
Explicitly sets the number of encoding threads. Overrides the `multithread` setting.

### `qcap2_video_encoder_set_native_buffer(pThis, bNativeBuffer)`
Enables native buffer mode (reserved for hardware-accelerated backends).

### `qcap2_video_encoder_request_idr(pThis)`
Requests that the next pushed frame be encoded as an IDR/keyframe. Lock-free (uses atomic flag).

### `qcap2_video_encoder_start(pThis)`
Initializes the FFmpeg codec context and begins accepting frames. Fails if no video property has been set or if the codec cannot be initialized. Idempotent if already running.

### `qcap2_video_encoder_stop(pThis)`
Flushes the encoder (sends a null frame to produce any remaining packets), then tears down the codec context and drains the output queue. Idempotent if already stopped.

### `qcap2_video_encoder_push(pThis, pRCBuffer)`
Encodes a raw video frame:
1. Locks the input `qcap2_rcbuffer_t` and extracts video properties and plane pointers
2. Initializes/reinitializes the pixel format converter if input format changed
3. Allocates an `AVFrame`, copies/converts pixel data into it
4. Handles YV12/YV24 U/V plane swapping (same as `qcap2_video_scaler_t`)
5. Applies IDR request flag if pending
6. Calls `avcodec_send_frame()` followed by `avcodec_receive_packet()` in a drain loop
7. Wraps each output `AVPacket` into a `qcap2_av_packet_t` with key-frame flag, PTS, DTS
8. Enqueues encoded packets and notifies waiting consumers

### `qcap2_video_encoder_pop(pThis, ppRCBuffer)`
Blocks until an encoded packet is available or the encoder is stopped. Returns the oldest queued `qcap2_rcbuffer_t` containing a `qcap2_av_packet_t`.
