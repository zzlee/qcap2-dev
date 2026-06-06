#include "qcap2.sync.h"
#include "qcap2.buffer.h"
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
#include <new>

#ifdef __linux__
#include <sys/eventfd.h>
#include <sys/timerfd.h>
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

QRESULT qcap2_event_wait_count(qcap2_event_t* pThis, uint64_t* pCount) {
    if (pThis && pCount) {
        qcap2_event_priv_t* p = (qcap2_event_priv_t*)pThis;
#ifdef __linux__
        struct pollfd pfd = { p->efd, POLLIN, 0 };
        while (true) {
            int ret = poll(&pfd, 1, -1);
            if (ret > 0) {
                uint64_t u = 0;
                if (read(p->efd, &u, sizeof(uint64_t)) > 0) {
                    *pCount = u;
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
        *pCount = p->signaled ? 1 : 0;
        p->signaled = false; // Auto-reset for simple implementation
        return QCAP_RS_SUCCESSFUL;
#endif
    }
    return QCAP_RS_ERROR_GENERAL;
}

QRESULT qcap2_event_wait(qcap2_event_t* pThis) {
    uint64_t dummy = 0;
    return qcap2_event_wait_count(pThis, &dummy);
}

QRESULT qcap2_event_read(qcap2_event_t* pThis, uint64_t* pCount) {
    if (pThis && pCount) {
        qcap2_event_priv_t* p = (qcap2_event_priv_t*)pThis;
#ifdef __linux__
        uint64_t u = 0;
        if (read(p->efd, &u, sizeof(uint64_t)) > 0) {
            *pCount = u;
            return QCAP_RS_SUCCESSFUL;
        }
        // EAGAIN means already drained by another thread
        *pCount = 0;
        return (errno == EAGAIN || errno == EWOULDBLOCK)
            ? QCAP_RS_SUCCESSFUL
            : QCAP_RS_ERROR_GENERAL;
#else
        std::lock_guard<std::mutex> lock(*(p->mtx));
        *pCount = p->signaled ? 1 : 0;
        p->signaled = false;
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

struct InvokeData {
    qcap2_on_event_t callback;
    PVOID userData;
};

typedef struct qcap2_event_handlers_priv_t {
    std::mutex* mtx;
    std::vector<HandlerData>* handlers;
    std::vector<InvokeData>* pending_invokes;
    std::thread* monitor_thread;
    bool running;
#ifdef __linux__
    int wakeup_fd;
#else
    std::condition_variable* cv;
#endif
} qcap2_event_handlers_priv_t;

#ifdef __linux__
static void qcap2_event_handlers_monitor_thread_func(qcap2_event_handlers_priv_t* p) {
    while (true) {
        std::vector<struct pollfd> fds;
        std::vector<HandlerData> active_handlers;
        int wakeup_fd = -1;
        std::vector<InvokeData> invokes_to_run;

        {
            std::lock_guard<std::mutex> lock(*(p->mtx));
            if (!p->running) {
                break;
            }
            wakeup_fd = p->wakeup_fd;

            if (!p->pending_invokes->empty()) {
                invokes_to_run = std::move(*(p->pending_invokes));
                p->pending_invokes->clear();
            }

            // Add wakeup_fd as fds[0]
            struct pollfd wfd = { wakeup_fd, POLLIN, 0 };
            fds.push_back(wfd);

            // Add all registered handlers
            for (const auto& h : *(p->handlers)) {
                struct pollfd fd_entry = { (int)h.handle, POLLIN, 0 };
                fds.push_back(fd_entry);
                active_handlers.push_back(h);
            }
        }

        // Execute invokes outside the lock
        for (const auto& inv : invokes_to_run) {
            inv.callback(inv.userData);
        }

        // Check if stopped during callbacks
        {
            std::lock_guard<std::mutex> lock(*(p->mtx));
            if (!p->running) {
                break;
            }
        }

        int ret = poll(fds.data(), fds.size(), -1);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break; // Error or interrupted
        }

        // Check if stopped
        {
            std::lock_guard<std::mutex> lock(*(p->mtx));
            if (!p->running) {
                break;
            }
        }

        // Check wakeup_fd
        if (fds[0].revents & POLLIN) {
            uint64_t val = 0;
            read(wakeup_fd, &val, sizeof(val));
        }

        // Check other fds
        for (size_t i = 1; i < fds.size(); ++i) {
            if (fds[i].revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL)) {
                // To avoid invoking a callback of a handler that was just removed
                bool exists = false;
                HandlerData h;
                {
                    std::lock_guard<std::mutex> lock(*(p->mtx));
                    for (const auto& current_handler : *(p->handlers)) {
                        if (current_handler.handle == active_handlers[i - 1].handle &&
                            current_handler.callback == active_handlers[i - 1].callback &&
                            current_handler.userData == active_handlers[i - 1].userData) {
                            exists = true;
                            h = current_handler;
                            break;
                        }
                    }
                }
                if (exists) {
                    h.callback(h.userData);
                }
            }
        }
    }
}
#else
static void qcap2_event_handlers_monitor_thread_func(qcap2_event_handlers_priv_t* p) {
    while (true) {
        std::vector<InvokeData> invokes_to_run;
        {
            std::unique_lock<std::mutex> lock(*(p->mtx));
            p->cv->wait(lock, [p] { return !p->running || !p->pending_invokes->empty(); });
            if (!p->running && p->pending_invokes->empty()) {
                break;
            }
            if (!p->pending_invokes->empty()) {
                invokes_to_run = std::move(*(p->pending_invokes));
                p->pending_invokes->clear();
            }
        }
        for (const auto& inv : invokes_to_run) {
            inv.callback(inv.userData);
        }
    }
}
#endif

qcap2_event_handlers_t* qcap2_event_handlers_new() {
    qcap2_event_handlers_priv_t* pThis = new qcap2_event_handlers_priv_t;
    pThis->mtx = new std::mutex();
    pThis->handlers = new std::vector<HandlerData>();
    pThis->pending_invokes = new std::vector<InvokeData>();
    pThis->monitor_thread = nullptr;
    pThis->running = false;
#ifdef __linux__
    pThis->wakeup_fd = -1;
#else
    pThis->cv = new std::condition_variable();
#endif
    return (qcap2_event_handlers_t*)pThis;
}

void qcap2_event_handlers_delete(qcap2_event_handlers_t* pThis) {
    if (pThis) {
        qcap2_event_handlers_stop(pThis);
        qcap2_event_handlers_priv_t* p = (qcap2_event_handlers_priv_t*)pThis;
        delete p->pending_invokes;
        delete p->handlers;
#ifndef __linux__
        delete p->cv;
#endif
        delete p->mtx;
        delete p;
    }
}

QRESULT qcap2_event_handlers_start(qcap2_event_handlers_t* pThis) {
    if (pThis) {
        qcap2_event_handlers_priv_t* p = (qcap2_event_handlers_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        if (!p->running) {
            p->running = true;
#ifdef __linux__
            p->wakeup_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
            if (p->wakeup_fd < 0) {
                p->running = false;
                return QCAP_RS_ERROR_GENERAL;
            }
#endif
            p->monitor_thread = new std::thread(qcap2_event_handlers_monitor_thread_func, p);
        }
        return QCAP_RS_SUCCESSFUL;
    }
    return QCAP_RS_ERROR_GENERAL;
}

QRESULT qcap2_event_handlers_stop(qcap2_event_handlers_t* pThis) {
    if (pThis) {
        qcap2_event_handlers_priv_t* p = (qcap2_event_handlers_priv_t*)pThis;
        std::thread* thread_to_join = nullptr;
        {
            std::lock_guard<std::mutex> lock(*(p->mtx));
            if (p->running) {
                p->running = false;
#ifdef __linux__
                if (p->wakeup_fd >= 0) {
                    uint64_t val = 1;
                    write(p->wakeup_fd, &val, sizeof(val));
                }
#else
                p->cv->notify_all();
#endif
                thread_to_join = p->monitor_thread;
                p->monitor_thread = nullptr;
            }
        }
        if (thread_to_join) {
            thread_to_join->join();
            delete thread_to_join;
        }
#ifdef __linux__
        {
            std::lock_guard<std::mutex> lock(*(p->mtx));
            if (p->wakeup_fd >= 0) {
                close(p->wakeup_fd);
                p->wakeup_fd = -1;
            }
        }
#endif
        return QCAP_RS_SUCCESSFUL;
    }
    return QCAP_RS_ERROR_GENERAL;
}

QRESULT qcap2_event_handlers_add_handler(qcap2_event_handlers_t* pThis, uintptr_t nHandle, qcap2_on_event_t pOnEvent, PVOID pUserData) {
    if (pThis && pOnEvent) {
        qcap2_event_handlers_priv_t* p = (qcap2_event_handlers_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->handlers->push_back({nHandle, pOnEvent, pUserData});
#ifdef __linux__
        if (p->running && p->wakeup_fd >= 0) {
            uint64_t val = 1;
            write(p->wakeup_fd, &val, sizeof(val));
        }
#endif
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
#ifdef __linux__
        if (p->running && p->wakeup_fd >= 0) {
            uint64_t val = 1;
            write(p->wakeup_fd, &val, sizeof(val));
        }
#endif
        return QCAP_RS_SUCCESSFUL;
    }
    return QCAP_RS_ERROR_GENERAL;
}

QRESULT qcap2_event_handlers_invoke(qcap2_event_handlers_t* pThis, qcap2_on_event_t pOnEvent, PVOID pUserData) {
    if (pThis && pOnEvent) {
        qcap2_event_handlers_priv_t* p = (qcap2_event_handlers_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->pending_invokes->push_back({pOnEvent, pUserData});
#ifdef __linux__
        if (p->running && p->wakeup_fd >= 0) {
            uint64_t val = 1;
            write(p->wakeup_fd, &val, sizeof(val));
        }
#else
        if (p->running) {
            p->cv->notify_all();
        }
#endif
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
    std::mutex mtx;
    std::condition_variable cv_push;
    std::condition_variable cv_pop;
    std::queue<qcap2_rcbuffer_t*> q;
    qcap2_event_t* event;
    int maxBuffers;
    bool running;
} qcap2_rcbuffer_queue_priv_t;

static void qcap2_rcbuffer_queue_notify_event(qcap2_event_t* pEvent) {
    if (pEvent) {
        qcap2_event_notify(pEvent);
    }
}

static void qcap2_rcbuffer_queue_release_all(std::queue<qcap2_rcbuffer_t*>& q) {
    while (!q.empty()) {
        qcap2_rcbuffer_t* pRCBuffer = q.front();
        q.pop();
        if (pRCBuffer) {
            qcap2_rcbuffer_release(pRCBuffer);
        }
    }
}

qcap2_rcbuffer_queue_t* qcap2_rcbuffer_queue_new() {
    qcap2_rcbuffer_queue_priv_t* pThis = new (std::nothrow) qcap2_rcbuffer_queue_priv_t();
    if (pThis) {
        pThis->event = NULL;
        pThis->maxBuffers = 0;
        pThis->running = false;
    }
    return (qcap2_rcbuffer_queue_t*)pThis;
}

void qcap2_rcbuffer_queue_delete(qcap2_rcbuffer_queue_t* pThis) {
    if (pThis) {
        qcap2_rcbuffer_queue_priv_t* p = (qcap2_rcbuffer_queue_priv_t*)pThis;
        std::queue<qcap2_rcbuffer_t*> qPending;
        qcap2_event_t* pEvent = NULL;
        {
            std::lock_guard<std::mutex> lock(p->mtx);
            p->running = false;
            p->q.swap(qPending);
            pEvent = p->event;
        }
        p->cv_pop.notify_all();
        p->cv_push.notify_all();
        qcap2_rcbuffer_queue_notify_event(pEvent);
        qcap2_rcbuffer_queue_release_all(qPending);
        delete p;
    }
}

void qcap2_rcbuffer_queue_set_max_buffers(qcap2_rcbuffer_queue_t* pThis, int nMaxBuffers) {
    if (pThis) {
        qcap2_rcbuffer_queue_priv_t* p = (qcap2_rcbuffer_queue_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(p->mtx);
        p->maxBuffers = (nMaxBuffers > 0) ? nMaxBuffers : 0;
        p->cv_push.notify_all();
    }
}

void qcap2_rcbuffer_queue_set_event(qcap2_rcbuffer_queue_t* pThis, qcap2_event_t* pEvent) {
    if (pThis) {
        qcap2_rcbuffer_queue_priv_t* p = (qcap2_rcbuffer_queue_priv_t*)pThis;
        bool bNotify = false;
        {
            std::lock_guard<std::mutex> lock(p->mtx);
            p->event = pEvent;
            bNotify = (pEvent && !p->q.empty());
        }
        if (bNotify) {
            qcap2_rcbuffer_queue_notify_event(pEvent);
        }
    }
}

void qcap2_rcbuffer_queue_set_buffers(qcap2_rcbuffer_queue_t* pThis, qcap2_rcbuffer_t** pBuffers) {
    if (pThis && pBuffers) {
        qcap2_rcbuffer_queue_priv_t* p = (qcap2_rcbuffer_queue_priv_t*)pThis;
        qcap2_event_t* pEvent = NULL;
        bool bNotify = false;
        {
            std::lock_guard<std::mutex> lock(p->mtx);
            bool bWasEmpty = p->q.empty();
            for (int i = 0; pBuffers[i]; ++i) {
                if (p->maxBuffers > 0 && p->q.size() >= (size_t)p->maxBuffers) {
                    break;
                }
                p->q.push(pBuffers[i]);
                qcap2_rcbuffer_add_ref(pBuffers[i]);
            }
            bNotify = bWasEmpty && !p->q.empty();
            pEvent = p->event;
        }
        p->cv_pop.notify_all();
        if (bNotify) {
            qcap2_rcbuffer_queue_notify_event(pEvent);
        }
    }
}

int qcap2_rcbuffer_queue_get_buffer_count(qcap2_rcbuffer_queue_t* pThis) {
    if (pThis) {
        qcap2_rcbuffer_queue_priv_t* p = (qcap2_rcbuffer_queue_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(p->mtx);
        return (int)p->q.size();
    }
    return 0;
}

bool qcap2_rcbuffer_queue_is_full(qcap2_rcbuffer_queue_t* pThis) {
    if (pThis) {
        qcap2_rcbuffer_queue_priv_t* p = (qcap2_rcbuffer_queue_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(p->mtx);
        return (p->maxBuffers > 0 && p->q.size() >= (size_t)p->maxBuffers);
    }
    return false;
}

bool qcap2_rcbuffer_queue_is_empty(qcap2_rcbuffer_queue_t* pThis) {
    if (pThis) {
        qcap2_rcbuffer_queue_priv_t* p = (qcap2_rcbuffer_queue_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(p->mtx);
        return p->q.empty();
    }
    return true;
}

QRESULT qcap2_rcbuffer_queue_start(qcap2_rcbuffer_queue_t* pThis) {
    if (pThis) {
        qcap2_rcbuffer_queue_priv_t* p = (qcap2_rcbuffer_queue_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(p->mtx);
        p->running = true;
        p->cv_push.notify_all();
        return QCAP_RS_SUCCESSFUL;
    }
    return QCAP_RS_ERROR_INVALID_PARAMETER;
}

QRESULT qcap2_rcbuffer_queue_stop(qcap2_rcbuffer_queue_t* pThis) {
    if (pThis) {
        qcap2_rcbuffer_queue_priv_t* p = (qcap2_rcbuffer_queue_priv_t*)pThis;
        qcap2_event_t* pEvent = NULL;
        {
            std::lock_guard<std::mutex> lock(p->mtx);
            p->running = false;
            pEvent = p->event;
        }
        p->cv_pop.notify_all();
        p->cv_push.notify_all();
        qcap2_rcbuffer_queue_notify_event(pEvent);
        return QCAP_RS_SUCCESSFUL;
    }
    return QCAP_RS_ERROR_INVALID_PARAMETER;
}

QRESULT qcap2_rcbuffer_queue_push(qcap2_rcbuffer_queue_t* pThis, qcap2_rcbuffer_t* pRCBuffer) {
    if (pThis && pRCBuffer) {
        qcap2_rcbuffer_queue_priv_t* p = (qcap2_rcbuffer_queue_priv_t*)pThis;
        qcap2_event_t* pEvent = NULL;
        bool bNotify = false;
        {
            std::unique_lock<std::mutex> lock(p->mtx);

            p->cv_push.wait(lock, [p]{
                return !p->running || (p->maxBuffers <= 0 || p->q.size() < (size_t)p->maxBuffers);
            });

            if (!p->running) return QCAP_RS_ERROR_GENERAL;

            bNotify = p->q.empty();
            p->q.push(pRCBuffer);
            qcap2_rcbuffer_add_ref(pRCBuffer);
            pEvent = p->event;
        }

        p->cv_pop.notify_one();
        if (bNotify) {
            qcap2_rcbuffer_queue_notify_event(pEvent);
        }
        return QCAP_RS_SUCCESSFUL;
    }
    return QCAP_RS_ERROR_INVALID_PARAMETER;
}

QRESULT qcap2_rcbuffer_queue_pop(qcap2_rcbuffer_queue_t* pThis, qcap2_rcbuffer_t** ppRCBuffer) {
    if (ppRCBuffer) {
        *ppRCBuffer = NULL;
    }

    if (pThis && ppRCBuffer) {
        qcap2_rcbuffer_queue_priv_t* p = (qcap2_rcbuffer_queue_priv_t*)pThis;
        qcap2_event_t* pEvent = NULL;
        bool bNotifyAgain = false;
        {
            std::unique_lock<std::mutex> lock(p->mtx);

            p->cv_pop.wait(lock, [p]{
                return !p->running || !p->q.empty();
            });

            if (!p->running && p->q.empty()) return QCAP_RS_ERROR_GENERAL;

            *ppRCBuffer = p->q.front();
            p->q.pop();
            bNotifyAgain = !p->q.empty();
            pEvent = p->event;
        }

        p->cv_push.notify_one();
        if (bNotifyAgain) {
            qcap2_rcbuffer_queue_notify_event(pEvent);
        }
        return QCAP_RS_SUCCESSFUL;
    }
    return QCAP_RS_ERROR_INVALID_PARAMETER;
}

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif

// --- qcap2_timer_t ---
typedef struct qcap2_timer_priv_t {
#ifdef __linux__
    int tfd;
    uint64_t interval_ms;
#else
    uint64_t interval_ms;
    std::mutex* mtx;
    std::condition_variable* cv;
    bool running;
    bool triggered;
    std::thread* timer_thread;
#endif
} qcap2_timer_priv_t;

#ifndef __linux__
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
#endif

qcap2_timer_t* qcap2_timer_new() {
    qcap2_timer_priv_t* pThis = new qcap2_timer_priv_t;
#ifdef __linux__
    pThis->tfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    pThis->interval_ms = 0;
#else
    pThis->interval_ms = 0;
    pThis->mtx = new std::mutex();
    pThis->cv = new std::condition_variable();
    pThis->running = false;
    pThis->triggered = false;
    pThis->timer_thread = nullptr;
#endif
    return (qcap2_timer_t*)pThis;
}

void qcap2_timer_delete(qcap2_timer_t* pThis) {
    if (pThis) {
        qcap2_timer_priv_t* p = (qcap2_timer_priv_t*)pThis;
#ifdef __linux__
        if (p->tfd >= 0) {
            close(p->tfd);
        }
        delete p;
#else
        qcap2_timer_stop(pThis); // Ensure it's stopped
        delete p->cv;
        delete p->mtx;
        delete p;
#endif
    }
}

void qcap2_timer_set_interval(qcap2_timer_t* pThis, uint64_t nInterval) {
    if (pThis) {
        qcap2_timer_priv_t* p = (qcap2_timer_priv_t*)pThis;
#ifdef __linux__
        p->interval_ms = nInterval;
#else
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->interval_ms = nInterval;
#endif
    }
}

QRESULT qcap2_timer_start(qcap2_timer_t* pThis) {
    if (pThis) {
        qcap2_timer_priv_t* p = (qcap2_timer_priv_t*)pThis;
#ifdef __linux__
        if (p->tfd < 0) return QCAP_RS_ERROR_GENERAL;

        struct itimerspec new_value;
        new_value.it_interval.tv_sec = p->interval_ms / 1000;
        new_value.it_interval.tv_nsec = (p->interval_ms % 1000) * 1000000;
        new_value.it_value.tv_sec = p->interval_ms / 1000;
        new_value.it_value.tv_nsec = (p->interval_ms % 1000) * 1000000;

        if (timerfd_settime(p->tfd, 0, &new_value, NULL) == 0) {
            return QCAP_RS_SUCCESSFUL;
        }
#else
        std::lock_guard<std::mutex> lock(*(p->mtx));
        if (!p->running) {
            p->running = true;
            p->triggered = false;
            p->timer_thread = new std::thread(timer_thread_func, p);
        }
        return QCAP_RS_SUCCESSFUL;
#endif
    }
    return QCAP_RS_ERROR_GENERAL;
}

QRESULT qcap2_timer_stop(qcap2_timer_t* pThis) {
    if (pThis) {
        qcap2_timer_priv_t* p = (qcap2_timer_priv_t*)pThis;
#ifdef __linux__
        if (p->tfd < 0) return QCAP_RS_ERROR_GENERAL;

        struct itimerspec new_value;
        memset(&new_value, 0, sizeof(new_value));

        if (timerfd_settime(p->tfd, 0, &new_value, NULL) == 0) {
            return QCAP_RS_SUCCESSFUL;
        }
#else
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
#endif
    }
    return QCAP_RS_ERROR_GENERAL;
}

QRESULT qcap2_timer_get_native_handle(qcap2_timer_t* pThis, uintptr_t* pHandle) {
    if (pThis && pHandle) {
        qcap2_timer_priv_t* p = (qcap2_timer_priv_t*)pThis;
#ifdef __linux__
        *pHandle = (uintptr_t)p->tfd;
#else
        *pHandle = (uintptr_t)pThis;
#endif
        return QCAP_RS_SUCCESSFUL;
    }
    return QCAP_RS_ERROR_GENERAL;
}

QRESULT qcap2_timer_wait(qcap2_timer_t* pThis, uint64_t* pExpirations) {
    if (pThis) {
        qcap2_timer_priv_t* p = (qcap2_timer_priv_t*)pThis;
#ifdef __linux__
        if (p->tfd < 0) return QCAP_RS_ERROR_GENERAL;

        struct pollfd pfd = { p->tfd, POLLIN, 0 };
        while (true) {
            int ret = poll(&pfd, 1, -1);
            if (ret > 0) {
                uint64_t exp = 0;
                ssize_t n = read(p->tfd, &exp, sizeof(exp));
                if (n == sizeof(exp)) {
                    if (pExpirations) {
                        *pExpirations = exp;
                    }
                    return QCAP_RS_SUCCESSFUL;
                } else if (n < 0 && errno == EINTR) {
                    continue;
                } else {
                    return QCAP_RS_ERROR_GENERAL;
                }
            } else if (ret < 0) {
                if (errno == EINTR) continue;
                return QCAP_RS_ERROR_GENERAL;
            }
        }
#else
        std::unique_lock<std::mutex> lock(*(p->mtx));
        p->cv->wait(lock, [p]{ return p->triggered || !p->running; });

        if (!p->running && !p->triggered) return QCAP_RS_ERROR_GENERAL;

        p->triggered = false;
        if (pExpirations) *pExpirations = 1;
        return QCAP_RS_SUCCESSFUL;
#endif
    }
    return QCAP_RS_ERROR_GENERAL;
}

QRESULT qcap2_timer_next(qcap2_timer_t* pThis, uint64_t nDuration) {
    if (pThis) {
        qcap2_timer_priv_t* p = (qcap2_timer_priv_t*)pThis;
#ifdef __linux__
        if (p->tfd < 0) return QCAP_RS_ERROR_GENERAL;

        struct itimerspec new_value;
        new_value.it_interval.tv_sec = 0;
        new_value.it_interval.tv_nsec = 0;
        new_value.it_value.tv_sec = nDuration / 1000;
        new_value.it_value.tv_nsec = (nDuration % 1000) * 1000000;

        if (timerfd_settime(p->tfd, 0, &new_value, NULL) == 0) {
            return QCAP_RS_SUCCESSFUL;
        }
#else
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->interval_ms = nDuration;
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

class qcap2_window_backend_t {
public:
    virtual ~qcap2_window_backend_t() = default;
    virtual QRESULT start() = 0;
    virtual QRESULT stop() = 0;
    virtual QRESULT handle_events() = 0;
};

typedef struct qcap2_window_priv_t {
    int backendType;
    int rect[4];
    bool fullScreen;
    qcap2_window_backend_t* backend;

    qcap2_window_priv_t() {
        backendType = 0;
        rect[0] = rect[1] = rect[2] = rect[3] = 0;
        fullScreen = false;
        backend = nullptr;
    }
    ~qcap2_window_priv_t() {
        if (backend) {
            delete backend;
        }
    }
} qcap2_window_priv_t;

class qcap2_fake_window_backend_t : public qcap2_window_backend_t {
private:
    qcap2_window_priv_t* m_pOwner;
public:
    qcap2_fake_window_backend_t(qcap2_window_priv_t* pOwner) : m_pOwner(pOwner) {}
    QRESULT start() override { return QCAP_RS_SUCCESSFUL; }
    QRESULT stop() override { return QCAP_RS_SUCCESSFUL; }
    QRESULT handle_events() override { return QCAP_RS_SUCCESSFUL; }
};

class qcap2_x11_window_backend_t : public qcap2_window_backend_t {
private:
    qcap2_window_priv_t* m_pOwner;
public:
    qcap2_x11_window_backend_t(qcap2_window_priv_t* pOwner) : m_pOwner(pOwner) {}
    QRESULT start() override { return QCAP_RS_SUCCESSFUL; }
    QRESULT stop() override { return QCAP_RS_SUCCESSFUL; }
    QRESULT handle_events() override { return QCAP_RS_SUCCESSFUL; }
};

qcap2_window_t* qcap2_window_new() {
    qcap2_window_priv_t* pThis = new (std::nothrow) qcap2_window_priv_t();
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
    if (!pThis) return QCAP_RS_ERROR_GENERAL;
    qcap2_window_priv_t* p = (qcap2_window_priv_t*)pThis;

    if (p->backend) return QCAP_RS_SUCCESSFUL;

    if (p->backendType == 1) {
        p->backend = new (std::nothrow) qcap2_x11_window_backend_t(p);
    } else {
        p->backend = new (std::nothrow) qcap2_fake_window_backend_t(p);
    }

    if (!p->backend) return QCAP_RS_ERROR_GENERAL;

    return p->backend->start();
}

QRESULT qcap2_window_stop(qcap2_window_t* pThis) {
    if (!pThis) return QCAP_RS_ERROR_GENERAL;
    qcap2_window_priv_t* p = (qcap2_window_priv_t*)pThis;

    if (p->backend) {
        p->backend->stop();
        delete p->backend;
        p->backend = nullptr;
    }
    return QCAP_RS_SUCCESSFUL;
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
    if (!pThis) return QCAP_RS_ERROR_GENERAL;
    qcap2_window_priv_t* p = (qcap2_window_priv_t*)pThis;
    if (p->backend) {
        return p->backend->handle_events();
    }
    return QCAP_RS_SUCCESSFUL;
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
