# rcbuf Extensibility Improvement Plan

## 1. Background
Currently, the `qcap2_rcbuffer_t` design wraps a generic `PVOID pData` and expects it to primarily act as a system memory pointer (e.g., embedded within a `qcap2_av_frame_t`). However, modern multimedia pipelines and hardware-accelerated components (encoders, decoders, filters) heavily rely on non-system buffer structures, such as V4L2 buffers, dmabufs, CUDA memory, Jetson nvbufs, and FFmpeg's `AVFrame`. To leverage zero-copy capabilities, `rcbuf` needs to natively support these types without forcing the existing media codec pipelines to change their recycling and queueing logic.

## 2. Goals
*   **Extensibility**: Allow `rcbuf` to encapsulate arbitrary third-party buffer structures securely.
*   **Zero-Copy Compatibility**: Enable hardware pipelines (e.g., CUDA encoder) to retrieve their native handles directly from the `rcbuf`.
*   **Pipeline Agnosticism**: Maintain complete backward compatibility with existing recycling mechanisms (`res_count`, `use_count`, queues).
*   **Graceful Fallback**: Support potential mapping from hardware buffers to system memory for components that only support software processing.

## 3. Proposed Architecture

### 3.1. Introduce `qcap2_buffer_type_t`
Create an enumeration to identify the underlying buffer format securely. This acts as a tag for consumers to query before processing.

```c
typedef enum {
    QCAP2_BUFFER_TYPE_SYSTEM = 0,
    QCAP2_BUFFER_TYPE_DMABUF,
    QCAP2_BUFFER_TYPE_V4L2,
    QCAP2_BUFFER_TYPE_CUDA,
    QCAP2_BUFFER_TYPE_NVBUF,
    QCAP2_BUFFER_TYPE_AVFRAME,
    QCAP2_BUFFER_TYPE_CUSTOM
} qcap2_buffer_type_t;
```

### 3.2. Extend `qcap2_rcbuffer_t` Internals
Modify the `qcap2_rcbuffer_priv_t` structure to store the buffer type and a native handle. The original `pData` will remain intact (often pointing to a high-level struct like `qcap2_av_frame_t`), while the new fields hold the hardware-specific details.

```cpp
typedef struct _qcap2_rcbuffer_priv_t {
    PVOID pData;
    ULONG nDataSize;
    qcap2_on_free_resource_t pOnFreeResource;

    std::atomic<int32_t> use_count;
    std::atomic<int32_t> res_count;
    std::atomic<bool> resource_freed;

    // --- New Extensibility Fields ---
    qcap2_buffer_type_t buffer_type;
    PVOID pNativeHandle; // e.g., CUdeviceptr, struct v4l2_buffer*, AVFrame*
    qcap2_on_free_resource_t pOnFreeNativeHandle;
} qcap2_rcbuffer_priv_t;
```

### 3.3. New Extensible APIs
Introduce an extended constructor and getters so that new pipeline stages can assign and retrieve the specialized handles.

```c
// Extended constructor for third-party buffer structures
qcap2_rcbuffer_t* qcap2_rcbuffer_new_ext(
    PVOID pData,
    qcap2_on_free_resource_t pOnFreeResource,
    qcap2_buffer_type_t buffer_type,
    PVOID pNativeHandle,
    qcap2_on_free_resource_t pOnFreeNativeHandle
);

// Metadata getters
qcap2_buffer_type_t qcap2_rcbuffer_get_type(qcap2_rcbuffer_t* pRCBuffer);
PVOID qcap2_rcbuffer_get_native_handle(qcap2_rcbuffer_t* pRCBuffer);
```

*Note: The existing `qcap2_rcbuffer_new()` will simply call `qcap2_rcbuffer_new_ext()` with `QCAP2_BUFFER_TYPE_SYSTEM` and `NULL` for the native handle.*

### 3.4. Hardware-to-System Memory Mapping (Optional Phase 2)
To handle cases where a hardware buffer enters a software component, provide hooks to temporarily map the native buffer into system memory.

```c
QRESULT qcap2_rcbuffer_map_system_memory(qcap2_rcbuffer_t* pRCBuffer, PVOID* ppDataOut);
QRESULT qcap2_rcbuffer_unmap_system_memory(qcap2_rcbuffer_t* pRCBuffer);
```
If a CPU resampler receives a CUDA buffer, it maps it, processes it, and unmaps it—while the buffer remains a CUDA type.

## 4. Impact on Existing Pipeline
*   **Recycling**: Unaffected. The queue mechanisms push and pop `qcap2_rcbuffer_t*`. Ownership transfers and `res_count` drops happen identically.
*   **Legacy Components**: Since legacy components don't check `qcap2_rcbuffer_get_type()`, they will assume the buffer is system memory. If a pipeline mixes legacy CPU components with hardware buffers, the developer must insert a mapping filter or use the proposed `qcap2_rcbuffer_map_system_memory` automatically.
*   **Hardware Components**: A CUDA encoder receiving the buffer will check `if (qcap2_rcbuffer_get_type(rcbuf) == QCAP2_BUFFER_TYPE_CUDA)`, cast `get_native_handle()` to a `CUdeviceptr`, and avoid copying entirely.

## 5. Actionable Steps
1.  **Header Modifications**: Add `qcap2_buffer_type_t` and new public APIs to `include/qcap2.buffer.h`.
2.  **Implementation**: Update `src/qcap2.buffer.cpp` to store and manage the new metadata fields.
3.  **Unit Tests**: Add tests in `tests/test_qcap2_buffer.cpp` asserting that the new extended constructor correctly retains types and native handles without affecting reference counting.
4.  **Documentation Update**: Refresh `wiki/RCBUF.md` to explain the new typing and native handle features for hardware acceleration.
