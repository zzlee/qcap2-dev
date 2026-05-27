# QCAP2 API Wiki

This directory contains implementation notes and usage constraints for QCAP2 APIs. Read relevant pages before refactoring or adding features.

## Buffer APIs

- [RCBUF.md](./RCBUF.md) — `qcap2_rcbuffer_t` lifetime, data pointer identity, embedded-member memory-layout rules, and `qcap2_container_of()` usage.
- [RCBUF-QUEUE.md](./RCBUF-QUEUE.md) — `qcap2_rcbuffer_queue_t` producer/consumer usage, event integration, queue lifecycle, and ownership handoff rules.
- [AV-FRAME-BUFFER.md](./AV-FRAME-BUFFER.md) — `qcap2_av_frame_alloc_buffer()` raw video allocation rules based on `QCAP_COLORSPACE_TYPE_XXX` layouts.

## Synchronization APIs

- [SYNC-EVENT-HANDLERS.md](./SYNC-EVENT-HANDLERS.md) — `qcap2_event_handlers_t` thread-based poll implementation, concurrency model, and invoke semantics.
