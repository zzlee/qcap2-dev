# qcap2_video_scaler_t usage notes

`qcap2_video_scaler_t` is a C-compatible utility for scaling, cropping, format-converting, and filtering raw video frames. It is backed by FFmpeg's `libswscale` (high-performance direct scaling/conversion/cropping) and `libavfilter` (complex arbitrary filter-graph parsing and execution).

---

## Architectural & Design Principles

### 1. Dual-Backend Processing Layout
The scaler operates dynamically in two mutually-exclusive execution modes depending on whether a filter graph is specified:

#### A. Direct Scaling Mode (SwsContext)
When no custom filter graph is configured, the scaler routes frames through an optimized `SwsContext`:
- **Cropping Computations**: If crop coordinates `(crop_x, crop_y, crop_w, crop_h)` are set, the scaler calculates offset shift variables to adjust input plane pointers `src_ptrs` before passing them to `sws_scale()`:
  - *Packed Formats (e.g. RGB24, BGR24, ARGB32)*: Shifts plane pointer `0` by `crop_y * stride[0] + crop_x * bytes_per_pixel`.
  - *Planar YUV 4:2:0 Formats (e.g. I420, YV12)*: Shifts plane `0` by `crop_y * stride[0] + crop_x` and chroma planes `1` and `2` by `(crop_y / 2) * stride[i] + (crop_x / 2)`.
  - *Interleaved Chroma Formats (e.g. NV12, P010)*: Shifts plane `0` by `crop_y * stride[0] + crop_x * bpp` and chroma plane `1` by `(crop_y / 2) * stride[1] + crop_x * 2`.
- **Planar U/V Plane Swapping**: Legacy QCAP2 formats `QCAP_COLORSPACE_TYPE_YV12` and `QCAP_COLORSPACE_TYPE_YV24` represent planar YUV where U and V planes are swapped compared to standard FFmpeg `AV_PIX_FMT_YUV420P` and `AV_PIX_FMT_YUV444P`. To maintain clean compatibility, the scaler maps them to standard YUV enums but automatically swaps plane pointers `1` and `2` during both input ingestion and output rendering.

#### B. Filter Graph Mode (AVFilterGraph)
When `qcap2_video_scaler_set_filter_graph()` is called with a non-empty configuration string:
- **Dynamic Filter Graph**: Instantiates a custom `AVFilterGraph` composed of a `buffer` source filter (accepting input frames at dynamic dimensions) and a `buffersink` sink filter (constraining formats to the target configuration).
- **String Parsing**: Parses and connects arbitrary FFmpeg filters (e.g., `"transpose=1,scale=160:120"`) using `avfilter_graph_parse_ptr()`.
- **Dynamic Resolution Propagation**: If the filter graph changes frame dimensions (e.g. via downscaling or rotating), the scaler dynamically updates the properties of the popped destination `qcap2_av_frame_t` to match the actual output of `av_buffersink_get_frame()`.

---

### 2. Zero-Allocation Buffer Recycling (HPR & PPR)
For high-performance video pipelines, the scaler supports dual-queue buffer recycling (HPR and PPR models) alongside a pre-allocated buffer pool to avoid heap allocations:

- **HPR (Host-Pipeline-Recycle) for Input Buffers**: When the user pushes a source frame via `qcap2_video_scaler_push()`, the scaler processes the frame and automatically enqueues the consumed input buffer into an internal `input_recycled_queue`. The user calls `qcap2_video_scaler_pop_input()` to retrieve it, refills it with new raw video data, and pushes it again.
- **PPR (Pipeline-Push-Recycle) for Output Buffers**: When the user retrieves a scaled output frame via `qcap2_video_scaler_pop()`, they consume the video payload and then return the empty buffer shell back to the scaler's `output_recycled_queue` using `qcap2_video_scaler_push_output()`.
- **Output Buffer Selection Priority**:
  When scaling a new frame, the scaler decides which output buffer to write to using the following priority order:
  1. **PPR Queue**: Pops an idle buffer shell from the `output_recycled_queue`.
  2. **Buffer Pool**: Scans registered pool buffers (set via `qcap2_video_scaler_set_buffers()`) and reuses any buffer whose `qcap2_rcbuffer_use_count(buf) == 1` (meaning it is currently idle and not owned by another pipeline stage).
  3. **Dynamic Fallback**: If no recycled or pool buffers are available, a new output frame is allocated dynamically and wrapped in a reference-counted buffer with a custom deleter.

---

## Public API Function Semantics

### `qcap2_video_scaler_new()`
Creates a new video scaler instance, initializing synchronization mutexes and condition variables.

### `qcap2_video_scaler_delete(qcap2_video_scaler_t* pThis)`
Stops the scaler, releases all pre-allocated buffer references, empties/deletes queued output frames, and reclaims allocated FFmpeg contexts.

### `qcap2_video_scaler_set_video_format(qcap2_video_scaler_t* pThis, qcap2_video_format_t* pVideoFormat)`
Configures target output video properties (width, height, output pixel colorspace, and interlacing).

### `qcap2_video_scaler_get_video_format(qcap2_video_scaler_t* pThis, qcap2_video_format_t* pVideoFormat)`
Retrieves the currently configured target output video properties.

### `qcap2_video_scaler_set_crop(qcap2_video_scaler_t* pThis, int x, int y, int w, int h)`
Sets a cropping box on the input video frame. Cropping is automatically bypassed if `w <= 0` or `h <= 0`.

### `qcap2_video_scaler_set_buffers(qcap2_video_scaler_t* pThis, qcap2_rcbuffer_t** pBuffers)`
Registers a null-terminated array of pre-allocated `qcap2_rcbuffer_t*` output buffers.

### `qcap2_video_scaler_set_filter_graph(qcap2_video_scaler_t* pThis, const char* strFilterGraph)`
Sets a custom FFmpeg filter graph string.

### `qcap2_video_scaler_start(qcap2_video_scaler_t* pThis)`
Activates the scaler to begin accepting pushed frames.

### `qcap2_video_scaler_stop(qcap2_video_scaler_t* pThis)`
Deactivates the scaler, flushing all pending queued frames and internal context allocations.

### `qcap2_video_scaler_push(qcap2_video_scaler_t* pThis, qcap2_rcbuffer_t* pRCBuffer)`
Pushes a raw source frame to be scaled, cropped, converted, or filtered.
- Automatically initializes or re-allocates FFmpeg structures (`SwsContext` or `AVFilterGraph`) if source dimensions or pixel layouts change dynamically.
- Notifies popped readers via the synchronization condition variable and triggers the registered event handle.

### `qcap2_video_scaler_pop(qcap2_video_scaler_t* pThis, qcap2_rcbuffer_t** ppRCBuffer)`
Pops a processed video frame from the output queue. Blocks the calling thread if empty until a frame is pushed or the scaler is stopped.

### `qcap2_video_scaler_pop_input(qcap2_video_scaler_t* pThis, qcap2_rcbuffer_t** ppRCBuffer)`
Pops a consumed input buffer from the scaler's internal `input_recycled_queue`. The user can reuse this buffer to hold subsequent raw input video frames.

### `qcap2_video_scaler_push_output(qcap2_video_scaler_t* pThis, qcap2_rcbuffer_t* pRCBuffer)`
Pushes a consumed output buffer back to the scaler's `output_recycled_queue`. The scaler will reuse this buffer shell for future scaled video frames.
