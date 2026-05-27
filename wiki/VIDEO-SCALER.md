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

### 2. Zero-Allocation Reference-Counted Buffer Pool
For high-performance pipelines, the scaler supports recycling pre-allocated buffer pools registered via `qcap2_video_scaler_set_buffers()`:

- **Ownership Handoff Rule**: When the producer registers pool buffers:
  1. The scaler increments their reference count via `qcap2_rcbuffer_add_ref()`.
  2. The producer should immediately release its initial local references (`qcap2_rcbuffer_release()`). This hands over unique ownership to the scaler, bringing their idle use counts to exactly **1**.
- **Idle Frame Recognition**: When a new frame is pushed, the scaler scans its registered buffer list. Any buffer with a `qcap2_rcbuffer_use_count(buf) == 1` is considered idle (as no other components are holding references). The scaler increments its count to **2**, scales pixel data directly into it, and pushes it to the output queue.
- **Dynamic Fallback**: If no pre-allocated pool is provided or all pool buffers are currently in use by the consumer, the scaler automatically allocates a new frame dynamically, wrapping it in a `qcap2_rcbuffer_t` with a custom `on_free_resource` callback to prevent leaks.

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
