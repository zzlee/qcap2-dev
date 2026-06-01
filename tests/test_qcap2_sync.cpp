#include "qcap2.sync.h"
#include "qcap2.buffer.h"
#include "qcap2.devices.h"
#include "qcap2.formats.h"
#include "qcap2.v4l2.h"
#include <stdio.h>
#include <assert.h>
#include <thread>
#include <chrono>
#include <atomic>
#include <string.h>
#ifdef __linux__
#include <unistd.h>
#endif

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

struct TestPipeContext {
    int fd;
    int* counter;
};

static QRETURN pipe_event_handler(PVOID data) {
    if (data) {
        TestPipeContext* ctx = (TestPipeContext*)data;
        char buf[128];
        int n = ::read(ctx->fd, buf, sizeof(buf));
        (void)n;
        (*ctx->counter)++;
    }
    return QCAP_RT_OK;
}

static std::thread::id main_thread_id;
static std::thread::id callback_thread_id;

static QRETURN thread_id_check_callback(PVOID data) {
    if (data) {
        int* counter = (int*)data;
        (*counter)++;
    }
    callback_thread_id = std::this_thread::get_id();
    return QCAP_RT_OK;
}

void test_qcap2_event_handlers_t() {
    qcap2_event_handlers_t* handlers = qcap2_event_handlers_new();
    assert(handlers != NULL);

    main_thread_id = std::this_thread::get_id();
    callback_thread_id = std::thread::id(); // clear

    // Start handlers first so the background thread is running
    assert(qcap2_event_handlers_start(handlers) == QCAP_RS_SUCCESSFUL);

    int invoke_counter = 0;
    assert(qcap2_event_handlers_invoke(handlers, thread_id_check_callback, &invoke_counter) == QCAP_RS_SUCCESSFUL);
    
    // It is asynchronous, so sleep a bit
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(invoke_counter == 1);
    assert(callback_thread_id != main_thread_id); // Must have run in the background thread context!

#ifdef __linux__
    int pipefds[2];
    assert(pipe(pipefds) == 0);

    int thread_counter = 0;
    TestPipeContext ctx = { pipefds[0], &thread_counter };
    // Add the read-end to the event handler list
    assert(qcap2_event_handlers_add_handler(handlers, pipefds[0], pipe_event_handler, &ctx) == QCAP_RS_SUCCESSFUL);

    // Write to the write-end of pipe to trigger callback
    char val = 'x';
    assert(write(pipefds[1], &val, 1) == 1);

    // Wait for callback to execute in monitoring thread
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(thread_counter == 1);

    // Remove handler
    assert(qcap2_event_handlers_remove_handler(handlers, pipefds[0]) == QCAP_RS_SUCCESSFUL);

    // Write again to make sure callback is NOT invoked after removing
    assert(write(pipefds[1], &val, 1) == 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(thread_counter == 1); // Should still be 1

    close(pipefds[0]);
    close(pipefds[1]);
#endif

    // Stop handlers
    assert(qcap2_event_handlers_stop(handlers) == QCAP_RS_SUCCESSFUL);
    qcap2_event_handlers_delete(handlers);
}

void test_qcap2_rcbuffer_queue_t() {
    qcap2_rcbuffer_queue_t* queue = qcap2_rcbuffer_queue_new();
    assert(queue != NULL);

    qcap2_rcbuffer_queue_set_max_buffers(queue, 5);
    assert(qcap2_rcbuffer_queue_start(queue) == QCAP_RS_SUCCESSFUL);

    qcap2_rcbuffer_t* buf = qcap2_rcbuffer_new(NULL, NULL);
    assert(buf != NULL);

    std::thread t1([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        qcap2_rcbuffer_queue_push(queue, buf);
        qcap2_rcbuffer_release(buf);
    });

    qcap2_rcbuffer_t* pop_buf = NULL;
    assert(qcap2_rcbuffer_queue_pop(queue, &pop_buf) == QCAP_RS_SUCCESSFUL);
    assert(pop_buf == buf);
    qcap2_rcbuffer_release(pop_buf);

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
    for (int i = 0; i < kBufferCount; ++i) {
        qcap2_rcbuffer_release(rcbufs[i]);
    }
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
            qcap2_rcbuffer_release(rcbuf);
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
            qcap2_rcbuffer_release(rcbuf);
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

    qcap2_rcbuffer_t* buf1 = qcap2_rcbuffer_new(NULL, NULL);
    qcap2_rcbuffer_t* buf2 = qcap2_rcbuffer_new(NULL, NULL);
    assert(buf1 != NULL && buf2 != NULL);

    assert(qcap2_rcbuffer_queue_push(queue, buf1) == QCAP_RS_SUCCESSFUL);
    qcap2_rcbuffer_release(buf1);
    assert(qcap2_rcbuffer_queue_push(queue, buf2) == QCAP_RS_SUCCESSFUL);
    qcap2_rcbuffer_release(buf2);

    qcap2_rcbuffer_t* pop_buf = NULL;
    assert(qcap2_event_wait(event) == QCAP_RS_SUCCESSFUL);
    assert(qcap2_rcbuffer_queue_pop(queue, &pop_buf) == QCAP_RS_SUCCESSFUL);
    assert(pop_buf == buf1);
    qcap2_rcbuffer_release(pop_buf);

    pop_buf = NULL;
    assert(qcap2_event_wait(event) == QCAP_RS_SUCCESSFUL);
    assert(qcap2_rcbuffer_queue_pop(queue, &pop_buf) == QCAP_RS_SUCCESSFUL);
    assert(pop_buf == buf2);
    qcap2_rcbuffer_release(pop_buf);

    assert(qcap2_rcbuffer_queue_is_empty(queue));
    assert(qcap2_rcbuffer_queue_stop(queue) == QCAP_RS_SUCCESSFUL);
    qcap2_rcbuffer_queue_delete(queue);
    qcap2_event_delete(event);
}

void test_qcap2_timer_t() {
    qcap2_timer_t* timer = qcap2_timer_new();
    assert(timer != NULL);

    uintptr_t handle = 0;
    assert(qcap2_timer_get_native_handle(timer, &handle) == QCAP_RS_SUCCESSFUL);
#ifdef __linux__
    assert((int)handle > 0);
#endif

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

void test_qcap2_video_source_t() {
    qcap2_video_source_t* vsrc = qcap2_video_source_new();
    assert(vsrc != NULL);

    qcap2_video_source_set_backend_type(vsrc, QCAP2_VIDEO_SOURCE_BACKEND_TYPE_TPG);
    qcap2_video_source_set_device_index(vsrc, 2);

    qcap2_video_format_t* fmt = qcap2_video_format_new();
    assert(fmt != NULL);
    qcap2_video_format_set_property(fmt, QCAP_COLORSPACE_TYPE_YUY2, 1920, 1080, FALSE, 30.0);
    qcap2_video_source_set_video_format(vsrc, fmt);

    qcap2_video_format_t* fmt_out = qcap2_video_format_new();
    assert(fmt_out != NULL);
    qcap2_video_source_get_video_format(vsrc, fmt_out);

    ULONG color_space = 0, width = 0, height = 0;
    BOOL interleaved = FALSE;
    double fps = 0.0;
    qcap2_video_format_get_property(fmt_out, &color_space, &width, &height, &interleaved, &fps);
    assert(color_space == QCAP_COLORSPACE_TYPE_YUY2);
    assert(width == 1920);
    assert(height == 1080);
    assert(fps == 30.0);

    assert(qcap2_video_source_start(vsrc) == QCAP_RS_SUCCESSFUL);

    // Wait a brief moment for the simulation thread to produce frames
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    qcap2_rcbuffer_t* popped = NULL;
    assert(qcap2_video_source_pop(vsrc, &popped) == QCAP_RS_SUCCESSFUL);
    assert(popped != NULL);

    PVOID pData = qcap2_rcbuffer_get_data(popped);
    assert(pData != NULL);
    qcap2_av_frame_t* pFrame = (qcap2_av_frame_t*)pData;
    uint8_t* pPixels = NULL;
    int stride = 0;
    qcap2_av_frame_get_buffer(pFrame, &pPixels, &stride);
    assert(pPixels != NULL);
    assert(stride == 1920 * 2);

    // Push the buffer back to recycle it
    assert(qcap2_video_source_push(vsrc, popped) == QCAP_RS_SUCCESSFUL);
    qcap2_rcbuffer_release(popped);

    assert(qcap2_video_source_stop(vsrc) == QCAP_RS_SUCCESSFUL);

    // Test V4L2 mode start fail-safe when no V4L2 device is present
    qcap2_video_source_set_backend_type(vsrc, QCAP2_VIDEO_SOURCE_BACKEND_TYPE_V4L2);
    assert(qcap2_video_source_start(vsrc) == QCAP_RS_ERROR_GENERAL);

    qcap2_video_source_delete(vsrc);
    qcap2_video_format_delete(fmt);
    qcap2_video_format_delete(fmt_out);
}

void test_qcap2_video_source_backends() {
    qcap2_video_source_t* vsrc = qcap2_video_source_new();
    assert(vsrc != NULL);

    // 1. Test V4L2 Properties
    qcap2_video_source_set_v4l2_name(vsrc, "/dev/video99");
    assert(strcmp(qcap2_video_source_get_v4l2_name(vsrc), "/dev/video99") == 0);

    qcap2_video_source_set_buf_type(vsrc, V4L2_BUF_TYPE_VIDEO_CAPTURE);
    qcap2_video_source_set_memory(vsrc, V4L2_MEMORY_MMAP);
    qcap2_video_source_set_exp_buf(vsrc, true);

    int fd = 0;
    assert(qcap2_video_source_get_fd(vsrc, &fd) == QCAP_RS_SUCCESSFUL);
    assert(fd == -1);

    qcap2_video_source_set_v4l2_sg_name(vsrc, 3, "sub-device-3");
    assert(strcmp(qcap2_video_source_get_v4l2_sg_name(vsrc, 3), "sub-device-3") == 0);

    qcap2_video_source_set_frame_count(vsrc, 8);

    // 2. Test polymorphic instantiation for each backend subclass
    int backend_types[] = {
        QCAP2_VIDEO_SOURCE_BACKEND_TYPE_USER,
        QCAP2_VIDEO_SOURCE_BACKEND_TYPE_TPG
    };

    for (int type : backend_types) {
        qcap2_video_source_set_backend_type(vsrc, type);
        assert(qcap2_video_source_start(vsrc) == QCAP_RS_SUCCESSFUL);
        assert(qcap2_video_source_stop(vsrc) == QCAP_RS_SUCCESSFUL);
    }

    // 3. Test USER backend buffer flow
    qcap2_video_source_set_backend_type(vsrc, QCAP2_VIDEO_SOURCE_BACKEND_TYPE_USER);
    assert(qcap2_video_source_start(vsrc) == QCAP_RS_SUCCESSFUL);

    qcap2_av_frame_t custom_frame;
    qcap2_av_frame_init(&custom_frame);
    qcap2_rcbuffer_t* custom_rcbuf = qcap2_rcbuffer_new(&custom_frame, [](PVOID){});
    assert(custom_rcbuf != NULL);

    assert(qcap2_video_source_push(vsrc, custom_rcbuf) == QCAP_RS_SUCCESSFUL);
    qcap2_rcbuffer_release(custom_rcbuf);

    qcap2_rcbuffer_t* user_popped = NULL;
    assert(qcap2_video_source_pop(vsrc, &user_popped) == QCAP_RS_SUCCESSFUL);
    assert(user_popped == custom_rcbuf);

    qcap2_rcbuffer_release(user_popped);
    assert(qcap2_video_source_stop(vsrc) == QCAP_RS_SUCCESSFUL);

    // 4. Test V4L2 memory modes (MMAP, USERPTR, DMABUF) fail-safe start & cleanup
    qcap2_video_source_set_backend_type(vsrc, QCAP2_VIDEO_SOURCE_BACKEND_TYPE_V4L2);
    qcap2_video_source_set_device_index(vsrc, 99); // nonexistent device index to trigger safe return

    qcap2_video_source_set_memory(vsrc, V4L2_MEMORY_MMAP);
    assert(qcap2_video_source_start(vsrc) == QCAP_RS_ERROR_GENERAL);

    qcap2_video_source_set_memory(vsrc, V4L2_MEMORY_USERPTR);
    assert(qcap2_video_source_start(vsrc) == QCAP_RS_ERROR_GENERAL);

    qcap2_video_source_set_memory(vsrc, V4L2_MEMORY_DMABUF);
    assert(qcap2_video_source_start(vsrc) == QCAP_RS_ERROR_GENERAL);

    qcap2_video_source_delete(vsrc);
}

void test_qcap2_audio_source_backends() {
    qcap2_audio_source_t* asrc = qcap2_audio_source_new();
    assert(asrc != NULL);

    qcap2_audio_source_set_backend_type(asrc, QCAP2_AUDIO_SOURCE_BACKEND_TYPE_TPG);
    qcap2_audio_source_set_period_time(asrc, 10);
    qcap2_audio_source_set_ideal_timer(asrc, true);

    qcap2_audio_format_t* fmt = qcap2_audio_format_new();
    assert(fmt != NULL);
    qcap2_audio_format_set_property(fmt, 2, 16, 48000);
    qcap2_audio_source_set_audio_format(asrc, fmt);

    assert(qcap2_audio_source_start(asrc) == QCAP_RS_SUCCESSFUL);

    // Wait a brief moment for the simulation thread to produce audio frames
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    qcap2_rcbuffer_t* popped = NULL;
    assert(qcap2_audio_source_pop(asrc, &popped) == QCAP_RS_SUCCESSFUL);
    assert(popped != NULL);

    PVOID pData = qcap2_rcbuffer_get_data(popped);
    assert(pData != NULL);
    qcap2_av_frame_t* pFrame = (qcap2_av_frame_t*)pData;

    ULONG channels = 0, sample_fmt = 0, sample_frequency = 0, frame_size = 0;
    qcap2_av_frame_get_audio_property(pFrame, &channels, &sample_fmt, &sample_frequency, &frame_size);
    assert(channels == 2);
    assert(sample_fmt == 16);
    assert(sample_frequency == 48000);
    assert(frame_size > 0);

    uint8_t* pSamples = NULL;
    int stride = 0;
    qcap2_av_frame_get_buffer(pFrame, &pSamples, &stride);
    assert(pSamples != NULL);
    assert(stride == (int)frame_size);

    qcap2_rcbuffer_release(popped);

    assert(qcap2_audio_source_stop(asrc) == QCAP_RS_SUCCESSFUL);

    // Test ALSA start fail-safe when dev PCM node is not present (or in sandbox)
    qcap2_audio_source_set_backend_type(asrc, QCAP2_AUDIO_SOURCE_BACKEND_TYPE_ALSA);
    qcap2_audio_source_set_card(asrc, 99);
    qcap2_audio_source_set_device(asrc, 9);
    assert(qcap2_audio_source_start(asrc) == QCAP_RS_ERROR_GENERAL);

    qcap2_audio_source_delete(asrc);
    qcap2_audio_format_delete(fmt);
}

int main() {
    test_qcap2_benaphore_lock_t();
    test_qcap2_block_lock_t();
    test_qcap2_event_t();
    test_qcap2_event_handlers_t();
    test_qcap2_rcbuffer_queue_t();
    test_qcap2_rcbuffer_queue_provider_consumer_scheme_t();
    test_qcap2_rcbuffer_queue_event_t();
    test_qcap2_video_source_t();
    test_qcap2_video_source_backends();
    test_qcap2_audio_source_backends();
    test_qcap2_timer_t();
    test_qcap2_window_t();
    test_qcap2_binder_t();
    printf("Sync test basic locks passed!\n");
    return 0;
}

