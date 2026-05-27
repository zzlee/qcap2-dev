# qcap2_rcbuffer_t usage notes

`qcap2_rcbuffer_t` is a reference-counted wrapper around a caller-provided resource pointer. The pointer passed to `qcap2_rcbuffer_new()` is not required to point to the beginning of an allocated object. It may point to a member embedded inside a larger owner object.

## Key memory-layout rule

The use case in `usecases/case0-rcbuf.cpp` stores an embedded `qcap2_av_frame_t` inside a larger object:

```cpp
struct MyVideoFrame {
	int index;
	void* buffers[4];
	qcap2_av_frame_t av_frame;
};
```

The rc-buffer is created with the address of the embedded member:

```cpp
qcap2_rcbuffer_t* pRCBuffer =
	qcap2_rcbuffer_new(&pVideoFrame->av_frame, MyVideoFrame::_on_free_resource);
```

Therefore:

```cpp
qcap2_rcbuffer_get_data(pRCBuffer) == &pVideoFrame->av_frame
```

must remain true. `qcap2_rcbuffer_t` must preserve the exact pointer identity passed to `qcap2_rcbuffer_new()`.

This is important because the free callback may recover the owning object through `qcap2_container_of()`:

```cpp
static void _on_free_resource(PVOID pData) {
	MyVideoFrame* pThis = qcap2_container_of(pData, MyVideoFrame, av_frame);
	pThis->on_free_resource();
}
```

`qcap2_container_of(ptr, type, member)` subtracts `offsetof(type, member)` from `ptr`. It only works if `ptr` is exactly the address of `member` inside the original `type` object. Do not replace `pData` with copied data, allocated wrapper data, or a normalized base pointer.

## Function semantics

### `qcap2_rcbuffer_new(PVOID pData, qcap2_on_free_resource_t pOnFreeResource)`

Creates an rc-buffer around the caller-supplied resource pointer.

- `pData` is a borrowed identity pointer.
- The rc-buffer does not own the enclosing object automatically.
- `pOnFreeResource`, if non-null, is called when the rc-buffer resource lifetime ends.
- The callback receives the same `pData` pointer originally passed to `qcap2_rcbuffer_new()`.

### `qcap2_rcbuffer_get_data(qcap2_rcbuffer_t* pRCBuffer)`

Risky raw accessor.

- Returns the original `pData` pointer.
- Does not increment any reference/resource count.
- Use only when the caller already knows the rc-buffer remains alive.
- Required for embedded-member patterns such as retrieving `qcap2_av_frame_t*` from a `MyVideoFrame` owner.

Example:

```cpp
qcap2_av_frame_t* pAVFrame =
	(qcap2_av_frame_t*)qcap2_rcbuffer_get_data(pRCBuffer);
assert(pAVFrame == &pVideoFrame->av_frame);
```

### `qcap2_rcbuffer_lock_data(qcap2_rcbuffer_t* pRCBuffer)` / `qcap2_rcbuffer_unlock_data(qcap2_rcbuffer_t* pRCBuffer)`

Use these when accessing data across code that should pin the resource lifetime.

- `lock_data()` returns the original `pData` and increments the resource pin count.
- `unlock_data()` releases that resource pin.
- Every successful `lock_data()` must be paired with exactly one `unlock_data()`.
- The free callback must not run while a lock is still held.

### `qcap2_rcbuffer_add_ref(qcap2_rcbuffer_t* pRCBuffer)` / `qcap2_rcbuffer_release(qcap2_rcbuffer_t* pRCBuffer)`

Manage ownership of the rc-buffer handle.

- New rc-buffers start with `use_count == 1`.
- Call `add_ref()` before sharing/storing a handle beyond the current owner lifetime.
- Call `release()` when that owner is done.
- The final release triggers resource cleanup once no resource pins remain.

### `qcap2_rcbuffer_delete(qcap2_rcbuffer_t* pRCBuffer)`

Alias-style destruction helper. It releases the initial/reference owner and should be used like `release()` for a uniquely owned rc-buffer.

### Count accessors

- `qcap2_rcbuffer_use_count()` returns strong handle references.
- `qcap2_rcbuffer_res_count()` returns active resource pins plus the initial resource lifetime hold.
- `qcap2_rcbuffer_weak_count()` is reserved for weak-reference style bookkeeping.

These are diagnostic helpers. Avoid writing core logic that depends on exact counts in concurrent code.

## Correct owner cleanup pattern

The rc-buffer free callback should release resources associated with `pData`, but it should not assume `pData` is a standalone allocation.

In the `MyVideoFrame` pattern:

1. `pData` points to `MyVideoFrame::av_frame`.
2. Callback uses `qcap2_container_of()` to recover `MyVideoFrame*`.
3. Callback can inspect/reset the frame and associated buffers.
4. The enclosing `MyVideoFrame` object is deleted separately by the application/free stack.

Example:

```cpp
qcap2_rcbuffer_t* pRCBuffer =
	qcap2_rcbuffer_new(&pVideoFrame->av_frame, MyVideoFrame::_on_free_resource);

qcap2_av_frame_t* pAVFrame =
	(qcap2_av_frame_t*)qcap2_rcbuffer_get_data(pRCBuffer);

// Fill pAVFrame and buffers...

qcap2_rcbuffer_delete(pRCBuffer); // invokes _on_free_resource(&pVideoFrame->av_frame)
delete pVideoFrame;              // releases the enclosing object and raw buffers
```

## Common pitfalls

- Do not allocate/copy a new `qcap2_av_frame_t` inside `qcap2_rcbuffer_new()`.
- Do not pass the enclosing object pointer to the callback if `pData` was an embedded member pointer.
- Do not clear or rewrite `pData` before invoking `pOnFreeResource`; `qcap2_container_of()` depends on the exact original address.
- Do not call `qcap2_container_of()` unless the pointer is known to be the address of the specified member in the specified owner type.
- Do not use `get_data()` as a lifetime pin; use `lock_data()`/`unlock_data()` for pinned access.
