# `qcap2_frame_pool_t` — Pre-Allocated Frame Buffer Pool

## Purpose

`qcap2_frame_pool_t` is a **fixed-size, pre-allocated buffer pool** for video and audio frames. It eliminates per-frame `malloc`/`free` overhead in real-time pipelines by allocating all buffers up front on `start()` and recycling them via reference-counted ownership (`use_count == 1` means idle).

## API Reference

```c
// Lifecycle
qcap2_frame_pool_t* qcap2_frame_pool_new();
void                qcap2_frame_pool_delete(qcap2_frame_pool_t* pThis);
QRESULT             qcap2_frame_pool_start(qcap2_frame_pool_t* pThis);
QRESULT             qcap2_frame_pool_stop(qcap2_frame_pool_t* pThis);

// Configuration (call before start)
void qcap2_frame_pool_set_backend_type(qcap2_frame_pool_t* pThis, int nBackendType);
void qcap2_frame_pool_set_frame_count(qcap2_frame_pool_t* pThis, int nFrameCount);
void qcap2_frame_pool_set_video_frame_align(qcap2_frame_pool_t* pThis, int nFrameAlign, int nFrameVAlign);
void qcap2_frame_pool_set_audio_frame_align(qcap2_frame_pool_t* pThis, int nFrameAlign);
void qcap2_frame_pool_set_video_property(qcap2_frame_pool_t* pThis, ULONG nColorSpaceType, ULONG nFrameWidth, ULONG nFrameHeight);
void qcap2_frame_pool_set_video_property1(qcap2_frame_pool_t* pThis, ULONG nWidthBorder, ULONG nHeightBorder, BOOL bMapped);
void qcap2_frame_pool_set_audio_property(qcap2_frame_pool_t* pThis, ULONG nChannels, ULONG nSampleFmt, ULONG nSampleFrequency, ULONG nAudioFrameSize);

// Buffer acquisition
QRESULT qcap2_frame_pool_get_buffer(qcap2_frame_pool_t* pThis, qcap2_rcbuffer_t** ppBuffer);
```

## Architecture

```
┌──────────────────────────────────────────────┐
│              qcap2_frame_pool_t              │
│                                              │
│  ┌──────┐ ┌──────┐ ┌──────┐     ┌──────┐   │
│  │ buf0 │ │ buf1 │ │ buf2 │ ... │ bufN │   │
│  └──┬───┘ └──┬───┘ └──┬───┘     └──┬───┘   │
│     │        │        │            │        │
│  qcap2_rcbuffer_t  (use_count tracking)     │
│     │        │        │            │        │
│  qcap2_av_frame_t with pre-allocated buffer │
└──────────────────────────────────────────────┘
         │
         ▼  get_buffer() → returns first buf with use_count == 1
    ┌─────────┐
    │ Consumer │  calls qcap2_rcbuffer_release() when done
    └─────────┘     → use_count drops to 1 → buffer is idle again
```

### Recycling Semantics

The pool owns exactly one reference to each `qcap2_rcbuffer_t`. When `get_buffer()` is called:

1. It scans the pool for a buffer with `use_count == 1` (only the pool holds a ref → idle)
2. Calls `qcap2_rcbuffer_add_ref()` to bump it to 2 (pool + consumer)
3. Returns the buffer to the caller

When the consumer calls `qcap2_rcbuffer_release()`, the count drops back to 1, making the buffer idle and available for reuse. **No dynamic allocation occurs after `start()`.**

If all buffers are in use, `get_buffer()` returns `QCAP_RS_ERROR_GENERAL`.

## Lifecycle

```
new() → set_*() → start() → get_buffer()/release() ... → stop() → delete()
                     │                                        │
                     │   Can reconfigure and restart:         │
                     └──── stop() → set_*() → start() ───────┘
```

| State | `get_buffer()` |
|-------|---------------|
| Before `start()` | Returns `QCAP_RS_ERROR_GENERAL` |
| After `start()`, idle buffer available | Returns `QCAP_RS_SUCCESSFUL` |
| After `start()`, all in use | Returns `QCAP_RS_ERROR_GENERAL` |
| After `stop()` | Returns `QCAP_RS_ERROR_GENERAL` |

- **Double `start()`** is idempotent (returns `QCAP_RS_SUCCESSFUL`, no reallocation)
- **`stop()`** releases all pool buffers. Consumers holding references should release them separately.
- **`delete()`** calls `stop()` automatically

## Video Pool Usage

The pool mode is determined by the last `set_*_property()` call:
- `set_video_property()` → video mode
- `set_audio_property()` → audio mode

### Basic Example

```c
qcap2_frame_pool_t* pool = qcap2_frame_pool_new();

qcap2_frame_pool_set_frame_count(pool, 4);
qcap2_frame_pool_set_video_property(pool, QCAP_COLORSPACE_TYPE_BGR24, 1920, 1080);
qcap2_frame_pool_set_video_frame_align(pool, 16, 1);

qcap2_frame_pool_start(pool);

// In hot path — zero allocation
qcap2_rcbuffer_t* buf = NULL;
if (qcap2_frame_pool_get_buffer(pool, &buf) == QCAP_RS_SUCCESSFUL) {
    PVOID data = qcap2_rcbuffer_lock_data(buf);
    qcap2_av_frame_t* frame = (qcap2_av_frame_t*)data;

    // Write pixels into frame buffer...
    uint8_t* ptrs[4];
    int strides[4];
    qcap2_av_frame_get_buffer1(frame, ptrs, strides);
    // ... fill ptrs[0] ...

    qcap2_rcbuffer_unlock_data(buf);

    // Pass to downstream (e.g., scaler, encoder)
    qcap2_video_scaler_push(scaler, buf);
    qcap2_rcbuffer_release(buf); // consumer done, buf returns to pool
}

qcap2_frame_pool_stop(pool);
qcap2_frame_pool_delete(pool);
```

### Border Support

Use `set_video_property1()` to add pixel borders around each frame:

```c
qcap2_frame_pool_set_video_property(pool, QCAP_COLORSPACE_TYPE_BGR24, 640, 480);
qcap2_frame_pool_set_video_property1(pool, 8, 8, FALSE);
// Actual allocated size: (640 + 8*2) × (480 + 8*2) = 656 × 496
```

This is useful for algorithms that need boundary padding (e.g., filtering, convolution).

## Audio Pool Usage

```c
qcap2_frame_pool_t* pool = qcap2_frame_pool_new();

qcap2_frame_pool_set_frame_count(pool, 4);
// Stereo, S16 format, 48kHz, 1024 samples per frame
qcap2_frame_pool_set_audio_property(pool, 2, AV_SAMPLE_FMT_S16, 48000, 1024);

qcap2_frame_pool_start(pool);

qcap2_rcbuffer_t* buf = NULL;
qcap2_frame_pool_get_buffer(pool, &buf);

// Buffer size = channels × bytes_per_sample × frame_size
// = 2 × 2 × 1024 = 4096 bytes
```

Audio buffer size is computed as: `channels × av_get_bytes_per_sample(sample_fmt) × frame_size`.

## Backend Types

| Value | Enum | Description |
|-------|------|-------------|
| 0 | `QCAP2_FRAME_POOL_BACKEND_TYPE_UNKNOWN` | Not set |
| 1 | `QCAP2_FRAME_POOL_BACKEND_TYPE_DEFAULT` | CPU `malloc` allocation (default) |
| 2 | `QCAP2_FRAME_POOL_BACKEND_TYPE_RKMPP` | Rockchip MPP DMA buffers (platform-specific) |
| 3 | `QCAP2_FRAME_POOL_BACKEND_TYPE_QDMABUF` | QCAP DMA buffer allocator (platform-specific) |

Currently, only `QCAP2_FRAME_POOL_BACKEND_TYPE_DEFAULT` is implemented. Other backends are reserved for hardware-accelerated platforms.

## Thread Safety

All public APIs are mutex-protected. `get_buffer()` is safe to call concurrently from multiple threads. However, the returned `qcap2_rcbuffer_t*` itself follows normal `rcbuffer` thread-safety rules (lock/unlock for data access, atomic ref counting).

## Integration with Video Scaler

The frame pool complements `qcap2_video_scaler_t`'s `set_buffers()` API. While the scaler's built-in buffer pool requires manual setup of individual `rcbuffer` instances, the frame pool automates allocation:

```c
// Instead of manually creating N rcbuffers for the scaler:
qcap2_frame_pool_t* pool = qcap2_frame_pool_new();
qcap2_frame_pool_set_frame_count(pool, 4);
qcap2_frame_pool_set_video_property(pool, QCAP_COLORSPACE_TYPE_BGR24, 1920, 1080);
qcap2_frame_pool_start(pool);

// Use pool for input frame staging
qcap2_rcbuffer_t* input_buf = NULL;
qcap2_frame_pool_get_buffer(pool, &input_buf);
// ... fill frame ...
qcap2_video_scaler_push(scaler, input_buf);
qcap2_rcbuffer_release(input_buf);
```

## Configuration Defaults

| Property | Default |
|----------|---------|
| `frame_count` | 4 |
| `backend_type` | `QCAP2_FRAME_POOL_BACKEND_TYPE_DEFAULT` |
| `video_align` | 16 |
| `video_valign` | 1 |
| `video_width_border` | 0 |
| `video_height_border` | 0 |
| `video_mapped` | `FALSE` |
| `audio_align` | 1 |

## Memory Cleanup

On `stop()`, all pool buffers are released. The frame pool's `on_free_resource` callback handles both video and audio frames:
- **Video frames**: Uses `qcap2_av_frame_free_buffer()` to free the owned allocation
- **Audio frames**: Manually `free()`s the audio buffer (since it was set via `set_buffer()`, not owned by the frame)

Both cases then `delete` the `qcap2_av_frame_t` struct itself.
