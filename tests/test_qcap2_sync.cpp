#include "qcap2.sync.h"
#include "qcap2.buffer.h"
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

#ifdef __linux__
#include <poll.h>
#endif

void test_qcap2_event_t() {
    qcap2_event_t* ev = qcap2_event_new();
    assert(ev != NULL);

    uintptr_t handle;
    assert(qcap2_event_get_native_handle(ev, &handle) == QCAP_RS_SUCCESSFUL);
#ifdef __linux__
    assert(handle > 0);
#endif

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

#ifdef __linux__
    // Test poll functionality
    qcap2_event_notify(ev);
    struct pollfd pfd = { (int)handle, POLLIN, 0 };
    int ret = poll(&pfd, 1, 0);
    assert(ret == 1);
    assert(pfd.revents & POLLIN);
    qcap2_event_wait(ev);
#endif

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

struct TestQueueVideoFrame {
    int index;
    int prepared_value;
    int consumed_count;
    int free_count;
    qcap2_av_frame_t frame;

    TestQueueVideoFrame() : index(0), prepared_value(-1), consumed_count(0), free_count(0) {
        qcap2_av_frame_init(&frame);
    }

    static void on_free_resource(PVOID pData) {
        TestQueueVideoFrame* pThis = qcap2_container_of(pData, TestQueueVideoFrame, frame);
        pThis->free_count++;
    }
};

void test_qcap2_rcbuffer_queue_provider_consumer_scheme_t() {
    const int kBufferCount = 3;
    const int kIterations = 12;

    qcap2_rcbuffer_queue_t* free_queue = qcap2_rcbuffer_queue_new();
    qcap2_rcbuffer_queue_t* source_queue = qcap2_rcbuffer_queue_new();
    qcap2_event_t* free_event = qcap2_event_new();
    qcap2_event_t* source_event = qcap2_event_new();
    assert(free_queue != NULL);
    assert(source_queue != NULL);
    assert(free_event != NULL);
    assert(source_event != NULL);

    TestQueueVideoFrame frames[kBufferCount];
    qcap2_rcbuffer_t* rcbufs[kBufferCount + 1];
    for (int i = 0; i < kBufferCount; ++i) {
        frames[i].index = i;
        rcbufs[i] = qcap2_rcbuffer_new(&frames[i].frame, TestQueueVideoFrame::on_free_resource);
        assert(rcbufs[i] != NULL);
        assert(qcap2_rcbuffer_get_data(rcbufs[i]) == &frames[i].frame);
    }
    rcbufs[kBufferCount] = NULL;

    qcap2_rcbuffer_queue_set_event(free_queue, free_event);
    qcap2_rcbuffer_queue_set_buffers(free_queue, rcbufs);
    assert(qcap2_rcbuffer_queue_get_buffer_count(free_queue) == kBufferCount);
    assert(qcap2_rcbuffer_queue_start(free_queue) == QCAP_RS_SUCCESSFUL);

    qcap2_rcbuffer_queue_set_event(source_queue, source_event);
    assert(qcap2_rcbuffer_queue_start(source_queue) == QCAP_RS_SUCCESSFUL);

    std::thread provider([&]() {
        for (int i = 0; i < kIterations; ++i) {
            assert(qcap2_event_wait(free_event) == QCAP_RS_SUCCESSFUL);

            qcap2_rcbuffer_t* rcbuf = NULL;
            assert(qcap2_rcbuffer_queue_pop(free_queue, &rcbuf) == QCAP_RS_SUCCESSFUL);
            assert(rcbuf != NULL);

            qcap2_av_frame_t* frame = (qcap2_av_frame_t*)qcap2_rcbuffer_get_data(rcbuf);
            TestQueueVideoFrame* owner = qcap2_container_of(frame, TestQueueVideoFrame, frame);
            owner->prepared_value = i;
            qcap2_av_frame_set_pts(frame, i);

            assert(qcap2_rcbuffer_queue_push(source_queue, rcbuf) == QCAP_RS_SUCCESSFUL);
        }
    });

    std::thread consumer([&]() {
        for (int i = 0; i < kIterations; ++i) {
            assert(qcap2_event_wait(source_event) == QCAP_RS_SUCCESSFUL);

            qcap2_rcbuffer_t* rcbuf = NULL;
            assert(qcap2_rcbuffer_queue_pop(source_queue, &rcbuf) == QCAP_RS_SUCCESSFUL);
            assert(rcbuf != NULL);

            qcap2_av_frame_t* frame = (qcap2_av_frame_t*)qcap2_rcbuffer_get_data(rcbuf);
            TestQueueVideoFrame* owner = qcap2_container_of(frame, TestQueueVideoFrame, frame);
            int64_t pts = -1;
            qcap2_av_frame_get_pts(frame, &pts);
            assert(owner->prepared_value == i);
            assert(pts == i);
            owner->consumed_count++;

            assert(qcap2_rcbuffer_queue_push(free_queue, rcbuf) == QCAP_RS_SUCCESSFUL);
        }
    });

    provider.join();
    consumer.join();

    int consumed_count = 0;
    for (int i = 0; i < kBufferCount; ++i) {
        assert(frames[i].free_count == 0);
        consumed_count += frames[i].consumed_count;
    }
    assert(consumed_count == kIterations);
    assert(qcap2_rcbuffer_queue_get_buffer_count(free_queue) == kBufferCount);
    assert(qcap2_rcbuffer_queue_is_empty(source_queue));

    assert(qcap2_rcbuffer_queue_stop(source_queue) == QCAP_RS_SUCCESSFUL);
    assert(qcap2_rcbuffer_queue_stop(free_queue) == QCAP_RS_SUCCESSFUL);
    qcap2_rcbuffer_queue_delete(source_queue);
    qcap2_rcbuffer_queue_delete(free_queue);
    qcap2_event_delete(source_event);
    qcap2_event_delete(free_event);

    for (int i = 0; i < kBufferCount; ++i) {
        assert(frames[i].free_count == 1);
    }
}

void test_qcap2_rcbuffer_queue_event_t() {
    qcap2_rcbuffer_queue_t* queue = qcap2_rcbuffer_queue_new();
    qcap2_event_t* event = qcap2_event_new();
    assert(queue != NULL);
    assert(event != NULL);

    qcap2_rcbuffer_queue_set_event(queue, event);
    assert(qcap2_rcbuffer_queue_start(queue) == QCAP_RS_SUCCESSFUL);

    qcap2_rcbuffer_t* dummy_buf1 = (qcap2_rcbuffer_t*)0x1234;
    qcap2_rcbuffer_t* dummy_buf2 = (qcap2_rcbuffer_t*)0x5678;

    assert(qcap2_rcbuffer_queue_push(queue, dummy_buf1) == QCAP_RS_SUCCESSFUL);
    assert(qcap2_rcbuffer_queue_push(queue, dummy_buf2) == QCAP_RS_SUCCESSFUL);

    qcap2_rcbuffer_t* pop_buf = NULL;
    assert(qcap2_event_wait(event) == QCAP_RS_SUCCESSFUL);
    assert(qcap2_rcbuffer_queue_pop(queue, &pop_buf) == QCAP_RS_SUCCESSFUL);
    assert(pop_buf == dummy_buf1);

    pop_buf = NULL;
    assert(qcap2_event_wait(event) == QCAP_RS_SUCCESSFUL);
    assert(qcap2_rcbuffer_queue_pop(queue, &pop_buf) == QCAP_RS_SUCCESSFUL);
    assert(pop_buf == dummy_buf2);

    assert(qcap2_rcbuffer_queue_is_empty(queue));
    assert(qcap2_rcbuffer_queue_stop(queue) == QCAP_RS_SUCCESSFUL);
    qcap2_rcbuffer_queue_delete(queue);
    qcap2_event_delete(event);
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
    test_qcap2_rcbuffer_queue_provider_consumer_scheme_t();
    test_qcap2_rcbuffer_queue_event_t();
    test_qcap2_timer_t();
    test_qcap2_window_t();
    test_qcap2_binder_t();
    printf("Sync test basic locks passed!\n");
    return 0;
}
