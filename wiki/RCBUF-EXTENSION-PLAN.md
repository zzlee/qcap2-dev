# rcbuf C++ Interface Redesign

## 1. Background

`qcap2_rcbuffer_t` is a reference-counted buffer wrapper used throughout the
pipeline. Currently it is a C struct holding `PVOID pData` + a free callback,
with consumers casting `pData` to known types (`qcap2_av_frame_t*`,
`qcap2_av_packet_t*`). This works but forces every metadata access through
type-specific getter/setter functions, and adding a new buffer type means
adding switch cases everywhere.

Moving to a C++ virtual interface eliminates the opaque pointer entirely —
each buffer type becomes a concrete subclass that implements the accessor
methods directly. No casts, no switch dispatch, no `buffer_type` enum.

## 2. Goals

- **No opaque pointers**: Buffer metadata is accessed through virtual methods,
  not by casting `PVOID pData`.
- **Pluggable backends**: Adding a new buffer type means writing one subclass,
  not patching switch statements across the codebase.
- **Zero-copy**: Backends wrap native types (`AVPacket*`, `AVFrame*`,
  `CUdeviceptr`, etc.) at construction time — no intermediate copy.
- **Reference counting**: Atomic `ref_count_` in the base class. Subclasses
  implement `on_release_resource()` to free their native resource.

## 3. Architecture

### 3.1. Base Class (`qcap2.buffer.h`)

The base class lives in its own header with **no 3rdparty dependencies**. It
declares only the general-purpose virtual functions that work across all buffer
types — packet metadata, frame properties, raw data access, and memory mapping.

```cpp
// include/qcap2.buffer.h

class qcap2_rcbuffer_t {
protected:
    std::atomic<int32_t> ref_count_{1};

    virtual ~qcap2_rcbuffer_t() = default;

    // Called once when ref_count_ reaches 0, before delete this.
    // Subclasses free their native resource here (av_packet_free, etc.).
    virtual void on_release_resource() = 0;

public:
    // ── Lifecycle (implemented once in base) ──
    void add_ref();
    void release();

    int32_t use_count() const;

    // ── Packet metadata (override per type) ──
    virtual QRESULT get_pts(int64_t* pts) = 0;
    virtual QRESULT set_pts(int64_t pts) = 0;
    virtual QRESULT get_dts(int64_t* dts) = 0;
    virtual QRESULT set_dts(int64_t dts) = 0;
    virtual QRESULT get_stream_index(int* idx) = 0;
    virtual QRESULT set_stream_index(int idx) = 0;
    virtual QRESULT is_keyframe(BOOL* key) = 0;
    virtual QRESULT set_keyframe(BOOL key) = 0;

    // ── Frame / raw data (override per type) ──
    virtual QRESULT get_data_ptr(uint8_t** data, int* size) = 0;
    virtual QRESULT get_video_property(
        ULONG* colorspace, ULONG* width, ULONG* height) = 0;
    virtual QRESULT get_plane(
        int plane, uint8_t** data, int* stride) = 0;

    // ── Mapping (optional, for hardware buffers) ──
    virtual QRESULT map_system_memory(PVOID* ppDataOut);
    virtual QRESULT unmap_system_memory();
};
```

Reference counting is centralized:

```cpp
void qcap2_rcbuffer_t::add_ref() {
    ref_count_.fetch_add(1, std::memory_order_relaxed);
}

void qcap2_rcbuffer_t::release() {
    if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        on_release_resource();
        delete this;
    }
}
```

### 3.2. Modular Header Layout

Each 3rdparty library gets its own header that includes the library directly.
The base class never sees them.

```
include/
 └── qcap2.buffer.h              ← base class only (no 3rdparty deps)
     qcap2.buffer.ffmpeg.h       ← AVPacket / AVFrame subclasses
     qcap2.buffer.cuda.h         ← CUDA buffer subclass
     qcap2.buffer.nvbuf.h        ← Jetson nvbuf subclass
     qcap2.buffer.v4l2.h         ← V4L2 buffer subclass
```

Each type-specific header provides the subclass and a downcast helper using
`dynamic_cast`:

```cpp
// include/qcap2.buffer.ffmpeg.h
#include <libavcodec/avcodec.h>
#include "qcap2.buffer.h"

class qcap2_avpacket_buffer : public qcap2_rcbuffer_t {
    AVPacket* pkt_;
    void on_release_resource() override { av_packet_free(&pkt_); }
public:
    qcap2_avpacket_buffer(AVPacket* pkt) : pkt_(pkt) {}
    AVPacket* native_handle() const { return pkt_; }

    QRESULT get_pts(int64_t* pts) override { ... }
    QRESULT set_pts(int64_t pts) override { ... }
    QRESULT get_stream_index(int* idx) override { ... }
    QRESULT is_keyframe(BOOL* key) override { ... }
    QRESULT get_data_ptr(uint8_t** data, int* size) override { ... }
};

// Downcast helper — safe, returns nullptr on type mismatch
inline qcap2_avpacket_buffer*
qcap2_buffer_to_avpacket(qcap2_rcbuffer_t* buf) {
    return dynamic_cast<qcap2_avpacket_buffer*>(buf);
}

class qcap2_avframe_buffer : public qcap2_rcbuffer_t {
    AVFrame* frame_;
    void on_release_resource() override { av_frame_free(&frame_); }
public:
    qcap2_avframe_buffer(AVFrame* frame) : frame_(frame) {}
    AVFrame* native_handle() const { return frame_; }

    QRESULT get_pts(int64_t* pts) override { ... }
    QRESULT get_video_property(...) override { ... }
    QRESULT get_plane(int plane, uint8_t** data, int* stride) override { ... }
};

inline qcap2_avframe_buffer*
qcap2_buffer_to_avframe(qcap2_rcbuffer_t* buf) {
    return dynamic_cast<qcap2_avframe_buffer*>(buf);
}
```

```cpp
// include/qcap2.buffer.cuda.h
#include <cuda.h>
#include "qcap2.buffer.h"

class qcap2_cuda_buffer : public qcap2_rcbuffer_t {
    CUdeviceptr devptr_;
    int width_, height_;
    void on_release_resource() override { /* cudaFree */ }
public:
    CUdeviceptr native_handle() const { return devptr_; }
    QRESULT get_video_property(...) override { /* CUDA-specific */ }
    QRESULT map_system_memory(PVOID* ppDataOut) override { ... }
};

inline qcap2_cuda_buffer*
qcap2_buffer_to_cuda(qcap2_rcbuffer_t* buf) {
    return dynamic_cast<qcap2_cuda_buffer*>(buf);
}
```

Consumers that need a specific backend's features include the corresponding
header and use the downcast helper:

```cpp
#include "qcap2.buffer.h"
#include "qcap2.buffer.ffmpeg.h"

void process(qcap2_rcbuffer_t* buf) {
    // General access — works with any backend
    int64_t pts;
    buf->get_pts(&pts);

    // Backend-specific access — only if it's an AVPacket buffer
    if (auto* pkt_buf = qcap2_buffer_to_avpacket(buf)) {
        AVPacket* raw = pkt_buf->native_handle();
        avcodec_send_packet(avctx, raw);
    }
}
```

This design keeps 3rdparty dependencies completely isolated — a pipeline that
only uses ffmpeg never needs to include CUDA headers, and vice versa.

### 3.3. Concrete Subclasses

Each buffer backend writes a subclass that implements the pure virtual methods.
The implementations live in backend-specific source files.

#### AVPacket (ffmpeg bitstream)

```cpp
// src/backends/buffer_avpacket.cpp
#include "qcap2.buffer.ffmpeg.h"

qcap2_avpacket_buffer::qcap2_avpacket_buffer(AVPacket* pkt)
    : pkt_(pkt) {}

void qcap2_avpacket_buffer::on_release_resource() {
    av_packet_free(&pkt_);
}

QRESULT qcap2_avpacket_buffer::get_pts(int64_t* pts) {
    if (!pts) return QCAP_RS_ERROR_INVALID_PARAMETER;
    *pts = pkt_->pts;
    return QCAP_RS_SUCCESSFUL;
}

QRESULT qcap2_avpacket_buffer::get_data_ptr(uint8_t** data, int* size) {
    *data = pkt_->data;
    *size = pkt_->size;
    return QCAP_RS_SUCCESSFUL;
}
// ... other overrides ...
```

#### AVFrame (ffmpeg decoded frames)

```cpp
// src/backends/buffer_avframe.cpp
#include "qcap2.buffer.ffmpeg.h"
// ... AVFrame override implementations ...
```

#### SYSTEM (generic system memory — for non-ffmpeg pipelines)

```cpp
// include/qcap2.buffer.h  (inline in the base header, no 3rdparty deps)
class qcap2_system_buffer : public qcap2_rcbuffer_t {
    uint8_t* buf_;
    int      size_;
    void*    user_data_;
    void (*free_cb_)(void*);
    void on_release_resource() override {
        if (free_cb_) free_cb_(user_data_);
    }
public:
    qcap2_system_buffer(uint8_t* buf, int size, void* cb_data, void (*cb)(void*))
        : buf_(buf), size_(size), user_data_(cb_data), free_cb_(cb) {}
    QRESULT get_data_ptr(uint8_t** data, int* size) override { ... }
};
```

#### CUDA (hardware memory)

```cpp
// src/backends/buffer_cuda.cpp
#include "qcap2.buffer.cuda.h"
// ... CUDA override implementations ...
```

### 3.4. Factory / Construction

Backends create the appropriate subclass directly. No central registry needed.

```cpp
// Inside the ffmpeg demuxer backend:
AVPacket* pkt = av_packet_alloc();
av_read_frame(ctx, pkt);

qcap2_rcbuffer_t* buf = new qcap2_avpacket_buffer(pkt);

// The buffer is ready to use — metadata is accessed through virtual methods.
```

For code that needs to create a simple system-memory buffer:

```cpp
qcap2_rcbuffer_t* buf = new qcap2_system_buffer(data, size, free_cb);
```

### 3.5. Pool Integration

A pool can pre-allocate buffers of a specific subclass:

```cpp
class qcap2_buffer_pool_t {
    std::vector<qcap2_rcbuffer_t*> pool_;
    std::function<qcap2_rcbuffer_t*()> factory_;

public:
    QRESULT get_buffer(qcap2_rcbuffer_t** out) {
        for (auto* buf : pool_) {
            if (buf->use_count() == 1) {  // only pool owns it
                buf->add_ref();
                *out = buf;
                return QCAP_RS_SUCCESSFUL;
            }
        }
        return QCAP_RS_ERROR_GENERAL;
    }
};
```

The pool is parameterized by factory function — it can produce
`qcap2_avpacket_buffer`, `qcap2_system_buffer`, or any other subclass.

## 4. Comparison: C Interface (type enum) vs C++ Interface

| Aspect | Type enum + switch | Virtual interface |
|---|---|---|
| Adding a new buffer type | Add enum entry + edit every switch | Write one subclass |
| Dispatch cost | Runtime switch (branch) | vtable dispatch (indirect call) |
| Type safety | `PVOID pData` cast at every call site | Compiler-verified per override |
| Data access | Cast `PVOID pData` to known type | Virtual methods on the buffer pointer |
| Code locality | Accessor logic spread across one big file | Each backend in its own file |
| Extensibility (third-party) | Must patch SDK enums and switches | Link a new subclass |
| C compatibility | Works from C code | C++ only |

The project is already C++ (the implementation uses `std::atomic`, `new`/
`delete`, lambdas). The public headers already have `extern "C"` guards for
C callers — but the rcbuf internals and pipeline code are all C++. Moving
the buffer type hierarchy to virtual methods is a natural fit.

## 5. What Gets Deleted

| Artifact | Replaced by |
|---|---|
| `struct qcap2_av_frame_t { padding[512] }` | `qcap2_avframe_buffer` class |
| `struct qcap2_av_packet_t { padding[128] }` | `qcap2_avpacket_buffer` class |
| `qcap2_av_frame_priv_t` overlay struct | (embedded in the class directly) |
| `qcap2_av_packet_priv_t` overlay struct | (embedded in the class directly) |
| All `qcap2_av_frame_*()` free functions | Virtual methods on `qcap2_rcbuffer_t` |
| All `qcap2_av_packet_*()` free functions | Virtual methods on `qcap2_rcbuffer_t` |
| `qcap2_rcbuffer_av_frame_owner_t` wrapper | (the object IS the buffer) |
| `qcap2_rcbuffer_av_packet_owner_t` wrapper | (the object IS the buffer) |
| `qcap2_rcbuffer_new_av_frame()` | `new qcap2_avframe_buffer(...)` |
| `qcap2_rcbuffer_new_av_packet()` | `new qcap2_avpacket_buffer(...)` |
| `qcap2_rcbuffer_priv_t` C struct | (members moved into base class) |
| `qcap2_buffer_type_t` enum | (type identity via `dynamic_cast` or a virtual `type_id()` if needed) |
| All switch-on-type dispatch | (polymorphism handles it) |

## 6. Pipeline User Code (unchanged)

The outermost API remains the same. Users still write:

```c
qcap2_rcbuffer_t* buf;
qcap2_pipeline_pop(my_pipeline, &buf);

int64_t pts;
buf->get_pts(&pts);                // virtual dispatch — no type checking needed

uint8_t* data;
int size;
buf->get_data_ptr(&data, &size);   // works for AVPACKET, SYSTEM

process(data, size, pts);
buf->release();                    // ref-counting from base class
```

The difference is internal: `buf` is now a polymorphic object instead of a C
struct with a tagged `PVOID`. The user doesn't need to know.

## 7. Migration Path

1. **Define the base class** in `include/qcap2.buffer.h` with the virtual
   interface and the centralized ref-counting implementation.

2. **Write concrete subclasses**: start with `qcap2_system_buffer` (replaces
   the legacy two-parameter `qcap2_rcbuffer_new` pattern), then
   `qcap2_avpacket_buffer` and `qcap2_avframe_buffer`.

3. **Port internal consumers**: change `qcap2.demuxer.cpp`, `qcap2.muxer.cpp`,
   `qcap2.processing.cpp` to construct the appropriate subclass and call
   virtual methods instead of `qcap2_av_packet_*` / `qcap2_av_frame_*`
   free functions.

4. **Remove the old types**: delete `qcap2_av_frame_t`, `qcap2_av_packet_t`,
   their overlay structs, their free functions, and their owning wrappers.

5. **Rework pools**: replace `qcap2_frame_pool_t` / `qcap2_packet_pool_t`
   with a generic `qcap2_buffer_pool_t` parameterized by a factory.

6. **Documentation**: update `wiki/RCBUF.md` and remove obsolete wiki pages.
