# rcbuf Extensibility Improvement Plan

## 1. Background & Motivation
Currently, [qcap2_rcbuffer_t](file:///home/zzlee/qcap2-dev/include/qcap2.types.h#L37) is a reference-counted buffer wrapper used throughout the pipeline. The implementation is based on a C struct storing a generic `PVOID pData` along with a free callback. Internal consumers cast `pData` to specific structs like [qcap2_av_frame_t](file:///home/zzlee/qcap2-dev/include/qcap2.types.h#L71) or [qcap2_av_packet_t](file:///home/zzlee/qcap2-dev/include/qcap2.types.h#L75). This requires type-specific free functions and switch statements, making it difficult to extend the pipeline with hardware-accelerated buffers (e.g., CUDA memory, dmabufs, Jetson nvbufs).

To improve extensibility, we propose refactoring [qcap2_rcbuffer_t](file:///home/zzlee/qcap2-dev/include/qcap2.types.h#L37) to use a C++ virtual interface internally while **exposing a pure C interface in the public headers**. This avoids exposing C++-specific constructs (like classes, virtual methods, or inheritance) to C callers, satisfying the requirement to maintain a clean C interface.

## 2. Goals
*   **Public C Interface**: The public header [qcap2.buffer.h](file:///home/zzlee/qcap2-dev/include/qcap2.buffer.h) must expose only C-compatible constructs (opaque pointers, enum tags, and C wrapper functions). No C++ virtual classes are to be exposed directly to users.
*   **Internal C++ Polymorphism**: Internally (in `src/`), the buffer is represented by a C++ virtual base class and concrete subclasses (`qcap2_system_buffer`, `qcap2_avpacket_buffer`, etc.) to eliminate switch statements and type casting.
*   **Zero-Copy Support**: Enable retrieving underlying native hardware handles (e.g. `CUdeviceptr`, `NvBufSurface*`) directly via the C/C++ interface.
*   **Pointer Identity Backward Compatibility**: Maintain the key memory-layout rule where [qcap2_rcbuffer_get_data](file:///home/zzlee/qcap2-dev/include/qcap2.buffer.h#L20) returns the exact borrowed identity pointer passed to [qcap2_rcbuffer_new](file:///home/zzlee/qcap2-dev/include/qcap2.buffer.h#L12) (supporting the `qcap2_container_of` pattern described in [RCBUF.md](file:///home/zzlee/qcap2-dev/wiki/RCBUF.md)).

## 3. Architecture

### 3.1. Public C Interface (`include/qcap2.buffer.h`)
The public header [qcap2.buffer.h](file:///home/zzlee/qcap2-dev/include/qcap2.buffer.h) remains a C-compatible header wrapped in `extern "C"`. It declares [qcap2_rcbuffer_t](file:///home/zzlee/qcap2-dev/include/qcap2.types.h#L37) as an opaque pointer:

```c
// include/qcap2.buffer.h
#include "qcap2.types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Opaque struct type (typedef in qcap2.types.h)
// typedef struct qcap2_rcbuffer_t qcap2_rcbuffer_t;

// --- Lifecycle APIs ---
qcap2_rcbuffer_t* qcap2_rcbuffer_new(PVOID pData, qcap2_on_free_resource_t pOnFreeResource);
void qcap2_rcbuffer_delete(qcap2_rcbuffer_t* pRCBuffer);
void qcap2_rcbuffer_add_ref(qcap2_rcbuffer_t* pRCBuffer);
void qcap2_rcbuffer_release(qcap2_rcbuffer_t* pRCBuffer);
PVOID qcap2_rcbuffer_lock_data(qcap2_rcbuffer_t* pRCBuffer);
void qcap2_rcbuffer_unlock_data(qcap2_rcbuffer_t* pRCBuffer);
PVOID qcap2_rcbuffer_get_data(qcap2_rcbuffer_t* pRCBuffer);
int32_t qcap2_rcbuffer_use_count(qcap2_rcbuffer_t* pRCBuffer);
int32_t qcap2_rcbuffer_res_count(qcap2_rcbuffer_t* pRCBuffer);

// --- Buffer Tagging & Native Handles ---
typedef enum {
    QCAP2_BUFFER_TYPE_SYSTEM = 0,
    QCAP2_BUFFER_TYPE_DMABUF,
    QCAP2_BUFFER_TYPE_V4L2,
    QCAP2_BUFFER_TYPE_CUDA,
    QCAP2_BUFFER_TYPE_NVBUF,
    QCAP2_BUFFER_TYPE_AVFRAME,
    QCAP2_BUFFER_TYPE_AVPACKET,
    QCAP2_BUFFER_TYPE_CUSTOM
} qcap2_buffer_type_t;

qcap2_buffer_type_t qcap2_rcbuffer_get_type(qcap2_rcbuffer_t* pRCBuffer);
PVOID qcap2_rcbuffer_get_native_handle(qcap2_rcbuffer_t* pRCBuffer);

// --- Extended Buffer Metadata & Accessors ---
QRESULT qcap2_rcbuffer_get_pts(qcap2_rcbuffer_t* pRCBuffer, int64_t* pts);
QRESULT qcap2_rcbuffer_set_pts(qcap2_rcbuffer_t* pRCBuffer, int64_t pts);
QRESULT qcap2_rcbuffer_get_dts(qcap2_rcbuffer_t* pRCBuffer, int64_t* dts);
QRESULT qcap2_rcbuffer_set_dts(qcap2_rcbuffer_t* pRCBuffer, int64_t dts);
QRESULT qcap2_rcbuffer_get_stream_index(qcap2_rcbuffer_t* pRCBuffer, int* idx);
QRESULT qcap2_rcbuffer_set_stream_index(qcap2_rcbuffer_t* pRCBuffer, int idx);
QRESULT qcap2_rcbuffer_is_keyframe(qcap2_rcbuffer_t* pRCBuffer, BOOL* key);
QRESULT qcap2_rcbuffer_set_keyframe(qcap2_rcbuffer_t* pRCBuffer, BOOL key);

// --- Frame/Raw Data Properties ---
QRESULT qcap2_rcbuffer_get_data_ptr(qcap2_rcbuffer_t* pRCBuffer, uint8_t** data, int* size);
QRESULT qcap2_rcbuffer_get_video_property(qcap2_rcbuffer_t* pRCBuffer, ULONG* colorspace, ULONG* width, ULONG* height);
QRESULT qcap2_rcbuffer_get_plane(qcap2_rcbuffer_t* pRCBuffer, int plane, uint8_t** data, int* stride);

// --- Hardware memory mapping ---
QRESULT qcap2_rcbuffer_map_system_memory(qcap2_rcbuffer_t* pRCBuffer, PVOID* ppDataOut);
QRESULT qcap2_rcbuffer_unmap_system_memory(qcap2_rcbuffer_t* pRCBuffer);

#ifdef __cplusplus
}
#endif
```

### 3.2. Private C++ Class Hierarchy (`src/qcap2.buffer_priv.h`)
The C++ base class is defined internally in a private header (invisible to public C users). In C++, the struct [qcap2_rcbuffer_t](file:///home/zzlee/qcap2-dev/include/qcap2.types.h#L37) is declared as a C++ base class:

```cpp
// src/qcap2.buffer_priv.h
#pragma once
#include "qcap2.buffer.h"
#include <atomic>

struct qcap2_rcbuffer_t {
protected:
    std::atomic<int32_t> use_count_{1};
    std::atomic<int32_t> res_count_{1};
    std::atomic<bool> resource_freed_{false};

    virtual ~qcap2_rcbuffer_t() = default;
    virtual void on_release_resource() = 0;

public:
    void add_ref();
    void release();
    int32_t use_count() const;
    int32_t res_count() const;
    PVOID lock_data();
    void unlock_data();

    // Virtual interfaces overridden by concrete backends
    virtual PVOID get_data() const = 0;
    virtual qcap2_buffer_type_t get_type() const = 0;
    virtual PVOID get_native_handle() const = 0;

    virtual QRESULT get_pts(int64_t* pts) = 0;
    virtual QRESULT set_pts(int64_t pts) = 0;
    virtual QRESULT get_dts(int64_t* dts) = 0;
    virtual QRESULT set_dts(int64_t dts) = 0;
    virtual QRESULT get_stream_index(int* idx) = 0;
    virtual QRESULT set_stream_index(int idx) = 0;
    virtual QRESULT is_keyframe(BOOL* key) = 0;
    virtual QRESULT set_keyframe(BOOL key) = 0;

    virtual QRESULT get_data_ptr(uint8_t** data, int* size) = 0;
    virtual QRESULT get_video_property(ULONG* colorspace, ULONG* width, ULONG* height) = 0;
    virtual QRESULT get_plane(int plane, uint8_t** data, int* stride) = 0;

    virtual QRESULT map_system_memory(PVOID* ppDataOut) { return QCAP_RS_ERROR_NOT_SUPPORTED; }
    virtual QRESULT unmap_system_memory() { return QCAP_RS_ERROR_NOT_SUPPORTED; }
};
```

Reference counting implementation in the base class:
```cpp
void qcap2_rcbuffer_t::add_ref() {
    use_count_.fetch_add(1, std::memory_order_relaxed);
}

void qcap2_rcbuffer_t::release() {
    if (use_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        // Decrease res_count; when both are 0, free resource and delete
        int32_t r = res_count_.fetch_sub(1, std::memory_order_acq_rel);
        if (r == 1) {
            bool expected = false;
            if (resource_freed_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
                on_release_resource();
            }
            delete this;
        }
    }
}
```

### 3.3. C-to-C++ Forwarding Layer (`src/qcap2.buffer.cpp`)
All public C APIs are defined in [qcap2.buffer.cpp](file:///home/zzlee/qcap2-dev/src/qcap2.buffer.cpp) and forward their operations to the C++ base class virtual methods via casting:

```cpp
// src/qcap2.buffer.cpp
#include "qcap2.buffer_priv.h"

extern "C" {

void qcap2_rcbuffer_add_ref(qcap2_rcbuffer_t* pRCBuffer) {
    if (pRCBuffer) pRCBuffer->add_ref();
}

void qcap2_rcbuffer_release(qcap2_rcbuffer_t* pRCBuffer) {
    if (pRCBuffer) pRCBuffer->release();
}

PVOID qcap2_rcbuffer_get_data(qcap2_rcbuffer_t* pRCBuffer) {
    return pRCBuffer ? pRCBuffer->get_data() : NULL;
}

qcap2_buffer_type_t qcap2_rcbuffer_get_type(qcap2_rcbuffer_t* pRCBuffer) {
    return pRCBuffer ? pRCBuffer->get_type() : QCAP2_BUFFER_TYPE_SYSTEM;
}

PVOID qcap2_rcbuffer_get_native_handle(qcap2_rcbuffer_t* pRCBuffer) {
    return pRCBuffer ? pRCBuffer->get_native_handle() : NULL;
}

QRESULT qcap2_rcbuffer_get_pts(qcap2_rcbuffer_t* pRCBuffer, int64_t* pts) {
    if (!pRCBuffer) return QCAP_RS_ERROR_INVALID_PARAMETER;
    return pRCBuffer->get_pts(pts);
}

// ... other forwarding functions ...
}
```

### 3.4. Private Modular Headers & Concrete Subclasses
Private subclasses exist in the `src/backends/` directory (or internally in `src/`) and are never exposed in public headers:

*   **System Memory Subclass (`qcap2_system_buffer`)**
    Wraps standard system buffers. Implements `get_data()` to return the original borrowed identity pointer to support `qcap2_container_of` patterns described in [RCBUF.md](file:///home/zzlee/qcap2-dev/wiki/RCBUF.md).
*   **FFmpeg AVPacket Subclass (`qcap2_avpacket_buffer`)**
    Wraps an `AVPacket*` and extracts packet metadata dynamically.
*   **FFmpeg AVFrame Subclass (`qcap2_avframe_buffer`)**
    Wraps an `AVFrame*`.
*   **CUDA Memory Subclass (`qcap2_cuda_buffer`)**
    Wraps a `CUdeviceptr` and overrides `map_system_memory` and `unmap_system_memory`.

Internally, downcasting is performed safely using helper inline functions:
```cpp
inline qcap2_avpacket_buffer* qcap2_buffer_to_avpacket(qcap2_rcbuffer_t* buf) {
    return (buf && buf->get_type() == QCAP2_BUFFER_TYPE_AVPACKET) 
        ? static_cast<qcap2_avpacket_buffer*>(buf) : nullptr;
}
```

---

## 4. Codebase Progress Verification

Checking the actual files in this repository reveals that **none of the migration steps have been started**. The previous draft of the plan stated that the demuxer, muxer, and portions of processing/utilities were already ported. This is **incorrect**:

*   [include/qcap2.buffer.h](file:///home/zzlee/qcap2-dev/include/qcap2.buffer.h): Still contains the legacy C functions and has no classes or new getters.
*   [qcap2.buffer.cpp](file:///home/zzlee/qcap2-dev/src/qcap2.buffer.cpp): Still defines the C struct `_qcap2_rcbuffer_priv_t` and `_qcap2_av_frame_priv_t` with no virtual methods or subclasses.
*   [qcap2.demuxer.cpp](file:///home/zzlee/qcap2-dev/src/qcap2.demuxer.cpp): Still creates buffers with `qcap2_rcbuffer_new()` and manages lifetime manually.
*   [qcap2.muxer.cpp](file:///home/zzlee/qcap2-dev/src/qcap2.muxer.cpp): Still uses `qcap2_rcbuffer_lock_data()` and expects legacy frame layout structures.
*   **Tests** in [tests/test_qcap2_buffer.cpp](file:///home/zzlee/qcap2-dev/tests/test_qcap2_buffer.cpp): Still compile and run successfully asserting the behavior of the old C structs.

### Progress Table

| Component | Status | Description |
|---|---|---|
| **Define Base Class & Subclasses** | ❌ Not Started | Core polymorphism architecture needs to be written. |
| **Port Demuxer & Muxer** | ❌ Not Started | Uses the legacy callbacks and struct-casting patterns. |
| **Port Processing** | ❌ Not Started | Encoders/decoders still cast `PVOID` to `qcap2_av_frame_t`. |
| **Port Utility Files** | ❌ Not Started | `sync.cpp`, `utils.cpp`, etc., still use old locking APIs. |
| **Remove Old Types** | ❌ Not Started | `qcap2_av_frame_t` is still declared in `qcap2.types.h`. |
| **Update Tests** | ❌ Not Started | Unit tests are based entirely on the legacy interface. |

---

## 5. Migration Path & Roadmap

To ensure continuous build verification and prevent regression, the migration must be phased:

### Phase 1: Core Framework (Private)
1. Add the internal base class `qcap2_rcbuffer_t` inside `src/qcap2.buffer_priv.h`.
2. Define `qcap2_system_buffer` subclass to emulate the legacy `qcap2_rcbuffer_new` behavior.
3. Rewrite [qcap2.buffer.cpp](file:///home/zzlee/qcap2-dev/src/qcap2.buffer.cpp) to forward the existing C API functions to the new virtual backend. This ensures all tests compile and pass before porting internal components.

### Phase 2: Extend public APIs
1. Update [qcap2.buffer.h](file:///home/zzlee/qcap2-dev/include/qcap2.buffer.h) to add `qcap2_rcbuffer_get_type`, `qcap2_rcbuffer_get_native_handle` and metadata getters (`qcap2_rcbuffer_get_pts`, etc.).
2. Implement subclasses `qcap2_avpacket_buffer` and `qcap2_avframe_buffer`.

### Phase 3: Port Internal Components
1. Port demuxer, muxer, and processing components to use the new C metadata getters instead of direct structure access.
2. Replace legacy frame/packet allocation calls with the creation of the respective subclasses.

### Phase 4: Cleanup & Rework Pools
1. Remove `qcap2_av_frame_t` and `qcap2_av_packet_t` from public headers.
2. Refactor buffer pools to use factory functions creating subclasses.
3. Update unit tests to verify the extended features (types and hardware handles).
