# qcap2_rcbuffer_queue_t usage notes

`qcap2_rcbuffer_queue_t` is a thread-safe FIFO queue for `qcap2_rcbuffer_t*` handles. It is intended for producer/consumer handoff between threads, optionally paired with a `qcap2_event_t` so consumers can wait for queue activity without polling.

## Intended use pattern

From `usecases/1-rcbuf-queue.cpp`:

```cpp
qcap2_rcbuffer_queue_t* pRCBufferQ = qcap2_rcbuffer_queue_new();
qcap2_rcbuffer_queue_start(pRCBufferQ);

qcap2_event_t* pRCBufferQEvent = ...;
qcap2_rcbuffer_queue_set_event(pRCBufferQ, pRCBufferQEvent);
```

Producer thread:

```cpp
while (true) {
	qcap2_rcbuffer_t* pRCBuffer = ...; // acquired from device/object/user code
	qcap2_rcbuffer_queue_push(pRCBufferQ, pRCBuffer);
}
```

Consumer thread:

```cpp
while (true) {
	qcap2_event_wait(pRCBufferQEvent);

	qcap2_rcbuffer_t* pRCBuffer = NULL;
	qcap2_rcbuffer_queue_pop(pRCBufferQ, &pRCBuffer);
	// consume pRCBuffer, then release it according to ownership rules
}
```

## Queue lifecycle

### `qcap2_rcbuffer_queue_new()`

Creates an empty stopped queue.

- The queue initially has no attached event.
- `maxBuffers == 0` means unbounded.
- Call `qcap2_rcbuffer_queue_start()` before normal producer/consumer use.

### `qcap2_rcbuffer_queue_start(qcap2_rcbuffer_queue_t* pThis)`

Starts the queue.

- Pushers may enqueue buffers.
- Poppers may block until a buffer arrives.

### `qcap2_rcbuffer_queue_stop(qcap2_rcbuffer_queue_t* pThis)`

Stops the queue and wakes waiters.

- Blocking `push()`/`pop()` calls are released.
- Attached `qcap2_event_t` is notified so external `qcap2_event_wait()` callers can wake and notice shutdown.
- Stopping does not necessarily destroy queued items; deletion drains remaining queued rc-buffers.

### `qcap2_rcbuffer_queue_delete(qcap2_rcbuffer_queue_t* pThis)`

Destroys the queue.

- Marks the queue stopped.
- Wakes waiters.
- Drains any still-queued `qcap2_rcbuffer_t*` entries and calls `qcap2_rcbuffer_release()` on them.
- Do not use the queue after deletion.

## Event integration

### `qcap2_rcbuffer_queue_set_event(qcap2_rcbuffer_queue_t* pThis, qcap2_event_t* pEvent)`

Attaches an external event object to the queue.

The queue notifies the event when:

- a push changes the queue from empty to non-empty;
- a pop removes one item but more items remain queued;
- the queue is stopped/deleted, to wake external waiters.

This supports the common pattern:

```cpp
qcap2_event_wait(event);
qcap2_rcbuffer_queue_pop(queue, &buffer);
```

Because events may be auto-reset or eventfd-like, consumers should treat the event as a wakeup hint, not as the queue item itself. After waking, call `qcap2_rcbuffer_queue_pop()` to retrieve the actual rc-buffer.

## Push/pop semantics

### `qcap2_rcbuffer_queue_push(qcap2_rcbuffer_queue_t* pThis, qcap2_rcbuffer_t* pRCBuffer)`

Enqueues an rc-buffer handle.

- Fails for null queue or null buffer.
- Blocks while the queue is full if `maxBuffers > 0`.
- Fails if the queue is stopped while waiting.
- Notifies internal pop waiters.
- Notifies the attached event when the queue becomes non-empty.

Ownership note:

- The queue stores the pointer passed to it.
- If the producer needs to keep using the same rc-buffer handle independently after pushing, it should call `qcap2_rcbuffer_add_ref()` before or during handoff according to the intended ownership model.
- The consumer is responsible for releasing the popped buffer when done.

### `qcap2_rcbuffer_queue_pop(qcap2_rcbuffer_queue_t* pThis, qcap2_rcbuffer_t** ppRCBuffer)`

Dequeues the oldest rc-buffer handle.

- Sets `*ppRCBuffer` to `NULL` before attempting to pop.
- Blocks while the queue is empty and running.
- Fails if the queue is stopped and empty.
- Wakes one blocked pusher after removing an item.
- If more items remain, re-notifies the attached event so event-driven consumers can continue draining.

## Capacity

### `qcap2_rcbuffer_queue_set_max_buffers(qcap2_rcbuffer_queue_t* pThis, int nMaxBuffers)`

Sets maximum queue depth.

- `nMaxBuffers <= 0` means unbounded.
- When bounded and full, `push()` blocks until a consumer pops or the queue is stopped.

### `qcap2_rcbuffer_queue_is_full()` / `qcap2_rcbuffer_queue_is_empty()` / `qcap2_rcbuffer_queue_get_buffer_count()`

Thread-safe state inspection helpers.

These values are snapshots only. In concurrent code, they can become stale immediately after return.

## `qcap2_rcbuffer_queue_set_buffers()`

Seeds the queue from a null-terminated array of `qcap2_rcbuffer_t*`:

```cpp
qcap2_rcbuffer_t* buffers[] = { buf0, buf1, buf2, NULL };
qcap2_rcbuffer_queue_set_buffers(queue, buffers);
```

- Stops inserting at the first null pointer.
- Respects `maxBuffers` if configured.
- Notifies pop waiters and the attached event if the queue becomes non-empty.

`set_buffers()` is only a queue-seeding helper. It does not install recycle behavior and does not change `qcap2_rcbuffer_release()` semantics.

## Correct producer/consumer ownership

A queue is a transport for rc-buffer handles. It does not alter the data pointer inside `qcap2_rcbuffer_t`; the memory-layout constraints described in `RCBUF.md` still apply.

Typical handoff:

1. Producer obtains or creates a `qcap2_rcbuffer_t*`.
2. Producer pushes it to the queue.
3. Consumer pops it.
4. Consumer processes it.
5. Consumer calls `qcap2_rcbuffer_release()` when done.

If multiple queues/consumers need the same rc-buffer, add references explicitly before sharing.

## Common pitfalls

- Do not assume `qcap2_event_wait()` returns the buffer; it only wakes the consumer.
- Do not forget to call `qcap2_rcbuffer_queue_start()` before normal use.
- Do not delete the queue while producer/consumer threads may still call into it.
- Do not rely on `is_empty()`/`get_buffer_count()` for synchronization decisions without locking at a higher level.
- Do not push the queue pointer by mistake; push the rc-buffer pointer:

```cpp
// Correct
qcap2_rcbuffer_queue_push(pRCBufferQ, pRCBuffer);

// Incorrect
qcap2_rcbuffer_queue_push(pRCBufferQ, (qcap2_rcbuffer_t*)pRCBufferQ);
```
