# `qcap2_packet_pool_t` — Pre-Allocated Packet Buffer Pool

## Purpose

`qcap2_packet_pool_t` is a **fixed-size, pre-allocated buffer pool** for AV packets. It eliminates per-packet `malloc`/`free` overhead in real-time pipelines by allocating packet objects up front on `start()` and recycling them via reference-counted ownership (`use_count == 1` means idle).

Unlike `qcap2_frame_pool_t` which allocates buffers of a fixed size based on video/audio properties, the packet pool supports dynamic resizing of underlying packet buffers if a requested packet size exceeds the currently allocated buffer size of a recycled packet.

## API Reference

```c
// Lifecycle
qcap2_packet_pool_t* qcap2_packet_pool_new();
void                 qcap2_packet_pool_delete(qcap2_packet_pool_t* pThis);
QRESULT              qcap2_packet_pool_start(qcap2_packet_pool_t* pThis);
QRESULT              qcap2_packet_pool_stop(qcap2_packet_pool_t* pThis);

// Configuration (call before start)
void qcap2_packet_pool_set_packet_count(qcap2_packet_pool_t* pThis, int nPacketCount);

// Buffer acquisition
QRESULT qcap2_packet_pool_get_buffer(qcap2_packet_pool_t* pThis, int nPacketSize, qcap2_rcbuffer_t** ppBuffer);
```

## Architecture

```
┌──────────────────────────────────────────────┐
│             qcap2_packet_pool_t              │
│                                              │
│  ┌──────┐ ┌──────┐ ┌──────┐     ┌──────┐   │
│  │ buf0 │ │ buf1 │ │ buf2 │ ... │ bufN │   │
│  └──┬───┘ └──┬───┘ └──┬───┘     └──┬───┘   │
│     │        │        │            │        │
│  qcap2_rcbuffer_t  (use_count tracking)     │
│     │        │        │            │        │
│  qcap2_av_packet_t with resizable buffer    │
└──────────────────────────────────────────────┘
         │
         ▼  get_buffer() → returns first buf with use_count == 1
    ┌─────────┐
    │ Consumer │  calls qcap2_rcbuffer_release() when done
    └─────────┘     → use_count drops to 1 → buffer is idle again
```

### Recycling and Resizing Semantics

The pool owns exactly one reference to each `qcap2_rcbuffer_t`. When `get_buffer(pool, size, &buf)` is called:

1. It scans the pool for a buffer with `use_count == 1` (only the pool holds a ref → idle).
2. It checks the allocated buffer size of the `qcap2_av_packet_t`. If the allocated size is smaller than the requested `nPacketSize`, it dynamically resizes the buffer using `qcap2_av_packet_alloc_buffer()`.
3. Calls `qcap2_rcbuffer_add_ref()` to bump it to 2 (pool + consumer).
4. Returns the buffer to the caller.

When the consumer calls `qcap2_rcbuffer_release()`, the count drops back to 1, making the packet idle and available for reuse.

If all packets are in use, `get_buffer()` returns `QCAP_RS_ERROR_GENERAL`.

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
| After `start()`, idle packet available | Returns `QCAP_RS_SUCCESSFUL` |
| After `start()`, all in use | Returns `QCAP_RS_ERROR_GENERAL` |
| After `stop()` | Returns `QCAP_RS_ERROR_GENERAL` |

- **Double `start()`** is idempotent (returns `QCAP_RS_SUCCESSFUL`, no reallocation of the packet wrapper).
- **`stop()`** releases all pool packets. Consumers holding references should release them separately.
- **`delete()`** calls `stop()` automatically.

## Usage Example

```c
qcap2_packet_pool_t* pool = qcap2_packet_pool_new();

// Request 10 packets in the pool
qcap2_packet_pool_set_packet_count(pool, 10);
qcap2_packet_pool_start(pool);

// In hot path — grab a packet capable of holding at least 1500 bytes
qcap2_rcbuffer_t* buf = NULL;
if (qcap2_packet_pool_get_buffer(pool, 1500, &buf) == QCAP_RS_SUCCESSFUL) {
    PVOID data = qcap2_rcbuffer_lock_data(buf);
    qcap2_av_packet_t* pkt = (qcap2_av_packet_t*)data;

    // Fill packet data...
    uint8_t* pBuffer = NULL;
    int nSize = 0;
    qcap2_av_packet_get_buffer(pkt, &pBuffer, &nSize);

    // pBuffer is valid and nSize >= 1500
    // ... write to pBuffer ...

    qcap2_rcbuffer_unlock_data(buf);

    // Pass to downstream
    qcap2_some_pipeline_push(pipeline, buf);

    // Consumer done, pkt returns to pool
    qcap2_rcbuffer_release(buf);
}

qcap2_packet_pool_stop(pool);
qcap2_packet_pool_delete(pool);
```

## Configuration Defaults

| Property | Default |
|----------|---------|
| `packet_count` | 4 |

## Thread Safety

All public APIs are mutex-protected. `get_buffer()` is safe to call concurrently from multiple threads. However, the returned `qcap2_rcbuffer_t*` itself follows normal `rcbuffer` thread-safety rules (lock/unlock for data access, atomic ref counting).
