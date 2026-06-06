#ifndef __QCAP2_SYNC_H__
#define __QCAP2_SYNC_H__

#include "qcap2.types.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

// qcap2_benaphore_lock_t
qcap2_benaphore_lock_t* qcap2_benaphore_lock_new();
void qcap2_benaphore_lock_delete(qcap2_benaphore_lock_t* pThis);
void qcap2_benaphore_lock_lock(qcap2_benaphore_lock_t* pThis);
void qcap2_benaphore_lock_unlock(qcap2_benaphore_lock_t* pThis);

// qcap2_event_t
qcap2_event_t* qcap2_event_new();
void qcap2_event_delete(qcap2_event_t* pThis);
void qcap2_event_set_initial(qcap2_event_t* pThis, int nInitial);
QRESULT qcap2_event_start(qcap2_event_t* pThis);
QRESULT qcap2_event_stop(qcap2_event_t* pThis);
QRESULT qcap2_event_get_native_handle(qcap2_event_t* pThis, uintptr_t* pHandle);
QRESULT qcap2_event_notify(qcap2_event_t* pThis);
QRESULT qcap2_event_wait(qcap2_event_t* pThis);
QRESULT qcap2_event_wait_count(qcap2_event_t* pThis, uint64_t* pCount);
QRESULT qcap2_event_read(qcap2_event_t* pThis, uint64_t* pCount);

// qcap2_event_handlers_t
qcap2_event_handlers_t* qcap2_event_handlers_new();
void qcap2_event_handlers_delete(qcap2_event_handlers_t* pThis);
QRESULT qcap2_event_handlers_start(qcap2_event_handlers_t* pThis);
QRESULT qcap2_event_handlers_stop(qcap2_event_handlers_t* pThis);
QRESULT qcap2_event_handlers_add_handler(qcap2_event_handlers_t* pThis, uintptr_t nHandle, qcap2_on_event_t pOnEvent, PVOID pUserData);
QRESULT qcap2_event_handlers_remove_handler(qcap2_event_handlers_t* pThis, uintptr_t nHandle);
QRESULT qcap2_event_handlers_invoke(qcap2_event_handlers_t* pThis, qcap2_on_event_t pOnEvent, PVOID pUserData);

// qcap2_rcbuffer_queue_t
qcap2_rcbuffer_queue_t* qcap2_rcbuffer_queue_new();
void qcap2_rcbuffer_queue_delete(qcap2_rcbuffer_queue_t* pThis);
void qcap2_rcbuffer_queue_set_max_buffers(qcap2_rcbuffer_queue_t* pThis, int nMaxBuffers);
void qcap2_rcbuffer_queue_set_event(qcap2_rcbuffer_queue_t* pThis, qcap2_event_t* pEvent);
void qcap2_rcbuffer_queue_set_buffers(qcap2_rcbuffer_queue_t* pThis, qcap2_rcbuffer_t** pBuffers);
int qcap2_rcbuffer_queue_get_buffer_count(qcap2_rcbuffer_queue_t* pThis);
bool qcap2_rcbuffer_queue_is_full(qcap2_rcbuffer_queue_t* pThis);
bool qcap2_rcbuffer_queue_is_empty(qcap2_rcbuffer_queue_t* pThis);
QRESULT qcap2_rcbuffer_queue_start(qcap2_rcbuffer_queue_t* pThis);
QRESULT qcap2_rcbuffer_queue_stop(qcap2_rcbuffer_queue_t* pThis);
QRESULT qcap2_rcbuffer_queue_push(qcap2_rcbuffer_queue_t* pThis, qcap2_rcbuffer_t* pRCBuffer);
QRESULT qcap2_rcbuffer_queue_pop(qcap2_rcbuffer_queue_t* pThis, qcap2_rcbuffer_t** ppRCBuffer);

// qcap2_timer_t
qcap2_timer_t* qcap2_timer_new();
void qcap2_timer_delete(qcap2_timer_t* pThis);
void qcap2_timer_set_interval(qcap2_timer_t* pThis, uint64_t nInterval);
QRESULT qcap2_timer_start(qcap2_timer_t* pThis);
QRESULT qcap2_timer_stop(qcap2_timer_t* pThis);
QRESULT qcap2_timer_get_native_handle(qcap2_timer_t* pThis, uintptr_t* pHandle);
QRESULT qcap2_timer_wait(qcap2_timer_t* pThis, uint64_t* pExpirations);
QRESULT qcap2_timer_next(qcap2_timer_t* pThis, uint64_t nDuration);

// qcap2_window_t
qcap2_window_t* qcap2_window_new();
void qcap2_window_delete(qcap2_window_t* pThis);
void qcap2_window_set_backend_type(qcap2_window_t* pThis, int nType);
void qcap2_window_set_rect(qcap2_window_t* pThis, int x, int y, int w, int h);
void qcap2_window_set_full_screen(qcap2_window_t* pThis, bool bFullScreen);
QRESULT qcap2_window_start(qcap2_window_t* pThis);
QRESULT qcap2_window_stop(qcap2_window_t* pThis);
QRESULT qcap2_window_get_hwnd(qcap2_window_t* pThis, HWND* pHwnd);
QRESULT qcap2_window_get_native_handle(qcap2_window_t* pThis, uintptr_t* pHandle);
QRESULT qcap2_window_handle_events(qcap2_window_t* pThis);

// qcap2_block_lock_t
qcap2_block_lock_t* qcap2_block_lock_new();
void qcap2_block_lock_delete(qcap2_block_lock_t* pThis);
void qcap2_block_lock_grant(qcap2_block_lock_t* pThis, bool bGranted);
bool qcap2_block_lock_enter(qcap2_block_lock_t* pThis);
void qcap2_block_lock_leave(qcap2_block_lock_t* pThis);

// qcap2_binder_t
qcap2_binder_t* qcap2_binder_new();
void qcap2_binder_delete(qcap2_binder_t* pThis);
QRESULT qcap2_binder_start(qcap2_binder_t* pThis);
QRESULT qcap2_binder_stop(qcap2_binder_t* pThis);
intptr_t qcap2_binder_vsrc_vsca(qcap2_binder_t* pThis, qcap2_video_source_t* pSrc, qcap2_video_scaler_t* pSink);
QRESULT qcap2_binder_unlink(qcap2_binder_t* pThis, intptr_t cookie);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif // __QCAP2_SYNC_H__
