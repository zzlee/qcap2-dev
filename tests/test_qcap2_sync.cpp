#include "qcap2.sync.h"
#include <stdio.h>
#include <assert.h>
#include <thread>
#include <chrono>
#include <atomic>

void test_qcap2_benaphore_lock_t() {
    qcap2_benaphore_lock_t* lock = qcap2_benaphore_lock_new();
    assert(lock != NULL);

    int counter = 0;
    auto func = [&]() {
        qcap2_benaphore_lock_lock(lock);
        counter++;
        qcap2_benaphore_lock_unlock(lock);
    };

    std::thread t1(func);
    std::thread t2(func);

    t1.join();
    t2.join();

    assert(counter == 2);
    qcap2_benaphore_lock_delete(lock);
}

void test_qcap2_block_lock_t() {
    qcap2_block_lock_t* lock = qcap2_block_lock_new();
    assert(lock != NULL);

    qcap2_block_lock_grant(lock, false);

    std::atomic<bool> entered(false);
    std::thread t1([&]() {
        qcap2_block_lock_enter(lock);
        entered = true;
        qcap2_block_lock_leave(lock);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    assert(!entered.load()); // Should be blocked

    qcap2_block_lock_grant(lock, true);
    t1.join();
    assert(entered.load()); // Should have entered after grant

    qcap2_block_lock_delete(lock);
}

QRETURN dummy_event_handler(PVOID data) {
    if (data) {
        int* counter = (int*)data;
        (*counter)++;
    }
    return QCAP_RT_OK;
}

void test_qcap2_event_t() {
    qcap2_event_t* ev = qcap2_event_new();
    assert(ev != NULL);

    assert(qcap2_event_start(ev) == QCAP_RS_SUCCESSFUL);

    std::atomic<bool> notified(false);
    std::thread t1([&]() {
        qcap2_event_wait(ev);
        notified = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    assert(!notified.load());

    assert(qcap2_event_notify(ev) == QCAP_RS_SUCCESSFUL);
    t1.join();
    assert(notified.load());

    assert(qcap2_event_stop(ev) == QCAP_RS_SUCCESSFUL);
    qcap2_event_delete(ev);
}

void test_qcap2_event_handlers_t() {
    qcap2_event_handlers_t* handlers = qcap2_event_handlers_new();
    assert(handlers != NULL);

    int counter = 0;
    assert(qcap2_event_handlers_add_handler(handlers, 1, dummy_event_handler, &counter) == QCAP_RS_SUCCESSFUL);
    assert(qcap2_event_handlers_invoke(handlers, NULL, NULL) == QCAP_RS_SUCCESSFUL);

    assert(counter == 1);

    assert(qcap2_event_handlers_remove_handler(handlers, 1) == QCAP_RS_SUCCESSFUL);
    qcap2_event_handlers_delete(handlers);
}

void test_qcap2_rcbuffer_queue_t() {
    qcap2_rcbuffer_queue_t* queue = qcap2_rcbuffer_queue_new();
    assert(queue != NULL);

    qcap2_rcbuffer_queue_set_max_buffers(queue, 5);
    assert(qcap2_rcbuffer_queue_start(queue) == QCAP_RS_SUCCESSFUL);

    qcap2_rcbuffer_t* dummy_buf = (qcap2_rcbuffer_t*)0x1234;

    std::thread t1([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        qcap2_rcbuffer_queue_push(queue, dummy_buf);
    });

    qcap2_rcbuffer_t* pop_buf = NULL;
    assert(qcap2_rcbuffer_queue_pop(queue, &pop_buf) == QCAP_RS_SUCCESSFUL);
    assert(pop_buf == dummy_buf);

    t1.join();

    assert(qcap2_rcbuffer_queue_stop(queue) == QCAP_RS_SUCCESSFUL);
    qcap2_rcbuffer_queue_delete(queue);
}

void test_qcap2_timer_t() {
    qcap2_timer_t* timer = qcap2_timer_new();
    assert(timer != NULL);

    qcap2_timer_set_interval(timer, 50);
    assert(qcap2_timer_start(timer) == QCAP_RS_SUCCESSFUL);

    auto start = std::chrono::steady_clock::now();
    uint64_t exp;
    assert(qcap2_timer_wait(timer, &exp) == QCAP_RS_SUCCESSFUL);
    auto end = std::chrono::steady_clock::now();

    auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    assert(diff >= 40); // Allow some scheduling tolerance

    assert(qcap2_timer_stop(timer) == QCAP_RS_SUCCESSFUL);
    qcap2_timer_delete(timer);
}

void test_qcap2_window_t() {
    qcap2_window_t* win = qcap2_window_new();
    assert(win != NULL);

    qcap2_window_set_backend_type(win, 1);
    qcap2_window_set_rect(win, 0, 0, 640, 480);

    assert(qcap2_window_start(win) == QCAP_RS_SUCCESSFUL);
    assert(qcap2_window_handle_events(win) == QCAP_RS_SUCCESSFUL);
    assert(qcap2_window_stop(win) == QCAP_RS_SUCCESSFUL);

    qcap2_window_delete(win);
}

void test_qcap2_binder_t() {
    qcap2_binder_t* binder = qcap2_binder_new();
    assert(binder != NULL);

    assert(qcap2_binder_start(binder) == QCAP_RS_SUCCESSFUL);

    qcap2_video_source_t* dummy_src = NULL;
    qcap2_video_scaler_t* dummy_sink = NULL;
    intptr_t cookie = qcap2_binder_vsrc_vsca(binder, dummy_src, dummy_sink);
    assert(cookie > 0);

    assert(qcap2_binder_unlink(binder, cookie) == QCAP_RS_SUCCESSFUL);

    assert(qcap2_binder_stop(binder) == QCAP_RS_SUCCESSFUL);

    qcap2_binder_delete(binder);
}

int main() {
    test_qcap2_benaphore_lock_t();
    test_qcap2_block_lock_t();
    test_qcap2_event_t();
    test_qcap2_event_handlers_t();
    test_qcap2_rcbuffer_queue_t();
    test_qcap2_timer_t();
    test_qcap2_window_t();
    test_qcap2_binder_t();
    printf("Sync test basic locks passed!\n");
    return 0;
}
