# qcap2_video_decoder_t usage notes

`qcap2_video_decoder_t` is a C-compatible video decoding component that transforms compressed bitstream packets into raw video frames. It is backed by FFmpeg's `libavcodec`, supporting decoding of H.264 (`h264`), H.265/HEVC (`hevc`), and MPEG-2 (`mpeg2video`) formats.

---

## Architectural & Design Principles

### 1. FFmpeg Codec Backend

The decoder wraps `AVCodecContext` from FFmpeg's `libavcodec` to perform the actual decompression. On `start()`, it initializes the codec context based on the configured `qcap2_video_encoder_property_t`, selecting the appropriate codec, passing initialization parameters (extra data), and configuring threading options.

#### Codec Selection Strategy
1. **By name**: Maps `QCAP_ENCODER_FORMAT_H264` → `"h264"`, `QCAP_ENCODER_FORMAT_H265` → `"hevc"`, `QCAP_ENCODER_FORMAT_MPEG2` → `"mpeg2video"`.
2. **Fallback by ID**: If the named decoder is unavailable, falls back to `avcodec_find_decoder()` with the corresponding `AVCodecID`.

#### In-stream Resolution and Format Detection
Decoders naturally discover the resolution and pixel format from the incoming bitstream. However, standard QCAP pipelines often require the decoded frames to be output in a specific target colorspace or scaled resolution. 

#### Target Format and Resolution Negotiation
The decoder allows clients to request a specific output colorspace (e.g. `QCAP_COLORSPACE_TYPE_I420`, `BGR24`) and scaling dimensions by setting the video properties before starting:
- If target width/height are set to `0`, the decoder outputs frames at the native stream resolution.
- If a target colorspace is set, but the native stream format differs, the decoder lazily initializes and caches a `SwsContext` to automatically perform scale and format conversion.
- For planar formats (e.g. `QCAP_COLORSPACE_TYPE_YV12` / `QCAP_COLORSPACE_TYPE_YV24`), the decoder handles planar U/V plane swaps automatically (matching the same behavior in `qcap2_video_scaler_t`).

---

### 2. Output Buffer Recycling

To minimize latency and heap allocations, the decoder supports recycling output buffers registered via `qcap2_video_decoder_set_buffers()`.
- **Recycle Mechanism**: When the decoder needs to produce a decoded frame, it iterates over the registered buffers. If any buffer has a reference count (`use_count`) of exactly `1` (indicating it is not currently held by downstream modules), it is selected and reused.
- **Fallback**: If no pre-allocated buffer is available or registered, the decoder dynamically allocates a new `qcap2_av_frame_t` and wraps it in a reference-counted `qcap2_rcbuffer_t` with a custom deletion callback.

---

### 3. Multi-Threaded Decoding

Multi-threaded decoding can be requested via `qcap2_video_decoder_set_multithread()`. When enabled, the decoder configures `libavcodec` with:
- `thread_count = 0` (auto-select thread count based on hardware cores)
- `thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE` (enabling both frame-level and slice-level parallel decoding)

---

### 4. Push/Pop Data Flow and Hybrid Recycling Models

The video decoder employs two lifecycle queues to achieve zero-allocation and lock-free/low-latency recycling:

1. **Input Queue (Push-Pop-Release / HPR)**:
   - The user calls `qcap2_video_decoder_push()` to submit a compressed packet for decoding.
   - The decoder processes it synchronously and then automatically pushes it to an internal `input_recycled_queue`.
   - The user retrieves the empty/processed input packet via `qcap2_video_decoder_pop_input()` to reuse it for the next push.
2. **Output Queue (Pop-Push-Release / PPR)**:
   - The user calls `qcap2_video_decoder_pop()` to retrieve a raw decoded frame.
   - Once the user is done processing the frame, they return the empty frame buffer to the decoder via `qcap2_video_decoder_push_output()`.
   - Finally, they release the user-level reference via `qcap2_rcbuffer_release()`.

```
Push (compressed packet in)          Pop (decoded raw frame out)
┌─────────────────┐                  ┌──────────────────┐
│qcap2_av_packet_t│                  │ qcap2_av_frame_t │
│ (H.264/H.265    │──► Decoder ─────►│  (I420/NV12/RGB) │
│ bitstream data) │    (libavcodec)  │  via rcbuffer    │
│ via rcbuffer    │                  │                  │
└─────────────────┘                  └──────────────────┘
   ▲                                     │
   │ (pop_input)                         │ (push_output)
   └─────────────── [Recycle] ◄──────────┘
```

---

## Public API Function Semantics

### `qcap2_video_decoder_new()`
Creates a new video decoder instance with default configuration. Returns an opaque pointer.

### `qcap2_video_decoder_delete(qcap2_video_decoder_t* pThis)`
Stops the decoder (if running), releases all owned resources (codec context, property copies, extra data, queued frames, registered buffers, and internal queues), and frees the instance.

### `qcap2_video_decoder_set_video_property(pThis, pVideoEncoderProperty)`
Copies the property structure defining the input codec format and the desired target colorspace/resolution. Must be called before `start()`.

### `qcap2_video_decoder_get_video_property(pThis, pVideoEncoderProperty)`
Retrieves the currently configured video property.

### `qcap2_video_decoder_get_extra_data(pThis, ppExtraData, pExtraDataSize)`
Retrieves a pointer to the codec extra data (SPS/PPS) currently set on the decoder.

### `qcap2_video_decoder_set_extra_data(pThis, pExtraData, nExtraDataSize)`
Manually sets the codec extra data (SPS/PPS/VPS) required by certain decoders to initialize. The data is copied internally.

### `qcap2_video_decoder_set_frame_count(pThis, nFrameCount)`
Sets the internal frame count hint.

### `qcap2_video_decoder_set_frame_align(pThis, nFrameAlign)`
Sets the memory alignment for decoder-internal frame buffers (default: 16).

### `qcap2_video_decoder_set_frame_valign(pThis, nFrameVAlign)`
Sets the vertical alignment for decoder-internal frame buffers (default: 1).

### `qcap2_video_decoder_set_packet_count(pThis, nPacketCount)`
Sets the packet count configuration hint.

### `qcap2_video_decoder_set_max_packet_size(pThis, nMaxPacketSize)`
Sets the maximum expected packet size hint.

### `qcap2_video_decoder_set_multithread(pThis, bMultiThread)`
Enables multi-threaded decoding. When enabled, FFmpeg auto-selects the thread count and enables frame/slice-based concurrency.

### `qcap2_video_decoder_set_event(pThis, pEvent)`
Registers a `qcap2_event_t` to be notified when decoded frames are available.

### `qcap2_video_decoder_set_buffers(pThis, pBuffers)`
Registers a list of pre-allocated `qcap2_rcbuffer_t` instances for output frame recycling.

### `qcap2_video_decoder_set_payload_type(pThis, nPayloadType)`
Sets the network RTP payload type associated with this stream.

### `qcap2_video_decoder_start(pThis)`
Initializes the FFmpeg codec context and opens the decoder. Fails if no property has been set or if the codec cannot be initialized. Idempotent if already running.

### `qcap2_video_decoder_stop(pThis)`
Flushes the decoder (sends a null packet to decode any remaining buffered frames), drains the output queue, stops and drains the input and output recycled queues to prevent stale buffer reuse on restart, and tears down the codec context. Idempotent if already stopped.

### `qcap2_video_decoder_push(pThis, pRCBuffer)`
Pushes a compressed packet to the decoder:
1. Locks the input `qcap2_rcbuffer_t` and extracts packet data pointers, size, PTS, and DTS.
2. Sends the packet to the decoder context via `avcodec_send_packet()`.
3. In a loop, retrieves all decoded frames using `avcodec_receive_frame()`.
4. Lazily initializes format/resolution conversion if the stream properties differ from the target properties.
5. Obtains an output frame buffer (either by recycling from registered buffers, popping from `output_recycled_queue`, or allocating a new one).
6. Copies/converts the pixels, swapping U/V planes if a planar format like YV12/YV24 is targeted.
7. Wraps each output raw frame in a `qcap2_rcbuffer_t` and pushes it to the output queue.
8. Signals waiting consumers and triggers the registered event notification.
9. Enqueues the consumed packet `pRCBuffer` to the internal `input_recycled_queue` for HPR recycling.

### `qcap2_video_decoder_pop(pThis, ppRCBuffer)`
Blocks until a decoded frame is available or the decoder is stopped. Returns the oldest queued `qcap2_rcbuffer_t` containing a `qcap2_av_frame_t`.

### `qcap2_video_decoder_pop_input(pThis, ppRCBuffer)`
Dequeues the oldest processed compressed packet from the `input_recycled_queue` (HPR model). Non-blocking if a recycled packet is available.

### `qcap2_video_decoder_push_output(pThis, pRCBuffer)`
Enqueues an empty/used decoded raw frame back to the `output_recycled_queue` (PPR model) so that the decoder can reuse it on future decompresses.
