#include "qcap2.sync.h"
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
typedef void* HWND;
#endif

#include <mutex>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <queue>
#include <vector>
#include <atomic>

#ifdef __linux__
#include <sys/eventfd.h>
#include <unistd.h>
#include <poll.h>
#endif


#ifdef __cplusplus
extern "C" {
#endif

// qcap2_benaphore_lock_t
// --- qcap2_benaphore_lock_t ---
typedef struct qcap2_benaphore_lock_priv_t {
    std::mutex* mtx;
} qcap2_benaphore_lock_priv_t;

qcap2_benaphore_lock_t* qcap2_benaphore_lock_new() {
    qcap2_benaphore_lock_priv_t* pThis = new qcap2_benaphore_lock_priv_t;
    pThis->mtx = new std::mutex();
    return (qcap2_benaphore_lock_t*)pThis;
}

void qcap2_benaphore_lock_delete(qcap2_benaphore_lock_t* pThis) {
    if (pThis) {
        qcap2_benaphore_lock_priv_t* p = (qcap2_benaphore_lock_priv_t*)pThis;
        delete p->mtx;
        delete p;
    }
}

void qcap2_benaphore_lock_lock(qcap2_benaphore_lock_t* pThis) {
    if (pThis) {
        ((qcap2_benaphore_lock_priv_t*)pThis)->mtx->lock();
    }
}

void qcap2_benaphore_lock_unlock(qcap2_benaphore_lock_t* pThis) {
    if (pThis) {
        ((qcap2_benaphore_lock_priv_t*)pThis)->mtx->unlock();
    }
}

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif

// --- qcap2_event_t ---
typedef struct qcap2_event_priv_t {
#ifdef __linux__
    int efd;
#else
    std::mutex* mtx;
    std::condition_variable* cv;
    bool signaled;
#endif
} qcap2_event_priv_t;

qcap2_event_t* qcap2_event_new() {
    qcap2_event_priv_t* pThis = new qcap2_event_priv_t;
#ifdef __linux__
    pThis->efd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
#else
    pThis->mtx = new std::mutex();
    pThis->cv = new std::condition_variable();
    pThis->signaled = false;
#endif
    return (qcap2_event_t*)pThis;
}

void qcap2_event_delete(qcap2_event_t* pThis) {
    if (pThis) {
        qcap2_event_priv_t* p = (qcap2_event_priv_t*)pThis;
#ifdef __linux__
        if (p->efd >= 0) close(p->efd);
#else
        delete p->cv;
        delete p->mtx;
#endif
        delete p;
    }
}

void qcap2_event_set_initial(qcap2_event_t* pThis, int nInitial) {
    if (pThis) {
        qcap2_event_priv_t* p = (qcap2_event_priv_t*)pThis;
#ifdef __linux__
        if (nInitial != 0) {
            uint64_t u = 1;
            write(p->efd, &u, sizeof(uint64_t));
        } else {
            uint64_t u;
            while (read(p->efd, &u, sizeof(uint64_t)) > 0);
        }
#else
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->signaled = (nInitial != 0);
#endif
    }
}

QRESULT qcap2_event_start(qcap2_event_t* pThis) {
    return pThis ? QCAP_RS_SUCCESSFUL : QCAP_RS_ERROR_GENERAL;
}

QRESULT qcap2_event_stop(qcap2_event_t* pThis) {
    if (pThis) {
        qcap2_event_priv_t* p = (qcap2_event_priv_t*)pThis;
#ifdef __linux__
        uint64_t u;
        while (read(p->efd, &u, sizeof(uint64_t)) > 0);
#else
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->signaled = false; // Reset on stop?
#endif
        return QCAP_RS_SUCCESSFUL;
    }
    return QCAP_RS_ERROR_GENERAL;
}

QRESULT qcap2_event_get_native_handle(qcap2_event_t* pThis, uintptr_t* pHandle) {
    if (pThis && pHandle) {
#ifdef __linux__
        *pHandle = (uintptr_t)((qcap2_event_priv_t*)pThis)->efd;
#else
        *pHandle = (uintptr_t)pThis;
#endif
        return QCAP_RS_SUCCESSFUL;
    }
    return QCAP_RS_ERROR_GENERAL;
}

QRESULT qcap2_event_notify(qcap2_event_t* pThis) {
    if (pThis) {
        qcap2_event_priv_t* p = (qcap2_event_priv_t*)pThis;
#ifdef __linux__
        uint64_t u = 1;
        write(p->efd, &u, sizeof(uint64_t));
#else
        {
            std::lock_guard<std::mutex> lock(*(p->mtx));
            p->signaled = true;
        }
        p->cv->notify_all();
#endif
        return QCAP_RS_SUCCESSFUL;
    }
    return QCAP_RS_ERROR_GENERAL;
}

QRESULT qcap2_event_wait(qcap2_event_t* pThis) {
    if (pThis) {
        qcap2_event_priv_t* p = (qcap2_event_priv_t*)pThis;
#ifdef __linux__
        struct pollfd pfd = { p->efd, POLLIN, 0 };
        while (true) {
            int ret = poll(&pfd, 1, -1);
            if (ret > 0) {
                uint64_t u;
                if (read(p->efd, &u, sizeof(uint64_t)) > 0) {
                    return QCAP_RS_SUCCESSFUL;
                } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    continue; // Another thread might have read it, wait again
                } else if (errno != EINTR) {
                    return QCAP_RS_ERROR_GENERAL;
                }
            } else if (ret < 0 && errno != EINTR) {
                return QCAP_RS_ERROR_GENERAL;
            }
        }
#else
        std::unique_lock<std::mutex> lock(*(p->mtx));
        p->cv->wait(lock, [p]{ return p->signaled; });
        p->signaled = false; // Auto-reset for simple implementation
        return QCAP_RS_SUCCESSFUL;
#endif
    }
    return QCAP_RS_ERROR_GENERAL;
}

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif

// --- qcap2_event_handlers_t ---
struct HandlerData {
    uintptr_t handle;
    qcap2_on_event_t callback;
    PVOID userData;
};

typedef struct qcap2_event_handlers_priv_t {
    std::mutex* mtx;
    std::vector<HandlerData>* handlers;
} qcap2_event_handlers_priv_t;

qcap2_event_handlers_t* qcap2_event_handlers_new() {
    qcap2_event_handlers_priv_t* pThis = new qcap2_event_handlers_priv_t;
    pThis->mtx = new std::mutex();
    pThis->handlers = new std::vector<HandlerData>();
    return (qcap2_event_handlers_t*)pThis;
}

void qcap2_event_handlers_delete(qcap2_event_handlers_t* pThis) {
    if (pThis) {
        qcap2_event_handlers_priv_t* p = (qcap2_event_handlers_priv_t*)pThis;
        delete p->handlers;
        delete p->mtx;
        delete p;
    }
}

QRESULT qcap2_event_handlers_start(qcap2_event_handlers_t* pThis) {
    return pThis ? QCAP_RS_SUCCESSFUL : QCAP_RS_ERROR_GENERAL;
}

QRESULT qcap2_event_handlers_stop(qcap2_event_handlers_t* pThis) {
    return pThis ? QCAP_RS_SUCCESSFUL : QCAP_RS_ERROR_GENERAL;
}

QRESULT qcap2_event_handlers_add_handler(qcap2_event_handlers_t* pThis, uintptr_t nHandle, qcap2_on_event_t pOnEvent, PVOID pUserData) {
    if (pThis && pOnEvent) {
        qcap2_event_handlers_priv_t* p = (qcap2_event_handlers_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->handlers->push_back({nHandle, pOnEvent, pUserData});
        return QCAP_RS_SUCCESSFUL;
    }
    return QCAP_RS_ERROR_GENERAL;
}

QRESULT qcap2_event_handlers_remove_handler(qcap2_event_handlers_t* pThis, uintptr_t nHandle) {
    if (pThis) {
        qcap2_event_handlers_priv_t* p = (qcap2_event_handlers_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        for (auto it = p->handlers->begin(); it != p->handlers->end(); ) {
            if (it->handle == nHandle) {
                it = p->handlers->erase(it);
            } else {
                ++it;
            }
        }
        return QCAP_RS_SUCCESSFUL;
    }
    return QCAP_RS_ERROR_GENERAL;
}

QRESULT qcap2_event_handlers_invoke(qcap2_event_handlers_t* pThis, qcap2_on_event_t pOnEvent, PVOID pUserData) {
    if (pThis) {
        qcap2_event_handlers_priv_t* p = (qcap2_event_handlers_priv_t*)pThis;
        std::vector<HandlerData> handlersCopy;
        {
            std::lock_guard<std::mutex> lock(*(p->mtx));
            handlersCopy = *(p->handlers);
        }

        for (const auto& h : handlersCopy) {
            h.callback(h.userData);
        }

        // Also invoke the specific one if passed
        if (pOnEvent) {
            pOnEvent(pUserData);
        }

        return QCAP_RS_SUCCESSFUL;
    }
    return QCAP_RS_ERROR_GENERAL;
}

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif

// --- qcap2_rcbuffer_queue_t ---
typedef struct qcap2_rcbuffer_queue_priv_t {
    std::mutex* mtx;
    std::condition_variable* cv_push;
    std::condition_variable* cv_pop;
    std::queue<qcap2_rcbuffer_t*>* q;
    int maxBuffers;
    bool running;
} qcap2_rcbuffer_queue_priv_t;

qcap2_rcbuffer_queue_t* qcap2_rcbuffer_queue_new() {
    qcap2_rcbuffer_queue_priv_t* pThis = new qcap2_rcbuffer_queue_priv_t;
    pThis->mtx = new std::mutex();
    pThis->cv_push = new std::condition_variable();
    pThis->cv_pop = new std::condition_variable();
    pThis->q = new std::queue<qcap2_rcbuffer_t*>();
    pThis->maxBuffers = 0;
    pThis->running = false;
    return (qcap2_rcbuffer_queue_t*)pThis;
}

void qcap2_rcbuffer_queue_delete(qcap2_rcbuffer_queue_t* pThis) {
    if (pThis) {
        qcap2_rcbuffer_queue_priv_t* p = (qcap2_rcbuffer_queue_priv_t*)pThis;
        delete p->q;
        delete p->cv_pop;
        delete p->cv_push;
        delete p->mtx;
        delete p;
    }
}

void qcap2_rcbuffer_queue_set_max_buffers(qcap2_rcbuffer_queue_t* pThis, int nMaxBuffers) {
    if (pThis) {
        qcap2_rcbuffer_queue_priv_t* p = (qcap2_rcbuffer_queue_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->maxBuffers = nMaxBuffers;
    }
}

void qcap2_rcbuffer_queue_set_event(qcap2_rcbuffer_queue_t* pThis, qcap2_event_t* pEvent) {
    // Optional integration point, skipped for pure std:: implementations
}

void qcap2_rcbuffer_queue_set_buffers(qcap2_rcbuffer_queue_t* pThis, qcap2_rcbuffer_t** pBuffers) {
    // Pre-allocate buffers integration point
}

int qcap2_rcbuffer_queue_get_buffer_count(qcap2_rcbuffer_queue_t* pThis) {
    if (pThis) {
        qcap2_rcbuffer_queue_priv_t* p = (qcap2_rcbuffer_queue_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        return p->q->size();
    }
    return 0;
}

bool qcap2_rcbuffer_queue_is_full(qcap2_rcbuffer_queue_t* pThis) {
    if (pThis) {
        qcap2_rcbuffer_queue_priv_t* p = (qcap2_rcbuffer_queue_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        if (p->maxBuffers > 0 && p->q->size() >= (size_t)p->maxBuffers) return true;
    }
    return false;
}

bool qcap2_rcbuffer_queue_is_empty(qcap2_rcbuffer_queue_t* pThis) {
    if (pThis) {
        qcap2_rcbuffer_queue_priv_t* p = (qcap2_rcbuffer_queue_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        return p->q->empty();
    }
    return true;
}

QRESULT qcap2_rcbuffer_queue_start(qcap2_rcbuffer_queue_t* pThis) {
    if (pThis) {
        qcap2_rcbuffer_queue_priv_t* p = (qcap2_rcbuffer_queue_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->running = true;
        return QCAP_RS_SUCCESSFUL;
    }
    return QCAP_RS_ERROR_GENERAL;
}

QRESULT qcap2_rcbuffer_queue_stop(qcap2_rcbuffer_queue_t* pThis) {
    if (pThis) {
        qcap2_rcbuffer_queue_priv_t* p = (qcap2_rcbuffer_queue_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->running = false;
        p->cv_pop->notify_all();
        p->cv_push->notify_all();
        return QCAP_RS_SUCCESSFUL;
    }
    return QCAP_RS_ERROR_GENERAL;
}

QRESULT qcap2_rcbuffer_queue_push(qcap2_rcbuffer_queue_t* pThis, qcap2_rcbuffer_t* pRCBuffer) {
    if (pThis && pRCBuffer) {
        qcap2_rcbuffer_queue_priv_t* p = (qcap2_rcbuffer_queue_priv_t*)pThis;
        std::unique_lock<std::mutex> lock(*(p->mtx));

        p->cv_push->wait(lock, [p]{
            return !p->running || (p->maxBuffers <= 0 || p->q->size() < (size_t)p->maxBuffers);
        });

        if (!p->running) return QCAP_RS_ERROR_GENERAL;

        p->q->push(pRCBuffer);
        p->cv_pop->notify_one();
        return QCAP_RS_SUCCESSFUL;
    }
    return QCAP_RS_ERROR_GENERAL;
}

QRESULT qcap2_rcbuffer_queue_pop(qcap2_rcbuffer_queue_t* pThis, qcap2_rcbuffer_t** ppRCBuffer) {
    if (pThis && ppRCBuffer) {
        qcap2_rcbuffer_queue_priv_t* p = (qcap2_rcbuffer_queue_priv_t*)pThis;
        std::unique_lock<std::mutex> lock(*(p->mtx));

        p->cv_pop->wait(lock, [p]{
            return !p->running || !p->q->empty();
        });

        if (!p->running && p->q->empty()) return QCAP_RS_ERROR_GENERAL;

        *ppRCBuffer = p->q->front();
        p->q->pop();
        p->cv_push->notify_one();
        return QCAP_RS_SUCCESSFUL;
    }
    return QCAP_RS_ERROR_GENERAL;
}

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif

// --- qcap2_timer_t ---
typedef struct qcap2_timer_priv_t {
    uint64_t interval_ms;
    std::mutex* mtx;
    std::condition_variable* cv;
    bool running;
    bool triggered;
    std::thread* timer_thread;
} qcap2_timer_priv_t;

static void timer_thread_func(qcap2_timer_priv_t* p) {
    while (true) {
        std::unique_lock<std::mutex> lock(*(p->mtx));
        if (!p->running) break;

        // Wait for stop or interval
        if (p->cv->wait_for(lock, std::chrono::milliseconds(p->interval_ms), [p]{ return !p->running; })) {
            // Stopped
            break;
        } else {
            // Timeout -> triggered
            p->triggered = true;
            p->cv->notify_all(); // Wake up any wait calls
        }
    }
}

qcap2_timer_t* qcap2_timer_new() {
    qcap2_timer_priv_t* pThis = new qcap2_timer_priv_t;
    pThis->interval_ms = 0;
    pThis->mtx = new std::mutex();
    pThis->cv = new std::condition_variable();
    pThis->running = false;
    pThis->triggered = false;
    pThis->timer_thread = nullptr;
    return (qcap2_timer_t*)pThis;
}

void qcap2_timer_delete(qcap2_timer_t* pThis) {
    if (pThis) {
        qcap2_timer_stop(pThis); // Ensure it's stopped
        qcap2_timer_priv_t* p = (qcap2_timer_priv_t*)pThis;
        delete p->cv;
        delete p->mtx;
        delete p;
    }
}

void qcap2_timer_set_interval(qcap2_timer_t* pThis, uint64_t nInterval) {
    if (pThis) {
        qcap2_timer_priv_t* p = (qcap2_timer_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->interval_ms = nInterval;
    }
}

QRESULT qcap2_timer_start(qcap2_timer_t* pThis) {
    if (pThis) {
        qcap2_timer_priv_t* p = (qcap2_timer_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        if (!p->running) {
            p->running = true;
            p->triggered = false;
            p->timer_thread = new std::thread(timer_thread_func, p);
        }
        return QCAP_RS_SUCCESSFUL;
    }
    return QCAP_RS_ERROR_GENERAL;
}

QRESULT qcap2_timer_stop(qcap2_timer_t* pThis) {
    if (pThis) {
        qcap2_timer_priv_t* p = (qcap2_timer_priv_t*)pThis;
        std::thread* thread_to_join = nullptr;
        {
            std::lock_guard<std::mutex> lock(*(p->mtx));
            if (p->running) {
                p->running = false;
                p->cv->notify_all(); // Wake up timer thread
                thread_to_join = p->timer_thread;
                p->timer_thread = nullptr;
            }
        }
        if (thread_to_join) {
            thread_to_join->join();
            delete thread_to_join;
        }
        return QCAP_RS_SUCCESSFUL;
    }
    return QCAP_RS_ERROR_GENERAL;
}

QRESULT qcap2_timer_get_native_handle(qcap2_timer_t* pThis, uintptr_t* pHandle) {
    if (pThis && pHandle) {
        *pHandle = (uintptr_t)pThis;
        return QCAP_RS_SUCCESSFUL;
    }
    return QCAP_RS_ERROR_GENERAL;
}

QRESULT qcap2_timer_wait(qcap2_timer_t* pThis, uint64_t* pExpirations) {
    if (pThis) {
        qcap2_timer_priv_t* p = (qcap2_timer_priv_t*)pThis;
        std::unique_lock<std::mutex> lock(*(p->mtx));
        p->cv->wait(lock, [p]{ return p->triggered || !p->running; });

        if (!p->running && !p->triggered) return QCAP_RS_ERROR_GENERAL;

        p->triggered = false;
        if (pExpirations) *pExpirations = 1;
        return QCAP_RS_SUCCESSFUL;
    }
    return QCAP_RS_ERROR_GENERAL;
}

QRESULT qcap2_timer_next(qcap2_timer_t* pThis, uint64_t nDuration) {
    if (pThis) {
        qcap2_timer_priv_t* p = (qcap2_timer_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->interval_ms = nDuration;
        return QCAP_RS_SUCCESSFUL;
    }
    return QCAP_RS_ERROR_GENERAL;
}

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif

// --- qcap2_window_t ---
typedef struct qcap2_window_priv_t {
    int backendType;
    int rect[4];
    bool fullScreen;
} qcap2_window_priv_t;

qcap2_window_t* qcap2_window_new() {
    qcap2_window_priv_t* pThis = new qcap2_window_priv_t;
    pThis->backendType = 0;
    pThis->rect[0] = pThis->rect[1] = pThis->rect[2] = pThis->rect[3] = 0;
    pThis->fullScreen = false;
    return (qcap2_window_t*)pThis;
}

void qcap2_window_delete(qcap2_window_t* pThis) {
    if (pThis) delete (qcap2_window_priv_t*)pThis;
}

void qcap2_window_set_backend_type(qcap2_window_t* pThis, int nType) {
    if (pThis) ((qcap2_window_priv_t*)pThis)->backendType = nType;
}

void qcap2_window_set_rect(qcap2_window_t* pThis, int x, int y, int w, int h) {
    if (pThis) {
        qcap2_window_priv_t* p = (qcap2_window_priv_t*)pThis;
        p->rect[0] = x;
        p->rect[1] = y;
        p->rect[2] = w;
        p->rect[3] = h;
    }
}

void qcap2_window_set_full_screen(qcap2_window_t* pThis, bool bFullScreen) {
    if (pThis) ((qcap2_window_priv_t*)pThis)->fullScreen = bFullScreen;
}

QRESULT qcap2_window_start(qcap2_window_t* pThis) {
    return pThis ? QCAP_RS_SUCCESSFUL : QCAP_RS_ERROR_GENERAL;
}

QRESULT qcap2_window_stop(qcap2_window_t* pThis) {
    return pThis ? QCAP_RS_SUCCESSFUL : QCAP_RS_ERROR_GENERAL;
}

QRESULT qcap2_window_get_hwnd(qcap2_window_t* pThis, HWND* pHwnd) {
    if (pThis && pHwnd) {
        *pHwnd = NULL;
        return QCAP_RS_SUCCESSFUL;
    }
    return QCAP_RS_ERROR_GENERAL;
}

QRESULT qcap2_window_get_native_handle(qcap2_window_t* pThis, uintptr_t* pHandle) {
    if (pThis && pHandle) {
        *pHandle = (uintptr_t)pThis;
        return QCAP_RS_SUCCESSFUL;
    }
    return QCAP_RS_ERROR_GENERAL;
}

QRESULT qcap2_window_handle_events(qcap2_window_t* pThis) {
    return pThis ? QCAP_RS_SUCCESSFUL : QCAP_RS_ERROR_GENERAL;
}

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif

// qcap2_block_lock_t
typedef struct qcap2_block_lock_priv_t {
    std::mutex* mtx;
    std::condition_variable* cv;
    bool granted;
    bool entered;
} qcap2_block_lock_priv_t;

qcap2_block_lock_t* qcap2_block_lock_new() {
    qcap2_block_lock_priv_t* pThis = new qcap2_block_lock_priv_t;
    pThis->mtx = new std::mutex();
    pThis->cv = new std::condition_variable();
    pThis->granted = false; // Initial state false to test blocking
    pThis->entered = false;
    return (qcap2_block_lock_t*)pThis;
}

void qcap2_block_lock_delete(qcap2_block_lock_t* pThis) {
    if (pThis) {
        qcap2_block_lock_priv_t* p = (qcap2_block_lock_priv_t*)pThis;
        delete p->cv;
        delete p->mtx;
        delete p;
    }
}

void qcap2_block_lock_grant(qcap2_block_lock_t* pThis, bool bGranted) {
    if (pThis) {
        qcap2_block_lock_priv_t* p = (qcap2_block_lock_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->granted = bGranted;
        if (bGranted) p->cv->notify_all();
    }
}

bool qcap2_block_lock_enter(qcap2_block_lock_t* pThis) {
    if (pThis) {
        qcap2_block_lock_priv_t* p = (qcap2_block_lock_priv_t*)pThis;
        std::unique_lock<std::mutex> lock(*(p->mtx));
        if (p->cv->wait_for(lock, std::chrono::milliseconds(500), [p]{ return p->granted; })) {
            p->entered = true;
            return true;
        }
        return false;
    }
    return false;
}

void qcap2_block_lock_leave(qcap2_block_lock_t* pThis) {
    if (pThis) {
        ((qcap2_block_lock_priv_t*)pThis)->entered = false;
    }
}

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif

// --- qcap2_binder_t ---
typedef struct qcap2_binder_priv_t {
    std::atomic<intptr_t> cookie_counter;
} qcap2_binder_priv_t;

qcap2_binder_t* qcap2_binder_new() {
    qcap2_binder_priv_t* pThis = new qcap2_binder_priv_t;
    pThis->cookie_counter = 1;
    return (qcap2_binder_t*)pThis;
}

void qcap2_binder_delete(qcap2_binder_t* pThis) {
    if (pThis) delete (qcap2_binder_priv_t*)pThis;
}

QRESULT qcap2_binder_start(qcap2_binder_t* pThis) {
    return pThis ? QCAP_RS_SUCCESSFUL : QCAP_RS_ERROR_GENERAL;
}

QRESULT qcap2_binder_stop(qcap2_binder_t* pThis) {
    return pThis ? QCAP_RS_SUCCESSFUL : QCAP_RS_ERROR_GENERAL;
}

intptr_t qcap2_binder_vsrc_vsca(qcap2_binder_t* pThis, qcap2_video_source_t* pSrc, qcap2_video_scaler_t* pSink) {
    if (pThis) {
        qcap2_binder_priv_t* p = (qcap2_binder_priv_t*)pThis;
        return p->cookie_counter.fetch_add(1);
    }
    return 0;
}

QRESULT qcap2_binder_unlink(qcap2_binder_t* pThis, intptr_t cookie) {
    return pThis && cookie > 0 ? QCAP_RS_SUCCESSFUL : QCAP_RS_ERROR_GENERAL;
}

#ifdef __cplusplus
}
#endif
