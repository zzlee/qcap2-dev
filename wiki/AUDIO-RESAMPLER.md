# qcap2_audio_resampler_t usage notes

`qcap2_audio_resampler_t` is a C-compatible utility for converting raw audio streams between different formats, sample rates, and channel layouts. It is backed by FFmpeg's `libswresample` library.

---

## Architectural & Design Principles

### 1. Thread Safety and Queue Synchronization
The resampler integrates a thread-safe synchronized output queue using standard C++ synchronization primitives (`std::mutex` and `std::condition_variable`):
- **Concurrent Processing**: Multiple threads can safely invoke `qcap2_audio_resampler_push()` and `qcap2_audio_resampler_pop()` concurrently.
- **Blocking Pop**: `qcap2_audio_resampler_pop()` blocks the caller thread using a condition variable until either:
  1. A new resampled frame is pushed and available in the queue.
  2. The resampler is stopped/deleted, at which point the wait terminates and returns an error code (`QCAP_RS_ERROR_GENERAL`).

### 2. Modern FFmpeg Integration
The resampler is designed to be compatible with modern FFmpeg 6.x releases:
- **Layout API**: Utilizes the modern `swr_alloc_set_opts2()` context configuration and `AVChannelLayout` channel layouts (`av_channel_layout_default` and `av_channel_layout_uninit`) rather than deprecated legacy int64 layout bitmasks.
- **Dynamic Reconfiguration**: The resampler automatically detects format changes in incoming raw frames dynamically. When the input sample rate, format, or channel count changes, the resampler teardown and reinitializes the underlying `SwrContext` seamlessly without requiring the consumer to restart the component.

### 3. Lifetime and No-Leak Memory Layout
To prevent memory leaks and coordinate clean deallocations across reference-counted boundary changes, output frames utilize `qcap2_rcbuffer_t` with an embedded member pattern:

```cpp
struct ResamplerOutputFrame {
    qcap2_av_frame_t frame;
    uint8_t* audio_buffer;
    size_t buffer_size;
    qcap2_rcbuffer_t* rc_buffer;
};
```

1. **Embedded Layout**: The resampled PCM buffer and the `qcap2_av_frame_t` structure are wrapped in a single allocated `ResamplerOutputFrame` block.
2. **Identity Preservation**: The reference-counted buffer is created using the address of the embedded `frame` member:
   ```cpp
   out_frame->rc_buffer = qcap2_rcbuffer_new(&out_frame->frame, [](PVOID pData) {
       ResamplerOutputFrame* p = qcap2_container_of(pData, ResamplerOutputFrame, frame);
       delete[] p->audio_buffer;
       delete p;
   });
   ```
3. **No-Leak Callback**: When the consumer releases the output buffer (`qcap2_rcbuffer_release()`), the custom callback uses `qcap2_container_of()` to obtain the base pointer of the `ResamplerOutputFrame` and safely deallocates both the dynamic audio buffer and the container struct.

---

## Public API Function Semantics

### `qcap2_audio_resampler_new()`
Creates and initializes a new resampler instance.
- Initializes mutexes, condition variables, and starts in the stopped/idle state.
- Default output target properties: Stereo (2 channels), S16 sample format, 44100Hz sample rate.

### `qcap2_audio_resampler_delete(qcap2_audio_resampler_t* pThis)`
Safely tears down the resampler.
- Automatically stops execution and flushes/releases any queued output frames.
- Reclaims all allocated synchronization structures and internal memories.

### `qcap2_audio_resampler_set_audio_property(qcap2_audio_resampler_t* pThis, ULONG nChannels, ULONG nSampleFmt, ULONG nSampleFrequency, ULONG nFrameSize)`
Configures target output audio properties:
- `nChannels`: Output channels (e.g., `1` for mono, `2` for stereo).
- `nSampleFmt`: Output sample format mapped to FFmpeg `AVSampleFormat` (e.g., `1` for `AV_SAMPLE_FMT_S16`, `3` for `AV_SAMPLE_FMT_FLT`).
- `nSampleFrequency`: Output sample rate (e.g., `48000`).
- `nFrameSize`: Target output frame size in samples (0 for variable frame sizes).

### `qcap2_audio_resampler_set_frame_count(qcap2_audio_resampler_t* pThis, int nFrameCount)`
Sets the maximum capacity of output frames.

### `qcap2_audio_resampler_set_multithread(qcap2_audio_resampler_t* pThis, bool bMultiThread)`
Enables/disables multi-threading processing inside the resampler.

### `qcap2_audio_resampler_set_event(qcap2_audio_resampler_t* pThis, qcap2_event_t* pEvent)`
Sets a synchronization event to notify when new resampled audio frames are pushed into the queue.
- The event (`qcap2_event_notify`) is triggered whenever a push completes.

### `qcap2_audio_resampler_start(qcap2_audio_resampler_t* pThis)`
Starts the resampler, enabling it to accept pushed frames.

### `qcap2_audio_resampler_stop(qcap2_audio_resampler_t* pThis)`
Stops the resampler.
- Closes the stream, prevents further frames from being pushed.
- Empties the output queue, releasing all pending output buffers.
- Frees internal FFmpeg `SwrContext` buffers.

### `qcap2_audio_resampler_push(qcap2_audio_resampler_t* pThis, qcap2_rcbuffer_t* pRCBuffer)`
Pushes a new reference-counted input audio frame.
- **Retrieves properties**: Reads the channel count, sample format, sample rate, and frame size from the input `qcap2_av_frame_t`.
- **Dynamic Setup**: Auto-reconfigures `libswresample` if the input format differs from the cached configuration.
- **Conversion**: Converts raw data and allocates new output buffers.
- **Enqueuing**: Wraps the converted frame inside an `rc_buffer` and notifies waiting readers via the condition variable and the registered event.

### `qcap2_audio_resampler_pop(qcap2_audio_resampler_t* pThis, qcap2_rcbuffer_t** ppRCBuffer)`
Pops a resampled frame from the output queue.
- Blocks if the queue is empty, until a frame is pushed or the resampler is stopped.
- Returns `QCAP_RS_SUCCESSFUL` on success, or an error if stopped.
