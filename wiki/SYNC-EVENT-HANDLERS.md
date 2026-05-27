# qcap2_event_handlers_t usage and implementation notes

`qcap2_event_handlers_t` manages and dispatches events asynchronously for multiple registered file descriptors (handles). It provides an event loop powered by a background monitoring thread on Linux.

## Key Design Principles

### 1. Dedicated Monitoring Thread
- **Threading Model**: A dedicated background thread is created when `qcap2_event_handlers_start()` is called, and is terminated when `qcap2_event_handlers_stop()` is called.
- **Multiplexing with `poll()`**: On Linux, the monitoring thread uses the `poll()` system call to monitor all registered event handles (represented as file descriptors) concurrently.
- **Wakeup Descriptor (`eventfd`)**: To reactively interrupt the blocking `poll()` call when handlers are added/removed or when the event loop is stopped, an internal `eventfd` (`wakeup_fd`) is automatically registered as the first monitored descriptor in the `poll()` array.

### 2. Thread Safety and Safe Callback Dispatch
`qcap2_event_handlers_t` is designed to be fully thread-safe for concurrent operations. Below are the specific thread-safety guarantees and concurrency mechanisms:

- **Central Mutex Synchronization**: An internal mutex (`p->mtx`) synchronizes access to all shared resources:
  - The handler registry (`p->handlers`).
  - The pending invocation queue (`p->pending_invokes`).
  - Loop state control flags (e.g. `p->running`).
- **Concurrent Add, Remove, and Invoke**:
  - `qcap2_event_handlers_add_handler()`, `qcap2_event_handlers_remove_handler()`, and `qcap2_event_handlers_invoke()` can be safely called from multiple threads concurrently.
  - Adding or removing a handler automatically wakes up the background monitoring thread to dynamically rebuild its polling target list.
- **Deadlock-Free Callback Dispatch**:
  - Registered callbacks (`dummy_event_handler`, etc.) and scheduled invoke callbacks are **always executed outside of the internal mutex lock**.
  - This prevents deadlocks if a callback calls back into the event handlers library (e.g., to add/remove handlers, trigger `invoke()`, or call `stop()`).
- **Stale Callback Prevention (Race Condition Protection)**:
  - If a file descriptor becomes ready and triggers `poll()` but another thread calls `remove_handler()` concurrently, the monitoring thread re-acquires the lock and checks if the handler is still registered before calling its callback. If it has been removed, the callback is skipped.
- **Start and Stop Re-entrancy**:
  - `qcap2_event_handlers_start()` and `qcap2_event_handlers_stop()` are thread-safe and can be safely called from multiple threads concurrently. `stop()` ensures the background thread is fully joined and all resources are cleaned up before returning.

### 3. Asynchronous `qcap2_event_handlers_invoke()` Semantics
- When calling `qcap2_event_handlers_invoke(pThis, pOnEvent, pUserData)`, the callback `pOnEvent(pUserData)` is queued internally and executed **asynchronously in the context of the event-handlers' background monitoring thread**.
- It does **not** iterate or dispatch other registered background handlers.

---

## Function Semantics

### `qcap2_event_handlers_start(qcap2_event_handlers_t* pThis)`
- Spawns the dedicated monitoring thread and initializes the `wakeup_fd` (`eventfd`).
- Returns success if already running.

### `qcap2_event_handlers_stop(qcap2_event_handlers_t* pThis)`
- Signals the background thread to terminate, writes to the `wakeup_fd` to wake up the blocking `poll()` call immediately, joins/deletes the thread, and closes the `wakeup_fd`.

### `qcap2_event_handlers_add_handler(qcap2_event_handlers_t* pThis, uintptr_t nHandle, qcap2_on_event_t pOnEvent, PVOID pUserData)`
- Registers a new file descriptor `nHandle` with the handler callback and user data.
- If the monitor thread is already running, triggers a write to `wakeup_fd` so `poll()` is interrupted and the active file descriptor array is instantly rebuilt on the next iteration.

### `qcap2_event_handlers_remove_handler(qcap2_event_handlers_t* pThis, uintptr_t nHandle)`
- Unregisters the file descriptor `nHandle`.
- Triggers a write to `wakeup_fd` to instantly rebuild the active `poll()` array.

### `qcap2_event_handlers_invoke(qcap2_event_handlers_t* pThis, qcap2_on_event_t pOnEvent, PVOID pUserData)`
- Queues the specified `pOnEvent(pUserData)` callback to be executed asynchronously in the background monitoring thread.

---

## qcap2_timer_t Timerfd Implementation

On Linux, `qcap2_timer_t` is implemented using the kernel's `timerfd` API to enable efficient, non-sleeping, descriptor-based periodic and one-shot timers.

### Key Semantics & APIs:
- **CLOCK_MONOTONIC**: The timerfd uses the monotonic system clock, which is immune to system time changes/adjustments.
- **Native Handle as File Descriptor**: `qcap2_timer_get_native_handle()` returns the underlying Linux `timerfd` file descriptor (cast as `uintptr_t`).
- **APIs**:
  - `qcap2_timer_set_interval(pThis, nInterval)`: Configures the timer interval in milliseconds.
  - `qcap2_timer_start(pThis)`: Arms the `timerfd` periodically using the set `interval_ms`.
  - `qcap2_timer_stop(pThis)`: Disarms the timer by setting the timerfd specification to 0.
  - `qcap2_timer_wait(pThis, pExpirations)`: Blocks using `poll()` until the timerfd is readable, then reads and returns the expiration count.
  - `qcap2_timer_next(pThis, nDuration)`: Configures a single-shot timer for `nDuration` milliseconds.
