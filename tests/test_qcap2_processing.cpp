#include "qcap2.processing.h"
#include "qcap2.buffer.h"
#include "qcap2.sync.h"
#include "qcap2.formats.h"
#include "qcap2.sync.h"
#include <stdio.h>
#include <assert.h>
#include <vector>
#include <string.h>
#include <atomic>
#include <thread>
#include <chrono>

void test_audio_resampler() {
    qcap2_audio_resampler_t* resampler = qcap2_audio_resampler_new();
    assert(resampler != NULL);

    // Set destination properties: 1 channel, format 3 (AV_SAMPLE_FMT_FLT), 48000Hz, frame size 0
    qcap2_audio_resampler_set_audio_property(resampler, 1, 3, 48000, 0);
    qcap2_audio_resampler_set_frame_count(resampler, 5);

    assert(qcap2_audio_resampler_start(resampler) == QCAP_RS_SUCCESSFUL);

    // Prepare mock input frame: 2 channels, format 1 (AV_SAMPLE_FMT_S16), 44100Hz, 1024 samples
    qcap2_av_frame_t in_frame;
    qcap2_av_frame_init(&in_frame);
    qcap2_av_frame_set_audio_property(&in_frame, 2, 1, 44100, 1024);

    std::vector<int16_t> in_buffer(1024 * 2);
    for (size_t i = 0; i < in_buffer.size(); ++i) {
        in_buffer[i] = (int16_t)(i % 1000);
    }

    qcap2_av_frame_set_buffer(&in_frame, (uint8_t*)in_buffer.data(), 1024 * 2 * sizeof(int16_t));
    qcap2_av_frame_set_pts(&in_frame, 12345);

    // Wrap in rc_buffer
    qcap2_rcbuffer_t* in_rc = qcap2_rcbuffer_new(&in_frame, NULL);
    assert(in_rc != NULL);

    // Push frame
    assert(qcap2_audio_resampler_push(resampler, in_rc) == QCAP_RS_SUCCESSFUL);
    qcap2_rcbuffer_release(in_rc);

    qcap2_rcbuffer_t* recycled_in = nullptr;
    assert(qcap2_audio_resampler_pop_input(resampler, &recycled_in) == QCAP_RS_SUCCESSFUL);
    assert(recycled_in == in_rc);
    qcap2_rcbuffer_release(recycled_in);

    // Pop resampled frame
    qcap2_rcbuffer_t* out_rc = NULL;
    assert(qcap2_audio_resampler_pop(resampler, &out_rc) == QCAP_RS_SUCCESSFUL);
    assert(out_rc != NULL);

    // Lock and inspect properties
    PVOID out_data = qcap2_rcbuffer_lock_data(out_rc);
    assert(out_data != NULL);

    qcap2_av_frame_t* out_frame = (qcap2_av_frame_t*)out_data;
    ULONG out_ch = 0, out_fmt = 0, out_freq = 0, out_size = 0;
    qcap2_av_frame_get_audio_property(out_frame, &out_ch, &out_fmt, &out_freq, &out_size);

    assert(out_ch == 1);
    assert(out_fmt == 3);
    assert(out_freq == 48000);
    assert(out_size > 1000 && out_size < 1200);

    int64_t out_pts = 0;
    qcap2_av_frame_get_pts(out_frame, &out_pts);
    assert(out_pts == 12345);

    uint8_t* out_buf = NULL;
    int out_stride = 0;
    qcap2_av_frame_get_buffer(out_frame, &out_buf, &out_stride);
    assert(out_buf != NULL);

    qcap2_rcbuffer_unlock_data(out_rc);
    assert(qcap2_audio_resampler_push_output(resampler, out_rc) == QCAP_RS_SUCCESSFUL);
    qcap2_rcbuffer_release(out_rc);

    assert(qcap2_audio_resampler_stop(resampler) == QCAP_RS_SUCCESSFUL);
    qcap2_audio_resampler_delete(resampler);

    printf("Audio resampler unit tests passed successfully!\n");
}

void test_video_scaler_direct() {
    qcap2_video_scaler_t* scaler = qcap2_video_scaler_new();
    assert(scaler != NULL);

    // Output format: BGR24, 640x360
    qcap2_video_format_t* out_fmt = qcap2_video_format_new();
    qcap2_video_format_set_property(out_fmt, QCAP_COLORSPACE_TYPE_BGR24, 640, 360, FALSE, 30.0);
    qcap2_video_scaler_set_video_format(scaler, out_fmt);
    qcap2_video_format_delete(out_fmt);

    assert(qcap2_video_scaler_start(scaler) == QCAP_RS_SUCCESSFUL);

    // Input frame: RGB24, 1280x720
    qcap2_av_frame_t in_frame;
    qcap2_av_frame_init(&in_frame);
    qcap2_av_frame_set_video_property(&in_frame, QCAP_COLORSPACE_TYPE_RGB24, 1280, 720);
    assert(qcap2_av_frame_alloc_buffer(&in_frame, 16, 1));

    uint8_t* in_ptrs[4] = { nullptr };
    int in_strides[4] = { 0 };
    qcap2_av_frame_get_buffer1(&in_frame, in_ptrs, in_strides);
    // Fill input buffer with mock pattern
    for (int y = 0; y < 720; ++y) {
        for (int x = 0; x < 1280 * 3; ++x) {
            in_ptrs[0][y * in_strides[0] + x] = (uint8_t)((y + x) % 256);
        }
    }
    qcap2_av_frame_set_pts(&in_frame, 98765);

    qcap2_rcbuffer_t* in_rc = qcap2_rcbuffer_new(&in_frame, [](PVOID p) {
        qcap2_av_frame_free_buffer((qcap2_av_frame_t*)p);
    });

    // Push frame
    assert(qcap2_video_scaler_push(scaler, in_rc) == QCAP_RS_SUCCESSFUL);
    qcap2_rcbuffer_release(in_rc);

    qcap2_rcbuffer_t* recycled_in = nullptr;
    assert(qcap2_video_scaler_pop_input(scaler, &recycled_in) == QCAP_RS_SUCCESSFUL);
    assert(recycled_in == in_rc);
    qcap2_rcbuffer_release(recycled_in);

    // Pop resampled frame
    qcap2_rcbuffer_t* out_rc = NULL;
    assert(qcap2_video_scaler_pop(scaler, &out_rc) == QCAP_RS_SUCCESSFUL);
    assert(out_rc != NULL);

    PVOID out_data = qcap2_rcbuffer_lock_data(out_rc);
    assert(out_data != NULL);

    qcap2_av_frame_t* out_frame = (qcap2_av_frame_t*)out_data;
    ULONG col = 0, w = 0, h = 0;
    qcap2_av_frame_get_video_property(out_frame, &col, &w, &h);
    assert(col == QCAP_COLORSPACE_TYPE_BGR24);
    assert(w == 640);
    assert(h == 360);

    int64_t pts = 0;
    qcap2_av_frame_get_pts(out_frame, &pts);
    assert(pts == 98765);

    qcap2_rcbuffer_unlock_data(out_rc);
    assert(qcap2_video_scaler_push_output(scaler, out_rc) == QCAP_RS_SUCCESSFUL);
    qcap2_rcbuffer_release(out_rc);

    assert(qcap2_video_scaler_stop(scaler) == QCAP_RS_SUCCESSFUL);
    qcap2_video_scaler_delete(scaler);

    printf("Video scaler direct scaling tests passed successfully!\n");
}

void test_video_scaler_crop() {
    qcap2_video_scaler_t* scaler = qcap2_video_scaler_new();
    assert(scaler != NULL);

    qcap2_video_format_t* out_fmt = qcap2_video_format_new();
    qcap2_video_format_set_property(out_fmt, QCAP_COLORSPACE_TYPE_BGR24, 100, 100, FALSE, 30.0);
    qcap2_video_scaler_set_video_format(scaler, out_fmt);
    qcap2_video_format_delete(out_fmt);

    // Crop settings
    qcap2_video_scaler_set_crop(scaler, 10, 20, 200, 150);

    assert(qcap2_video_scaler_start(scaler) == QCAP_RS_SUCCESSFUL);

    // Input: RGB24, 640x480
    qcap2_av_frame_t in_frame;
    qcap2_av_frame_init(&in_frame);
    qcap2_av_frame_set_video_property(&in_frame, QCAP_COLORSPACE_TYPE_RGB24, 640, 480);
    assert(qcap2_av_frame_alloc_buffer(&in_frame, 16, 1));

    qcap2_rcbuffer_t* in_rc = qcap2_rcbuffer_new(&in_frame, [](PVOID p) {
        qcap2_av_frame_free_buffer((qcap2_av_frame_t*)p);
    });

    assert(qcap2_video_scaler_push(scaler, in_rc) == QCAP_RS_SUCCESSFUL);
    qcap2_rcbuffer_release(in_rc);

    qcap2_rcbuffer_t* recycled_in = nullptr;
    assert(qcap2_video_scaler_pop_input(scaler, &recycled_in) == QCAP_RS_SUCCESSFUL);
    assert(recycled_in == in_rc);
    qcap2_rcbuffer_release(recycled_in);

    qcap2_rcbuffer_t* out_rc = NULL;
    assert(qcap2_video_scaler_pop(scaler, &out_rc) == QCAP_RS_SUCCESSFUL);
    assert(out_rc != NULL);

    PVOID out_data = qcap2_rcbuffer_lock_data(out_rc);
    qcap2_av_frame_t* out_frame = (qcap2_av_frame_t*)out_data;
    ULONG col = 0, w = 0, h = 0;
    qcap2_av_frame_get_video_property(out_frame, &col, &w, &h);
    assert(w == 100);
    assert(h == 100);

    qcap2_rcbuffer_unlock_data(out_rc);
    assert(qcap2_video_scaler_push_output(scaler, out_rc) == QCAP_RS_SUCCESSFUL);
    qcap2_rcbuffer_release(out_rc);

    assert(qcap2_video_scaler_stop(scaler) == QCAP_RS_SUCCESSFUL);
    qcap2_video_scaler_delete(scaler);

    printf("Video scaler cropping tests passed successfully!\n");
}

void test_video_scaler_buffer_pool() {
    qcap2_video_scaler_t* scaler = qcap2_video_scaler_new();
    assert(scaler != NULL);

    qcap2_video_format_t* out_fmt = qcap2_video_format_new();
    qcap2_video_format_set_property(out_fmt, QCAP_COLORSPACE_TYPE_BGR24, 320, 240, FALSE, 30.0);
    qcap2_video_scaler_set_video_format(scaler, out_fmt);
    qcap2_video_format_delete(out_fmt);

    // Pre-allocate 2 buffers and register them
    qcap2_av_frame_t pool_frame1, pool_frame2;
    qcap2_av_frame_init(&pool_frame1);
    qcap2_av_frame_set_video_property(&pool_frame1, QCAP_COLORSPACE_TYPE_BGR24, 320, 240);
    assert(qcap2_av_frame_alloc_buffer(&pool_frame1, 16, 1));

    qcap2_av_frame_init(&pool_frame2);
    qcap2_av_frame_set_video_property(&pool_frame2, QCAP_COLORSPACE_TYPE_BGR24, 320, 240);
    assert(qcap2_av_frame_alloc_buffer(&pool_frame2, 16, 1));

    qcap2_rcbuffer_t* pool_rc1 = qcap2_rcbuffer_new(&pool_frame1, [](PVOID p) {
        qcap2_av_frame_free_buffer((qcap2_av_frame_t*)p);
    });
    qcap2_rcbuffer_t* pool_rc2 = qcap2_rcbuffer_new(&pool_frame2, [](PVOID p) {
        qcap2_av_frame_free_buffer((qcap2_av_frame_t*)p);
    });

    qcap2_rcbuffer_t* pool_arr[] = { pool_rc1, pool_rc2, nullptr };
    qcap2_video_scaler_set_buffers(scaler, pool_arr);

    // Releases original ownership references immediately so scaler owns them with use_count == 1
    qcap2_rcbuffer_release(pool_rc1);
    qcap2_rcbuffer_release(pool_rc2);

    assert(qcap2_video_scaler_start(scaler) == QCAP_RS_SUCCESSFUL);

    // Input: RGB24, 640x480
    qcap2_av_frame_t in_frame;
    qcap2_av_frame_init(&in_frame);
    qcap2_av_frame_set_video_property(&in_frame, QCAP_COLORSPACE_TYPE_RGB24, 640, 480);
    assert(qcap2_av_frame_alloc_buffer(&in_frame, 16, 1));

    qcap2_rcbuffer_t* in_rc = qcap2_rcbuffer_new(&in_frame, [](PVOID p) {
        qcap2_av_frame_free_buffer((qcap2_av_frame_t*)p);
    });

    // Push frame
    assert(qcap2_video_scaler_push(scaler, in_rc) == QCAP_RS_SUCCESSFUL);
    qcap2_rcbuffer_release(in_rc);

    qcap2_rcbuffer_t* recycled_in = nullptr;
    assert(qcap2_video_scaler_pop_input(scaler, &recycled_in) == QCAP_RS_SUCCESSFUL);
    assert(recycled_in == in_rc);
    qcap2_rcbuffer_release(recycled_in);

    // Pop popped frame
    qcap2_rcbuffer_t* out_rc = NULL;
    assert(qcap2_video_scaler_pop(scaler, &out_rc) == QCAP_RS_SUCCESSFUL);
    assert(out_rc != NULL);

    // Check that out_rc is indeed pool_rc1 or pool_rc2!
    assert(out_rc == pool_rc1 || out_rc == pool_rc2);

    qcap2_rcbuffer_release(out_rc);

    assert(qcap2_video_scaler_stop(scaler) == QCAP_RS_SUCCESSFUL);
    qcap2_video_scaler_delete(scaler);

    printf("Video scaler pre-allocated buffer pool tests passed successfully!\n");
}

void test_video_scaler_filter_graph() {
    qcap2_video_scaler_t* scaler = qcap2_video_scaler_new();
    assert(scaler != NULL);

    qcap2_video_format_t* out_fmt = qcap2_video_format_new();
    qcap2_video_format_set_property(out_fmt, QCAP_COLORSPACE_TYPE_BGR24, 320, 240, FALSE, 30.0);
    qcap2_video_scaler_set_video_format(scaler, out_fmt);
    qcap2_video_format_delete(out_fmt);

    // Set filter graph
    qcap2_video_scaler_set_filter_graph(scaler, "scale=160:120");

    assert(qcap2_video_scaler_start(scaler) == QCAP_RS_SUCCESSFUL);

    // Input: RGB24, 640x480
    qcap2_av_frame_t in_frame;
    qcap2_av_frame_init(&in_frame);
    qcap2_av_frame_set_video_property(&in_frame, QCAP_COLORSPACE_TYPE_RGB24, 640, 480);
    assert(qcap2_av_frame_alloc_buffer(&in_frame, 16, 1));

    qcap2_rcbuffer_t* in_rc = qcap2_rcbuffer_new(&in_frame, [](PVOID p) {
        qcap2_av_frame_free_buffer((qcap2_av_frame_t*)p);
    });

    assert(qcap2_video_scaler_push(scaler, in_rc) == QCAP_RS_SUCCESSFUL);
    qcap2_rcbuffer_release(in_rc);

    qcap2_rcbuffer_t* recycled_in = nullptr;
    assert(qcap2_video_scaler_pop_input(scaler, &recycled_in) == QCAP_RS_SUCCESSFUL);
    assert(recycled_in == in_rc);
    qcap2_rcbuffer_release(recycled_in);

    qcap2_rcbuffer_t* out_rc = NULL;
    assert(qcap2_video_scaler_pop(scaler, &out_rc) == QCAP_RS_SUCCESSFUL);
    assert(out_rc != NULL);

    PVOID out_data = qcap2_rcbuffer_lock_data(out_rc);
    qcap2_av_frame_t* out_frame = (qcap2_av_frame_t*)out_data;
    ULONG col = 0, w = 0, h = 0;
    qcap2_av_frame_get_video_property(out_frame, &col, &w, &h);
    // Since avfilter has "scale=160:120" override, scaled resolution is 160x120!
    assert(w == 160);
    assert(h == 120);

    qcap2_rcbuffer_unlock_data(out_rc);
    assert(qcap2_video_scaler_push_output(scaler, out_rc) == QCAP_RS_SUCCESSFUL);
    qcap2_rcbuffer_release(out_rc);

    assert(qcap2_video_scaler_stop(scaler) == QCAP_RS_SUCCESSFUL);
    qcap2_video_scaler_delete(scaler);

    printf("Video scaler avfilter filter graph tests passed successfully!\n");
}

void test_frame_pool_video_basic() {
    qcap2_frame_pool_t* pool = qcap2_frame_pool_new();
    assert(pool != NULL);

    // Configure: 3 BGR24 640x480 frames
    qcap2_frame_pool_set_frame_count(pool, 3);
    qcap2_frame_pool_set_video_property(pool, QCAP_COLORSPACE_TYPE_BGR24, 640, 480);
    qcap2_frame_pool_set_video_frame_align(pool, 16, 1);

    assert(qcap2_frame_pool_start(pool) == QCAP_RS_SUCCESSFUL);

    // Get a buffer
    qcap2_rcbuffer_t* buf1 = NULL;
    assert(qcap2_frame_pool_get_buffer(pool, &buf1) == QCAP_RS_SUCCESSFUL);
    assert(buf1 != NULL);

    // Inspect the frame inside
    PVOID data = qcap2_rcbuffer_lock_data(buf1);
    assert(data != NULL);
    qcap2_av_frame_t* frame = (qcap2_av_frame_t*)data;

    ULONG col = 0, w = 0, h = 0;
    qcap2_av_frame_get_video_property(frame, &col, &w, &h);
    assert(col == QCAP_COLORSPACE_TYPE_BGR24);
    assert(w == 640);
    assert(h == 480);

    // Verify buffer is allocated
    uint8_t* ptrs[4] = { nullptr };
    int strides[4] = { 0 };
    qcap2_av_frame_get_buffer1(frame, ptrs, strides);
    assert(ptrs[0] != NULL);
    assert(strides[0] >= 640 * 3);

    qcap2_rcbuffer_unlock_data(buf1);
    qcap2_rcbuffer_release(buf1);

    assert(qcap2_frame_pool_stop(pool) == QCAP_RS_SUCCESSFUL);
    qcap2_frame_pool_delete(pool);

    printf("Frame pool video basic tests passed successfully!\n");
}

void test_frame_pool_video_recycling() {
    qcap2_frame_pool_t* pool = qcap2_frame_pool_new();
    assert(pool != NULL);

    qcap2_frame_pool_set_frame_count(pool, 2);
    qcap2_frame_pool_set_video_property(pool, QCAP_COLORSPACE_TYPE_RGB24, 320, 240);

    assert(qcap2_frame_pool_start(pool) == QCAP_RS_SUCCESSFUL);

    // Get first buffer
    qcap2_rcbuffer_t* buf1 = NULL;
    assert(qcap2_frame_pool_get_buffer(pool, &buf1) == QCAP_RS_SUCCESSFUL);

    // Get second buffer
    qcap2_rcbuffer_t* buf2 = NULL;
    assert(qcap2_frame_pool_get_buffer(pool, &buf2) == QCAP_RS_SUCCESSFUL);

    // They must be different
    assert(buf1 != buf2);

    // Now both are in use — no idle buffer available
    qcap2_rcbuffer_t* buf3 = NULL;
    assert(qcap2_frame_pool_get_buffer(pool, &buf3) == QCAP_RS_ERROR_GENERAL);
    assert(buf3 == NULL);

    // Release buf1, then we should be able to get it back
    qcap2_rcbuffer_release(buf1);

    qcap2_rcbuffer_t* buf4 = NULL;
    assert(qcap2_frame_pool_get_buffer(pool, &buf4) == QCAP_RS_SUCCESSFUL);
    assert(buf4 == buf1); // recycled!

    qcap2_rcbuffer_release(buf4);
    qcap2_rcbuffer_release(buf2);

    assert(qcap2_frame_pool_stop(pool) == QCAP_RS_SUCCESSFUL);
    qcap2_frame_pool_delete(pool);

    printf("Frame pool video recycling tests passed successfully!\n");
}

void test_frame_pool_audio() {
    qcap2_frame_pool_t* pool = qcap2_frame_pool_new();
    assert(pool != NULL);

    // Configure: 2 audio frames, stereo S16, 48kHz, 1024 samples
    qcap2_frame_pool_set_frame_count(pool, 2);
    qcap2_frame_pool_set_audio_property(pool, 2, 1 /* AV_SAMPLE_FMT_S16 */, 48000, 1024);

    assert(qcap2_frame_pool_start(pool) == QCAP_RS_SUCCESSFUL);

    qcap2_rcbuffer_t* buf = NULL;
    assert(qcap2_frame_pool_get_buffer(pool, &buf) == QCAP_RS_SUCCESSFUL);
    assert(buf != NULL);

    PVOID data = qcap2_rcbuffer_lock_data(buf);
    assert(data != NULL);
    qcap2_av_frame_t* frame = (qcap2_av_frame_t*)data;

    ULONG ch = 0, fmt = 0, freq = 0, fsize = 0;
    qcap2_av_frame_get_audio_property(frame, &ch, &fmt, &freq, &fsize);
    assert(ch == 2);
    assert(fmt == 1);
    assert(freq == 48000);
    assert(fsize == 1024);

    // Verify buffer is allocated: 2 channels * 2 bytes_per_sample * 1024 = 4096 bytes
    uint8_t* audio_buf = NULL;
    int audio_stride = 0;
    qcap2_av_frame_get_buffer(frame, &audio_buf, &audio_stride);
    assert(audio_buf != NULL);
    assert(audio_stride == 4096);

    qcap2_rcbuffer_unlock_data(buf);
    qcap2_rcbuffer_release(buf);

    assert(qcap2_frame_pool_stop(pool) == QCAP_RS_SUCCESSFUL);
    qcap2_frame_pool_delete(pool);

    printf("Frame pool audio tests passed successfully!\n");
}

void test_frame_pool_video_with_border() {
    qcap2_frame_pool_t* pool = qcap2_frame_pool_new();
    assert(pool != NULL);

    qcap2_frame_pool_set_frame_count(pool, 1);
    qcap2_frame_pool_set_video_property(pool, QCAP_COLORSPACE_TYPE_BGR24, 640, 480);
    // Set 8-pixel border on each side
    qcap2_frame_pool_set_video_property1(pool, 8, 8, FALSE);

    assert(qcap2_frame_pool_start(pool) == QCAP_RS_SUCCESSFUL);

    qcap2_rcbuffer_t* buf = NULL;
    assert(qcap2_frame_pool_get_buffer(pool, &buf) == QCAP_RS_SUCCESSFUL);

    PVOID data = qcap2_rcbuffer_lock_data(buf);
    qcap2_av_frame_t* frame = (qcap2_av_frame_t*)data;

    ULONG col = 0, w = 0, h = 0;
    qcap2_av_frame_get_video_property(frame, &col, &w, &h);
    // Allocated with border: 640 + 8*2 = 656, 480 + 8*2 = 496
    assert(w == 656);
    assert(h == 496);

    qcap2_rcbuffer_unlock_data(buf);
    qcap2_rcbuffer_release(buf);

    assert(qcap2_frame_pool_stop(pool) == QCAP_RS_SUCCESSFUL);
    qcap2_frame_pool_delete(pool);

    printf("Frame pool video with border tests passed successfully!\n");
}

void test_frame_pool_lifecycle() {
    qcap2_frame_pool_t* pool = qcap2_frame_pool_new();
    assert(pool != NULL);

    // get_buffer before start should fail
    qcap2_rcbuffer_t* buf = NULL;
    assert(qcap2_frame_pool_get_buffer(pool, &buf) == QCAP_RS_ERROR_GENERAL);

    qcap2_frame_pool_set_frame_count(pool, 2);
    qcap2_frame_pool_set_video_property(pool, QCAP_COLORSPACE_TYPE_I420, 1920, 1080);
    assert(qcap2_frame_pool_start(pool) == QCAP_RS_SUCCESSFUL);

    // Double start should be idempotent
    assert(qcap2_frame_pool_start(pool) == QCAP_RS_SUCCESSFUL);

    // Get buffer should work now
    assert(qcap2_frame_pool_get_buffer(pool, &buf) == QCAP_RS_SUCCESSFUL);
    assert(buf != NULL);

    // Verify I420 layout (3-plane)
    PVOID data = qcap2_rcbuffer_lock_data(buf);
    qcap2_av_frame_t* frame = (qcap2_av_frame_t*)data;
    uint8_t* ptrs[4] = { nullptr };
    int strides[4] = { 0 };
    qcap2_av_frame_get_buffer1(frame, ptrs, strides);
    assert(ptrs[0] != NULL);
    assert(ptrs[1] != NULL);
    assert(ptrs[2] != NULL);
    assert(strides[0] >= 1920);
    assert(strides[1] >= 960);

    qcap2_rcbuffer_unlock_data(buf);
    qcap2_rcbuffer_release(buf);

    // Stop and restart
    assert(qcap2_frame_pool_stop(pool) == QCAP_RS_SUCCESSFUL);

    // get_buffer after stop should fail
    buf = NULL;
    assert(qcap2_frame_pool_get_buffer(pool, &buf) == QCAP_RS_ERROR_GENERAL);

    // Restart with different config
    qcap2_frame_pool_set_frame_count(pool, 1);
    qcap2_frame_pool_set_video_property(pool, QCAP_COLORSPACE_TYPE_NV12, 1280, 720);
    assert(qcap2_frame_pool_start(pool) == QCAP_RS_SUCCESSFUL);

    assert(qcap2_frame_pool_get_buffer(pool, &buf) == QCAP_RS_SUCCESSFUL);
    data = qcap2_rcbuffer_lock_data(buf);
    frame = (qcap2_av_frame_t*)data;
    ULONG col = 0, w = 0, h = 0;
    qcap2_av_frame_get_video_property(frame, &col, &w, &h);
    assert(col == QCAP_COLORSPACE_TYPE_NV12);
    assert(w == 1280);
    assert(h == 720);

    qcap2_rcbuffer_unlock_data(buf);
    qcap2_rcbuffer_release(buf);

    assert(qcap2_frame_pool_stop(pool) == QCAP_RS_SUCCESSFUL);
    qcap2_frame_pool_delete(pool);

    printf("Frame pool lifecycle tests passed successfully!\n");
}

void test_video_encoder_h264_basic() {
    qcap2_video_encoder_t* encoder = qcap2_video_encoder_new();
    assert(encoder != NULL);

    // Configure encoder: H.264, I420, 320x240, 30fps, CBR 1000kbps, GOP=10
    qcap2_video_encoder_property_t* enc_prop = qcap2_video_encoder_property_new();
    qcap2_video_encoder_property_set_property(enc_prop,
        QCAP_ENCODER_TYPE_SOFTWARE,   // nEncoderType
        QCAP_ENCODER_FORMAT_H264,     // nEncoderFormat
        QCAP_COLORSPACE_TYPE_I420,    // nColorSpaceType
        320, 240,                     // width, height
        30.0,                         // fps
        QCAP_RECORD_MODE_CBR,         // nRecordMode
        0,                            // nQuality
        1000,                         // nBitRate (kbps)
        10,                           // nGOP
        1, 1);                        // aspect ratio
    qcap2_video_encoder_set_video_property(encoder, enc_prop);
    qcap2_video_encoder_property_delete(enc_prop);

    assert(qcap2_video_encoder_start(encoder) == QCAP_RS_SUCCESSFUL);

    // Verify extra data is available after start (SPS/PPS for H.264)
    uint8_t* extra = NULL;
    int extra_size = 0;
    qcap2_video_encoder_get_extra_data(encoder, &extra, &extra_size);
    // With zerolatency, extra data should be present
    // Note: some builds may not produce extradata with certain settings

    // Create and push multiple frames to ensure encoder produces output
    int frames_pushed = 0;

    qcap2_rcbuffer_t* in_rc = nullptr;
    for (int f = 0; f < 5; ++f) {
        if (f == 0) {
            qcap2_av_frame_t* in_frame = new qcap2_av_frame_t;
            qcap2_av_frame_init(in_frame);
            qcap2_av_frame_set_video_property(in_frame, QCAP_COLORSPACE_TYPE_I420, 320, 240);
            assert(qcap2_av_frame_alloc_buffer(in_frame, 16, 1));

            in_rc = qcap2_rcbuffer_new(in_frame, [](PVOID p) {
                qcap2_av_frame_free_buffer((qcap2_av_frame_t*)p);
                delete (qcap2_av_frame_t*)p;
            });
        } else {
            // Pop the recycled input buffer (HPR model)
            qcap2_rcbuffer_t* recycled = nullptr;
            assert(qcap2_video_encoder_pop_input(encoder, &recycled) == QCAP_RS_SUCCESSFUL);
            assert(recycled == in_rc);
        }

        // Fill with a pattern
        PVOID pData = qcap2_rcbuffer_lock_data(in_rc);
        assert(pData != nullptr);
        qcap2_av_frame_t* frame = (qcap2_av_frame_t*)pData;

        uint8_t* ptrs[4] = { nullptr };
        int strides[4] = { 0 };
        qcap2_av_frame_get_buffer1(frame, ptrs, strides);
        // Fill Y plane with gradient
        for (int y = 0; y < 240; ++y) {
            for (int x = 0; x < strides[0] && x < 320; ++x) {
                ptrs[0][y * strides[0] + x] = (uint8_t)((y + x + f * 10) % 256);
            }
        }
        // Fill U/V planes with mid-gray
        if (ptrs[1]) memset(ptrs[1], 128, strides[1] * 120);
        if (ptrs[2]) memset(ptrs[2], 128, strides[2] * 120);

        qcap2_av_frame_set_pts(frame, f * 3000);
        qcap2_rcbuffer_unlock_data(in_rc);

        assert(qcap2_video_encoder_push(encoder, in_rc) == QCAP_RS_SUCCESSFUL);
        qcap2_rcbuffer_release(in_rc);
        frames_pushed++;
    }

    // Pop all available encoded packets (non-blocking check via stop)
    // Stop flushes the encoder, making all packets available
    assert(qcap2_video_encoder_stop(encoder) == QCAP_RS_SUCCESSFUL);

    // Verify we got at least some encoded packets during the push calls
    // With zerolatency and 5 frames, we should have gotten packets
    // Since stop() drains the queue, we verify the lifecycle is clean
    qcap2_rcbuffer_t* pkt_rc = nullptr;
    while (qcap2_video_encoder_pop(encoder, &pkt_rc) == QCAP_RS_SUCCESSFUL) {
        assert(pkt_rc != nullptr);
        // Recycle the output packet using push_output + release (PPR model)
        assert(qcap2_video_encoder_push_output(encoder, pkt_rc) == QCAP_RS_SUCCESSFUL);
        qcap2_rcbuffer_release(pkt_rc);
        pkt_rc = nullptr;
    }

    // Restart and encode again to test restart
    enc_prop = qcap2_video_encoder_property_new();
    qcap2_video_encoder_property_set_property(enc_prop,
        QCAP_ENCODER_TYPE_SOFTWARE, QCAP_ENCODER_FORMAT_H264,
        QCAP_COLORSPACE_TYPE_I420, 320, 240, 30.0,
        QCAP_RECORD_MODE_CBR, 0, 1000, 10, 1, 1);
    qcap2_video_encoder_set_video_property(encoder, enc_prop);
    qcap2_video_encoder_property_delete(enc_prop);

    assert(qcap2_video_encoder_start(encoder) == QCAP_RS_SUCCESSFUL);

    // Push a single frame and pop a packet
    {
        qcap2_av_frame_t in_frame;
        qcap2_av_frame_init(&in_frame);
        qcap2_av_frame_set_video_property(&in_frame, QCAP_COLORSPACE_TYPE_I420, 320, 240);
        assert(qcap2_av_frame_alloc_buffer(&in_frame, 16, 1));

        uint8_t* ptrs[4] = { nullptr };
        int strides[4] = { 0 };
        qcap2_av_frame_get_buffer1(&in_frame, ptrs, strides);
        memset(ptrs[0], 128, strides[0] * 240);
        if (ptrs[1]) memset(ptrs[1], 128, strides[1] * 120);
        if (ptrs[2]) memset(ptrs[2], 128, strides[2] * 120);
        qcap2_av_frame_set_pts(&in_frame, 0);

        qcap2_rcbuffer_t* in_rc = qcap2_rcbuffer_new(&in_frame, [](PVOID p) {
            qcap2_av_frame_free_buffer((qcap2_av_frame_t*)p);
        });

        assert(qcap2_video_encoder_push(encoder, in_rc) == QCAP_RS_SUCCESSFUL);
        qcap2_rcbuffer_release(in_rc);

        // Pop recycled input buffer (HPR model)
        qcap2_rcbuffer_t* recycled_in = nullptr;
        assert(qcap2_video_encoder_pop_input(encoder, &recycled_in) == QCAP_RS_SUCCESSFUL);
        assert(recycled_in == in_rc);
        qcap2_rcbuffer_release(recycled_in);

        // With zerolatency, the first frame should produce a packet immediately
        qcap2_rcbuffer_t* out_rc = NULL;
        assert(qcap2_video_encoder_pop(encoder, &out_rc) == QCAP_RS_SUCCESSFUL);
        assert(out_rc != NULL);

        // Inspect the encoded packet
        PVOID out_data = qcap2_rcbuffer_lock_data(out_rc);
        assert(out_data != NULL);
        qcap2_av_packet_t* out_pkt = (qcap2_av_packet_t*)out_data;

        uint8_t* pkt_buf = NULL;
        int pkt_size = 0;
        qcap2_av_packet_get_buffer(out_pkt, &pkt_buf, &pkt_size);
        assert(pkt_buf != NULL);
        assert(pkt_size > 0);

        // First frame should be a key frame
        int stream_idx = -1;
        BOOL is_key = FALSE;
        qcap2_av_packet_get_property(out_pkt, &stream_idx, &is_key);
        assert(is_key == TRUE);

        int64_t pkt_pts = -1;
        qcap2_av_packet_get_pts(out_pkt, &pkt_pts);
        assert(pkt_pts >= 0);

        qcap2_rcbuffer_unlock_data(out_rc);

        // Recycle the output packet (PPR model)
        assert(qcap2_video_encoder_push_output(encoder, out_rc) == QCAP_RS_SUCCESSFUL);
        qcap2_rcbuffer_release(out_rc);
    }

    assert(qcap2_video_encoder_stop(encoder) == QCAP_RS_SUCCESSFUL);
    qcap2_video_encoder_delete(encoder);

    printf("Video encoder H.264 basic tests passed successfully!\n");
}

void test_video_encoder_bgr24_input() {
    qcap2_video_encoder_t* encoder = qcap2_video_encoder_new();
    assert(encoder != NULL);

    // Configure: H.264 encoder but with BGR24 input (requires pixel format conversion)
    qcap2_video_encoder_property_t* enc_prop = qcap2_video_encoder_property_new();
    qcap2_video_encoder_property_set_property(enc_prop,
        QCAP_ENCODER_TYPE_SOFTWARE, QCAP_ENCODER_FORMAT_H264,
        QCAP_COLORSPACE_TYPE_BGR24, 160, 120, 30.0,
        QCAP_RECORD_MODE_VBR, 0, 500, 5, 1, 1);
    qcap2_video_encoder_set_video_property(encoder, enc_prop);
    qcap2_video_encoder_property_delete(enc_prop);

    assert(qcap2_video_encoder_start(encoder) == QCAP_RS_SUCCESSFUL);

    // Push BGR24 frame
    qcap2_av_frame_t in_frame;
    qcap2_av_frame_init(&in_frame);
    qcap2_av_frame_set_video_property(&in_frame, QCAP_COLORSPACE_TYPE_BGR24, 160, 120);
    assert(qcap2_av_frame_alloc_buffer(&in_frame, 16, 1));

    uint8_t* ptrs[4] = { nullptr };
    int strides[4] = { 0 };
    qcap2_av_frame_get_buffer1(&in_frame, ptrs, strides);
    // Fill with blue-ish color
    for (int y = 0; y < 120; ++y) {
        for (int x = 0; x < 160; ++x) {
            ptrs[0][y * strides[0] + x * 3 + 0] = 200; // B
            ptrs[0][y * strides[0] + x * 3 + 1] = 100; // G
            ptrs[0][y * strides[0] + x * 3 + 2] = 50;  // R
        }
    }

    qcap2_rcbuffer_t* in_rc = qcap2_rcbuffer_new(&in_frame, [](PVOID p) {
        qcap2_av_frame_free_buffer((qcap2_av_frame_t*)p);
    });

    assert(qcap2_video_encoder_push(encoder, in_rc) == QCAP_RS_SUCCESSFUL);
    qcap2_rcbuffer_release(in_rc);

    // Pop and recycle input frame (HPR model)
    qcap2_rcbuffer_t* recycled_in = nullptr;
    assert(qcap2_video_encoder_pop_input(encoder, &recycled_in) == QCAP_RS_SUCCESSFUL);
    assert(recycled_in == in_rc);
    qcap2_rcbuffer_release(recycled_in);

    // With zerolatency, pop immediately
    qcap2_rcbuffer_t* out_rc = NULL;
    assert(qcap2_video_encoder_pop(encoder, &out_rc) == QCAP_RS_SUCCESSFUL);
    assert(out_rc != NULL);

    PVOID out_data = qcap2_rcbuffer_lock_data(out_rc);
    qcap2_av_packet_t* pkt = (qcap2_av_packet_t*)out_data;
    uint8_t* pkt_buf = NULL;
    int pkt_size = 0;
    qcap2_av_packet_get_buffer(pkt, &pkt_buf, &pkt_size);
    assert(pkt_buf != NULL);
    assert(pkt_size > 0);

    qcap2_rcbuffer_unlock_data(out_rc);

    // Recycle output packet (PPR model)
    assert(qcap2_video_encoder_push_output(encoder, out_rc) == QCAP_RS_SUCCESSFUL);
    qcap2_rcbuffer_release(out_rc);

    assert(qcap2_video_encoder_stop(encoder) == QCAP_RS_SUCCESSFUL);
    qcap2_video_encoder_delete(encoder);

    printf("Video encoder BGR24 input conversion tests passed successfully!\n");
}

void test_video_encoder_property_roundtrip() {
    qcap2_video_encoder_t* encoder = qcap2_video_encoder_new();
    assert(encoder != NULL);

    // Set properties
    qcap2_video_encoder_property_t* enc_prop = qcap2_video_encoder_property_new();
    qcap2_video_encoder_property_set_property(enc_prop,
        QCAP_ENCODER_TYPE_SOFTWARE, QCAP_ENCODER_FORMAT_H264,
        QCAP_COLORSPACE_TYPE_NV12, 1920, 1080, 60.0,
        QCAP_RECORD_MODE_CBR, 85, 5000, 30, 16, 9);
    qcap2_video_encoder_set_video_property(encoder, enc_prop);
    qcap2_video_encoder_property_delete(enc_prop);

    // Get properties back
    qcap2_video_encoder_property_t* out_prop = qcap2_video_encoder_property_new();
    qcap2_video_encoder_get_video_property(encoder, out_prop);

    ULONG encType, encFmt, color, w, h, recMode, qual, br, gop, arX, arY;
    double fps;
    qcap2_video_encoder_property_get_property(out_prop,
        &encType, &encFmt, &color, &w, &h, &fps,
        &recMode, &qual, &br, &gop, &arX, &arY);

    assert(encType == QCAP_ENCODER_TYPE_SOFTWARE);
    assert(encFmt == QCAP_ENCODER_FORMAT_H264);
    assert(color == QCAP_COLORSPACE_TYPE_NV12);
    assert(w == 1920);
    assert(h == 1080);
    assert(fps == 60.0);
    assert(recMode == QCAP_RECORD_MODE_CBR);
    assert(qual == 85);
    assert(br == 5000);
    assert(gop == 30);
    assert(arX == 16);
    assert(arY == 9);

    qcap2_video_encoder_property_delete(out_prop);

    // Test dynamic property roundtrip
    qcap2_video_encoder_dynamic_property_t* dyn_prop = qcap2_video_encoder_dynamic_property_new();
    qcap2_video_encoder_dynamic_set_property(dyn_prop, QCAP_RECORD_MODE_VBR, 90, 8000, 60);
    qcap2_video_encoder_set_dynamic_video_property(encoder, dyn_prop);
    qcap2_video_encoder_dynamic_property_delete(dyn_prop);

    qcap2_video_encoder_dynamic_property_t* dyn_out = qcap2_video_encoder_dynamic_property_new();
    qcap2_video_encoder_get_dynamic_video_property(encoder, dyn_out);
    ULONG dRecMode, dQual, dBr, dGop;
    qcap2_video_encoder_dynamic_get_property(dyn_out, &dRecMode, &dQual, &dBr, &dGop);
    assert(dRecMode == QCAP_RECORD_MODE_VBR);
    assert(dQual == 90);
    assert(dBr == 8000);
    assert(dGop == 60);
    qcap2_video_encoder_dynamic_property_delete(dyn_out);

    qcap2_video_encoder_delete(encoder);

    printf("Video encoder property roundtrip tests passed successfully!\n");
}

void test_video_encoder_idr_request() {
    qcap2_video_encoder_t* encoder = qcap2_video_encoder_new();
    assert(encoder != NULL);

    qcap2_video_encoder_property_t* enc_prop = qcap2_video_encoder_property_new();
    qcap2_video_encoder_property_set_property(enc_prop,
        QCAP_ENCODER_TYPE_SOFTWARE, QCAP_ENCODER_FORMAT_H264,
        QCAP_COLORSPACE_TYPE_I420, 160, 120, 30.0,
        QCAP_RECORD_MODE_CBR, 0, 500, 30, 1, 1); // GOP=30 so normally no keyframe soon
    qcap2_video_encoder_set_video_property(encoder, enc_prop);
    qcap2_video_encoder_property_delete(enc_prop);

    assert(qcap2_video_encoder_start(encoder) == QCAP_RS_SUCCESSFUL);

    // Push 3 frames, then request IDR, push another frame
    for (int f = 0; f < 4; ++f) {
        if (f == 3) {
            qcap2_video_encoder_request_idr(encoder);
        }

        qcap2_av_frame_t in_frame;
        qcap2_av_frame_init(&in_frame);
        qcap2_av_frame_set_video_property(&in_frame, QCAP_COLORSPACE_TYPE_I420, 160, 120);
        assert(qcap2_av_frame_alloc_buffer(&in_frame, 16, 1));
        uint8_t* ptrs[4] = { nullptr };
        int strides[4] = { 0 };
        qcap2_av_frame_get_buffer1(&in_frame, ptrs, strides);
        memset(ptrs[0], 128 + f * 10, strides[0] * 120);
        if (ptrs[1]) memset(ptrs[1], 128, strides[1] * 60);
        if (ptrs[2]) memset(ptrs[2], 128, strides[2] * 60);

        qcap2_rcbuffer_t* in_rc = qcap2_rcbuffer_new(&in_frame, [](PVOID p) {
            qcap2_av_frame_free_buffer((qcap2_av_frame_t*)p);
        });
        assert(qcap2_video_encoder_push(encoder, in_rc) == QCAP_RS_SUCCESSFUL);
        qcap2_rcbuffer_release(in_rc);
    }

    // Pop all 4 packets and verify IDR behavior
    bool found_non_first_key = false;
    for (int i = 0; i < 4; ++i) {
        qcap2_rcbuffer_t* out_rc = NULL;
        assert(qcap2_video_encoder_pop(encoder, &out_rc) == QCAP_RS_SUCCESSFUL);
        assert(out_rc != NULL);

        PVOID out_data = qcap2_rcbuffer_lock_data(out_rc);
        qcap2_av_packet_t* pkt = (qcap2_av_packet_t*)out_data;
        int stream_idx = -1;
        BOOL is_key = FALSE;
        qcap2_av_packet_get_property(pkt, &stream_idx, &is_key);

        if (i == 0) assert(is_key == TRUE); // First frame is always key
        if (i == 3 && is_key) found_non_first_key = true;

        qcap2_rcbuffer_unlock_data(out_rc);
        qcap2_rcbuffer_release(out_rc);
    }

    // IDR request should have produced a key frame at frame 3
    assert(found_non_first_key == true);

    assert(qcap2_video_encoder_stop(encoder) == QCAP_RS_SUCCESSFUL);
    qcap2_video_encoder_delete(encoder);

    printf("Video encoder IDR request tests passed successfully!\n");
}

void test_video_encoder_lifecycle() {
    qcap2_video_encoder_t* encoder = qcap2_video_encoder_new();
    assert(encoder != NULL);

    // Push before start should fail
    qcap2_av_frame_t dummy;
    qcap2_av_frame_init(&dummy);
    qcap2_rcbuffer_t* dummy_rc = qcap2_rcbuffer_new(&dummy, NULL);
    assert(qcap2_video_encoder_push(encoder, dummy_rc) == QCAP_RS_ERROR_GENERAL);
    qcap2_rcbuffer_release(dummy_rc);

    // Start without properties should fail
    assert(qcap2_video_encoder_start(encoder) == QCAP_RS_ERROR_GENERAL);

    // Configure and start
    qcap2_video_encoder_property_t* enc_prop = qcap2_video_encoder_property_new();
    qcap2_video_encoder_property_set_property(enc_prop,
        QCAP_ENCODER_TYPE_SOFTWARE, QCAP_ENCODER_FORMAT_H264,
        QCAP_COLORSPACE_TYPE_I420, 160, 120, 30.0,
        QCAP_RECORD_MODE_VBR, 0, 500, 10, 1, 1);
    qcap2_video_encoder_set_video_property(encoder, enc_prop);
    qcap2_video_encoder_property_delete(enc_prop);

    assert(qcap2_video_encoder_start(encoder) == QCAP_RS_SUCCESSFUL);

    // Double start should be idempotent
    assert(qcap2_video_encoder_start(encoder) == QCAP_RS_SUCCESSFUL);

    // Stop
    assert(qcap2_video_encoder_stop(encoder) == QCAP_RS_SUCCESSFUL);

    // Double stop should be idempotent
    assert(qcap2_video_encoder_stop(encoder) == QCAP_RS_SUCCESSFUL);

    // Delete
    qcap2_video_encoder_delete(encoder);

    printf("Video encoder lifecycle tests passed successfully!\n");
}

void test_video_decoder_lifecycle() {
    qcap2_video_decoder_t* decoder = qcap2_video_decoder_new();
    assert(decoder != NULL);

    // Push before start should fail
    qcap2_av_packet_t dummy;
    qcap2_av_packet_init(&dummy);
    qcap2_rcbuffer_t* dummy_rc = qcap2_rcbuffer_new(&dummy, NULL);
    assert(qcap2_video_decoder_push(decoder, dummy_rc) == QCAP_RS_ERROR_GENERAL);
    qcap2_rcbuffer_release(dummy_rc);

    // Start without properties should fail
    assert(qcap2_video_decoder_start(decoder) == QCAP_RS_ERROR_GENERAL);

    // Configure and start
    qcap2_video_encoder_property_t* dec_prop = qcap2_video_encoder_property_new();
    qcap2_video_encoder_property_set_property(dec_prop,
        QCAP_ENCODER_TYPE_SOFTWARE, QCAP_ENCODER_FORMAT_H264,
        QCAP_COLORSPACE_TYPE_I420, 160, 120, 30.0,
        QCAP_RECORD_MODE_VBR, 0, 500, 10, 1, 1);
    qcap2_video_decoder_set_video_property(decoder, dec_prop);
    qcap2_video_encoder_property_delete(dec_prop);

    assert(qcap2_video_decoder_start(decoder) == QCAP_RS_SUCCESSFUL);

    // Double start should be idempotent
    assert(qcap2_video_decoder_start(decoder) == QCAP_RS_SUCCESSFUL);

    // Stop
    assert(qcap2_video_decoder_stop(decoder) == QCAP_RS_SUCCESSFUL);

    // Double stop should be idempotent
    assert(qcap2_video_decoder_stop(decoder) == QCAP_RS_SUCCESSFUL);

    // Delete
    qcap2_video_decoder_delete(decoder);

    printf("Video decoder lifecycle tests passed successfully!\n");
}

void test_video_decoder_h264_integration() {
    // 1. Setup encoder
    qcap2_video_encoder_t* encoder = qcap2_video_encoder_new();
    assert(encoder != NULL);

    qcap2_video_encoder_property_t* enc_prop = qcap2_video_encoder_property_new();
    qcap2_video_encoder_property_set_property(enc_prop,
        QCAP_ENCODER_TYPE_SOFTWARE, QCAP_ENCODER_FORMAT_H264,
        QCAP_COLORSPACE_TYPE_I420, 320, 240, 30.0,
        QCAP_RECORD_MODE_CBR, 0, 1000, 10, 1, 1);
    qcap2_video_encoder_set_video_property(encoder, enc_prop);
    qcap2_video_encoder_property_delete(enc_prop);

    assert(qcap2_video_encoder_start(encoder) == QCAP_RS_SUCCESSFUL);

    // Get SPS/PPS extra data from encoder
    uint8_t* extra = NULL;
    int extra_size = 0;
    qcap2_video_encoder_get_extra_data(encoder, &extra, &extra_size);

    // Push 3 raw gradient frames to encoder to get compressed packets
    std::vector<qcap2_rcbuffer_t*> encoded_packets;
    qcap2_rcbuffer_t* in_rc = nullptr;
    for (int f = 0; f < 3; ++f) {
        if (f == 0) {
            qcap2_av_frame_t* in_frame = new qcap2_av_frame_t;
            qcap2_av_frame_init(in_frame);
            qcap2_av_frame_set_video_property(in_frame, QCAP_COLORSPACE_TYPE_I420, 320, 240);
            assert(qcap2_av_frame_alloc_buffer(in_frame, 16, 1));

            in_rc = qcap2_rcbuffer_new(in_frame, [](PVOID p) {
                qcap2_av_frame_free_buffer((qcap2_av_frame_t*)p);
                delete (qcap2_av_frame_t*)p;
            });
        } else {
            // Pop the recycled input buffer (HPR model)
            qcap2_rcbuffer_t* recycled = nullptr;
            assert(qcap2_video_encoder_pop_input(encoder, &recycled) == QCAP_RS_SUCCESSFUL);
            assert(recycled == in_rc);
        }

        // Fill gradient
        PVOID pData = qcap2_rcbuffer_lock_data(in_rc);
        assert(pData != nullptr);
        qcap2_av_frame_t* frame = (qcap2_av_frame_t*)pData;

        uint8_t* ptrs[4] = { nullptr };
        int strides[4] = { 0 };
        qcap2_av_frame_get_buffer1(frame, ptrs, strides);
        for (int y = 0; y < 240; ++y) {
            for (int x = 0; x < 320; ++x) {
                ptrs[0][y * strides[0] + x] = (uint8_t)((y + x + f * 10) % 256);
            }
        }
        if (ptrs[1]) memset(ptrs[1], 128, strides[1] * 120);
        if (ptrs[2]) memset(ptrs[2], 128, strides[2] * 120);

        qcap2_av_frame_set_pts(frame, f * 3000);
        qcap2_rcbuffer_unlock_data(in_rc);

        assert(qcap2_video_encoder_push(encoder, in_rc) == QCAP_RS_SUCCESSFUL);
        qcap2_rcbuffer_release(in_rc);

        qcap2_rcbuffer_t* out_pkt_rc = NULL;
        assert(qcap2_video_encoder_pop(encoder, &out_pkt_rc) == QCAP_RS_SUCCESSFUL);
        assert(out_pkt_rc != NULL);
        encoded_packets.push_back(out_pkt_rc);
    }

    // 2. Setup decoder (keep encoder alive to allow recycling packets to it)
    qcap2_video_decoder_t* decoder = qcap2_video_decoder_new();
    assert(decoder != NULL);

    qcap2_video_encoder_property_t* dec_prop = qcap2_video_encoder_property_new();
    qcap2_video_encoder_property_set_property(dec_prop,
        QCAP_ENCODER_TYPE_SOFTWARE, QCAP_ENCODER_FORMAT_H264,
        QCAP_COLORSPACE_TYPE_I420, 320, 240, 30.0,
        QCAP_RECORD_MODE_CBR, 0, 1000, 10, 1, 1);
    qcap2_video_decoder_set_video_property(decoder, dec_prop);
    qcap2_video_encoder_property_delete(dec_prop);

    if (extra && extra_size > 0) {
        qcap2_video_decoder_set_extra_data(decoder, extra, extra_size);
    }

    assert(qcap2_video_decoder_start(decoder) == QCAP_RS_SUCCESSFUL);

    // Push encoded packets to decoder and recycle them
    for (auto pkt_rc : encoded_packets) {
        assert(qcap2_video_decoder_push(decoder, pkt_rc) == QCAP_RS_SUCCESSFUL);

        // Pop recycled packet from decoder (HPR model)
        qcap2_rcbuffer_t* recycled_pkt = nullptr;
        assert(qcap2_video_decoder_pop_input(decoder, &recycled_pkt) == QCAP_RS_SUCCESSFUL);
        assert(recycled_pkt == pkt_rc);

        // Recycle it back to the encoder's output queue (PPR model)
        assert(qcap2_video_encoder_push_output(encoder, recycled_pkt) == QCAP_RS_SUCCESSFUL);

        // Release references
        qcap2_rcbuffer_release(recycled_pkt);
        qcap2_rcbuffer_release(pkt_rc);
    }
    encoded_packets.clear();

    // Now stop and delete the encoder safely
    qcap2_video_encoder_stop(encoder);
    qcap2_video_encoder_delete(encoder);

    // Pop and verify decoded frames
    for (int f = 0; f < 3; ++f) {
        qcap2_rcbuffer_t* decoded_rc = NULL;
        assert(qcap2_video_decoder_pop(decoder, &decoded_rc) == QCAP_RS_SUCCESSFUL);
        assert(decoded_rc != NULL);

        PVOID out_data = qcap2_rcbuffer_lock_data(decoded_rc);
        assert(out_data != NULL);

        qcap2_av_frame_t* out_frame = (qcap2_av_frame_t*)out_data;
        ULONG colorspace = 0, w = 0, h = 0;
        qcap2_av_frame_get_video_property(out_frame, &colorspace, &w, &h);

        assert(colorspace == QCAP_COLORSPACE_TYPE_I420);
        assert(w == 320);
        assert(h == 240);

        int64_t pts = 0;
        qcap2_av_frame_get_pts(out_frame, &pts);
        assert(pts == f);

        qcap2_rcbuffer_unlock_data(decoded_rc);

        // Recycle the decoder output raw frame (PPR model)
        assert(qcap2_video_decoder_push_output(decoder, decoded_rc) == QCAP_RS_SUCCESSFUL);
        qcap2_rcbuffer_release(decoded_rc);
    }

    qcap2_video_decoder_stop(decoder);
    qcap2_video_decoder_delete(decoder);

    printf("Video decoder H.264 integration tests passed successfully!\n");
}
void test_packet_pool_basic() {
    qcap2_packet_pool_t* pool = qcap2_packet_pool_new();
    assert(pool != NULL);

    qcap2_packet_pool_set_packet_count(pool, 3);
    assert(qcap2_packet_pool_start(pool) == QCAP_RS_SUCCESSFUL);

    qcap2_rcbuffer_t* buf1 = NULL;
    assert(qcap2_packet_pool_get_buffer(pool, 1024, &buf1) == QCAP_RS_SUCCESSFUL);
    assert(buf1 != NULL);

    PVOID data = qcap2_rcbuffer_lock_data(buf1);
    assert(data != NULL);
    qcap2_av_packet_t* pkt = (qcap2_av_packet_t*)data;

    uint8_t* pBuffer = NULL;
    int nSize = 0;
    qcap2_av_packet_get_buffer(pkt, &pBuffer, &nSize);
    assert(pBuffer != NULL);
    assert(nSize >= 1024);

    qcap2_rcbuffer_unlock_data(buf1);
    qcap2_rcbuffer_release(buf1);

    assert(qcap2_packet_pool_stop(pool) == QCAP_RS_SUCCESSFUL);
    qcap2_packet_pool_delete(pool);

    printf("Packet pool basic tests passed successfully!\n");
}

void test_packet_pool_recycling_and_resizing() {
    qcap2_packet_pool_t* pool = qcap2_packet_pool_new();
    assert(pool != NULL);

    qcap2_packet_pool_set_packet_count(pool, 2);
    assert(qcap2_packet_pool_start(pool) == QCAP_RS_SUCCESSFUL);

    qcap2_rcbuffer_t* buf1 = NULL;
    assert(qcap2_packet_pool_get_buffer(pool, 100, &buf1) == QCAP_RS_SUCCESSFUL);

    qcap2_rcbuffer_t* buf2 = NULL;
    assert(qcap2_packet_pool_get_buffer(pool, 200, &buf2) == QCAP_RS_SUCCESSFUL);

    // No more buffers
    qcap2_rcbuffer_t* buf3 = NULL;
    assert(qcap2_packet_pool_get_buffer(pool, 300, &buf3) == QCAP_RS_ERROR_GENERAL);

    // Release and recycle
    qcap2_rcbuffer_release(buf1);

    qcap2_rcbuffer_t* buf4 = NULL;
    // Request larger buffer on the recycled one
    assert(qcap2_packet_pool_get_buffer(pool, 500, &buf4) == QCAP_RS_SUCCESSFUL);
    assert(buf4 == buf1); // should be recycled

    PVOID data = qcap2_rcbuffer_lock_data(buf4);
    qcap2_av_packet_t* pkt = (qcap2_av_packet_t*)data;
    uint8_t* pBuffer = NULL;
    int nSize = 0;
    qcap2_av_packet_get_buffer(pkt, &pBuffer, &nSize);
    assert(pBuffer != NULL);
    assert(nSize >= 500); // verify resized

    qcap2_rcbuffer_unlock_data(buf4);

    qcap2_rcbuffer_release(buf4);
    qcap2_rcbuffer_release(buf2);

    qcap2_packet_pool_delete(pool);

    printf("Packet pool recycling and resizing tests passed successfully!\n");
}

struct AsyncAencContext {
    qcap2_audio_encoder_t* aenc;
    qcap2_event_t* evt;
    std::atomic<int> packet_count;
};

static QRETURN on_async_aenc_event(PVOID pUserData) {
    AsyncAencContext* ctx = (AsyncAencContext*)pUserData;
    if (ctx) {
        uint64_t count = 0;
        if (qcap2_event_read(ctx->evt, &count) == QCAP_RS_SUCCESSFUL) {
            for (uint64_t i = 0; i < count; ++i) {
                qcap2_rcbuffer_t* pkt = nullptr;
                if (qcap2_audio_encoder_pop(ctx->aenc, &pkt) == QCAP_RS_SUCCESSFUL) {
                    if (pkt) {
                        ctx->packet_count++;
                        qcap2_audio_encoder_push_output(ctx->aenc, pkt);
                        qcap2_rcbuffer_release(pkt);
                    }
                }
            }
        }
    }
    return QCAP_RT_OK;
}

struct AudioFrameBuffer {
    qcap2_av_frame_t frame;
    std::vector<int16_t> data;
};

void test_audio_encoder_sync_async() {
    qcap2_audio_encoder_t* aenc = qcap2_audio_encoder_new();
    assert(aenc != nullptr);

    // Configure audio encoder: PCM, Stereo, 16-bit, 44100Hz, 1411200bps
    qcap2_audio_encoder_property_t* aprop = qcap2_audio_encoder_property_new();
    assert(aprop != nullptr);
    qcap2_audio_encoder_property_set_property1(aprop,
        0, QCAP_ENCODER_FORMAT_PCM, 2, 16, 44100, 1411200
    );
    qcap2_audio_encoder_set_audio_property(aenc, aprop);
    qcap2_audio_encoder_property_delete(aprop);

    // ----------------------------------------------------
    // Part 1: Test Synchronous Event Handling (wait_count)
    // ----------------------------------------------------
    qcap2_event_t* sync_evt = qcap2_event_new();
    assert(sync_evt != nullptr);
    assert(qcap2_event_start(sync_evt) == QCAP_RS_SUCCESSFUL);

    qcap2_audio_encoder_set_event(aenc, sync_evt);
    assert(qcap2_audio_encoder_start(aenc) == QCAP_RS_SUCCESSFUL);

    // Push 5 frames of S16 PCM (1024 samples, stereo)
    qcap2_rcbuffer_t* in_rc = nullptr;
    int sync_packets_received = 0;

    for (int f = 0; f < 5; ++f) {
        if (f == 0) {
            AudioFrameBuffer* af = new AudioFrameBuffer();
            qcap2_av_frame_init(&af->frame);
            qcap2_av_frame_set_audio_property(&af->frame, 2, 1, 44100, 1024);
            af->data.resize(1024 * 2);
            for (size_t i = 0; i < af->data.size(); ++i) {
                af->data[i] = (int16_t)(i % 1000);
            }
            qcap2_av_frame_set_buffer(&af->frame, (uint8_t*)af->data.data(), 1024 * 2 * sizeof(int16_t));
            qcap2_av_frame_set_pts(&af->frame, f * 1024);

            in_rc = qcap2_rcbuffer_new(&af->frame, [](PVOID p) {
                AudioFrameBuffer* af = qcap2_container_of((qcap2_av_frame_t*)p, AudioFrameBuffer, frame);
                delete af;
            });
        } else {
            // HPR recycle check
            qcap2_rcbuffer_t* recycled = nullptr;
            assert(qcap2_audio_encoder_pop_input(aenc, &recycled) == QCAP_RS_SUCCESSFUL);
            assert(recycled == in_rc);

            qcap2_av_frame_t* frame = (qcap2_av_frame_t*)qcap2_rcbuffer_lock_data(in_rc);
            assert(frame != nullptr);
            AudioFrameBuffer* af = qcap2_container_of(frame, AudioFrameBuffer, frame);
            qcap2_av_frame_set_pts(&af->frame, f * 1024);
            qcap2_rcbuffer_unlock_data(in_rc);
        }

        assert(qcap2_audio_encoder_push(aenc, in_rc) == QCAP_RS_SUCCESSFUL);
        qcap2_rcbuffer_release(in_rc);
    }

    // Drain the sync packets using qcap2_event_wait_count
    uint64_t sync_count = 0;
    if (qcap2_event_wait_count(sync_evt, &sync_count) == QCAP_RS_SUCCESSFUL) {
        for (uint64_t i = 0; i < sync_count; ++i) {
            qcap2_rcbuffer_t* pkt = nullptr;
            if (qcap2_audio_encoder_pop(aenc, &pkt) == QCAP_RS_SUCCESSFUL) {
                if (pkt) {
                    sync_packets_received++;
                    qcap2_audio_encoder_push_output(aenc, pkt);
                    qcap2_rcbuffer_release(pkt);
                }
            }
        }
    }
    assert(sync_packets_received > 0);

    // Stop the encoder after retrieving packets
    assert(qcap2_audio_encoder_stop(aenc) == QCAP_RS_SUCCESSFUL);

    qcap2_audio_encoder_set_event(aenc, nullptr);
    assert(qcap2_event_stop(sync_evt) == QCAP_RS_SUCCESSFUL);
    qcap2_event_delete(sync_evt);

    // ----------------------------------------------------
    // Part 2: Test Asynchronous Event Handling (event_handlers)
    // ----------------------------------------------------
    qcap2_event_t* async_evt = qcap2_event_new();
    assert(async_evt != nullptr);
    assert(qcap2_event_start(async_evt) == QCAP_RS_SUCCESSFUL);

    qcap2_event_handlers_t* handlers = qcap2_event_handlers_new();
    assert(handlers != nullptr);
    assert(qcap2_event_handlers_start(handlers) == QCAP_RS_SUCCESSFUL);

    uintptr_t handle = 0;
    assert(qcap2_event_get_native_handle(async_evt, &handle) == QCAP_RS_SUCCESSFUL);

    AsyncAencContext async_ctx;
    async_ctx.aenc = aenc;
    async_ctx.evt = async_evt;
    async_ctx.packet_count = 0;

    assert(qcap2_event_handlers_add_handler(handlers, handle, on_async_aenc_event, &async_ctx) == QCAP_RS_SUCCESSFUL);

    qcap2_audio_encoder_set_event(aenc, async_evt);
    assert(qcap2_audio_encoder_start(aenc) == QCAP_RS_SUCCESSFUL);

    // Push 5 frames
    in_rc = nullptr;
    for (int f = 0; f < 5; ++f) {
        if (f == 0) {
            AudioFrameBuffer* af = new AudioFrameBuffer();
            qcap2_av_frame_init(&af->frame);
            qcap2_av_frame_set_audio_property(&af->frame, 2, 1, 44100, 1024);
            af->data.resize(1024 * 2);
            for (size_t i = 0; i < af->data.size(); ++i) {
                af->data[i] = (int16_t)(i % 1000);
            }
            qcap2_av_frame_set_buffer(&af->frame, (uint8_t*)af->data.data(), 1024 * 2 * sizeof(int16_t));
            qcap2_av_frame_set_pts(&af->frame, f * 1024);

            in_rc = qcap2_rcbuffer_new(&af->frame, [](PVOID p) {
                AudioFrameBuffer* af = qcap2_container_of((qcap2_av_frame_t*)p, AudioFrameBuffer, frame);
                delete af;
            });
        } else {
            qcap2_rcbuffer_t* recycled = nullptr;
            assert(qcap2_audio_encoder_pop_input(aenc, &recycled) == QCAP_RS_SUCCESSFUL);
            assert(recycled == in_rc);

            qcap2_av_frame_t* frame = (qcap2_av_frame_t*)qcap2_rcbuffer_lock_data(in_rc);
            assert(frame != nullptr);
            AudioFrameBuffer* af = qcap2_container_of(frame, AudioFrameBuffer, frame);
            qcap2_av_frame_set_pts(&af->frame, f * 1024);
            qcap2_rcbuffer_unlock_data(in_rc);
        }

        assert(qcap2_audio_encoder_push(aenc, in_rc) == QCAP_RS_SUCCESSFUL);
        qcap2_rcbuffer_release(in_rc);
    }

    // Wait a short time for async event handlers thread to dispatch any pending callbacks
    for (int i = 0; i < 20 && async_ctx.packet_count < sync_packets_received; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    assert(async_ctx.packet_count > 0);

    // Stop encoder
    assert(qcap2_audio_encoder_stop(aenc) == QCAP_RS_SUCCESSFUL);

    // Clean up async handlers
    assert(qcap2_event_handlers_remove_handler(handlers, handle) == QCAP_RS_SUCCESSFUL);
    assert(qcap2_event_handlers_stop(handlers) == QCAP_RS_SUCCESSFUL);
    qcap2_event_handlers_delete(handlers);

    qcap2_audio_encoder_set_event(aenc, nullptr);
    assert(qcap2_event_stop(async_evt) == QCAP_RS_SUCCESSFUL);
    qcap2_event_delete(async_evt);

    qcap2_audio_encoder_delete(aenc);

    printf("Audio encoder sync and async event tests passed successfully!\n");
}

int main() {
    test_audio_resampler();
    test_video_scaler_direct();
    test_video_scaler_crop();
    test_video_scaler_buffer_pool();
    test_video_scaler_filter_graph();
    test_frame_pool_video_basic();
    test_frame_pool_video_recycling();
    test_frame_pool_audio();
    test_frame_pool_video_with_border();
    test_frame_pool_lifecycle();
    test_video_encoder_h264_basic();
    test_video_encoder_bgr24_input();
    test_video_encoder_property_roundtrip();
    test_video_encoder_idr_request();
    test_video_encoder_lifecycle();
    test_video_decoder_lifecycle();
    test_video_decoder_h264_integration();
    test_packet_pool_basic();
    test_packet_pool_recycling_and_resizing();
    test_audio_encoder_sync_async();

    printf("All processing unit tests passed successfully!\n");
    return 0;
}
