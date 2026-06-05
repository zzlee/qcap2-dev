# QCAP2 Buffer Recycling Architecture Analysis

This document analyzes the design of the QCAP2 input-recycling (HPR) and output-recycling (PPR) processes across multiple software engineering perspectives.

---

## 1. Pipeline Dataflow Perspective
The architecture employs a strict separation of input and output lifecycles to enable zero-copy processing pipelines.

```mermaid
graph TD
    subgraph HPR (Input Lifecycle / Host-Pipeline-Recycle)
        A[Host/User] -- 1. push --> B[Component Input]
        B -- 2. Process --> C[input_recycled_queue]
        C -- 3. pop_input --> A
    end
    
    subgraph PPR (Output Lifecycle / Pipeline-Push-Recycle)
        D[Component Output] -- 1. pop --> E[Host/User]
        E -- 2. Read/Consume --> F[output_recycled_queue]
        F -- 3. push_output --> D
    end
```

* **HPR (Host-Pipeline-Recycle) for Inputs:**
  * **Ownership Flow:** The Host allocates/obtains a buffer, fills it, and transfers processing ownership to the component (`push`). Once the component finishes using the buffer (e.g., finishes encoding/scaling), it returns the buffer to the `input_recycled_queue`. The user reclaims it (`pop_input`) to reuse it.
* **PPR (Pipeline-Push-Recycle) for Outputs:**
  * **Ownership Flow:** The component allocates/obtains a shell, writes output data to it, and gives read ownership to the user (`pop`). Once the user is done reading, they return it back to the component's `output_recycled_queue` (`push_output`) for future outputs.

---

## 2. Memory Management & Lifecycle Perspective
The framework leverages a reference-counted structure, [qcap2_rcbuffer_t](file:///home/zzlee/docker/qcap2-dev/include/qcap2.buffer.h#L12), to coordinate memory lifetime between threads.

* **Reference Count Accounting:**
  * `qcap2_rcbuffer_new()` starts with `ref_count = 1`.
  * Queuing a buffer (`qcap2_rcbuffer_queue_push`) increments `ref_count` by 1.
  * Dequeuing a buffer (`qcap2_rcbuffer_queue_pop`) transfers the reference without decrementing, keeping the ref count unchanged.
  * Releasing a buffer (`qcap2_rcbuffer_release`) decrements `ref_count` by 1. When `ref_count == 0`, the custom deleter is fired.
* **Zero-Allocation Steady State:**
  Once the pipeline is primed (e.g., 5 input buffers and 5 output buffers), no further `malloc` or `free` calls occur in the hot processing loop. Steady-state memory consumption is completely constant.

---

## 3. Concurrency & Synchronization Perspective
Thread safety and synchronization are handled by the [qcap2_rcbuffer_queue_t](file:///home/zzlee/docker/qcap2-dev/src/qcap2.sync.cpp#L691) queue engine.

* **Mutual Exclusion:**
  Every queue operation is protected by a `std::mutex` to prevent race conditions during concurrent pushes and pops from different threads (e.g., worker threads vs. user threads).
* **Backpressure and Thread Blocking:**
  * **Underflow (Empty Queue):** A consumer calling `pop` blocks on `cv_pop` if no buffers are available. This acts as a natural rate-limiter, blocking the consumer thread until data is produced.
  * **Overflow (Full Queue):** If `maxBuffers > 0` and the queue is full, a producer calling `push` blocks on `cv_push`. This manages backpressure, preventing fast producers from exhausting system memory.

---

## 4. Performance & DX (Developer Experience) Perspective

| Aspect | Impact / Advantage | Trade-off / Complexity |
| :--- | :--- | :--- |
| **Throughput & Latency** | Eliminates heap allocation overhead in high-frequency loops (e.g., 60fps video frames). Reusing memory blocks improves CPU cache locality. | Requires careful matching of `push` and `release` calls; failure to release popped references results in memory leaks. |
| **Zero-Copy Pipelines** | Buffers can be passed directly from one component's output to another's input (e.g., encoder output -> decoder input) without copying raw payload data. | Requires strict lifecycle synchronization. If a buffer is recycled to the encoder while the decoder is still reading it, data corruption occurs. |
| **API DX** | Exposing `pop_input` and `push_output` wraps the queue details cleanly, presenting simple verbs to developers. | The developer must understand who currently owns the reference to avoid double-free or memory leak issues. |
