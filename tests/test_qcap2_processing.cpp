#include "qcap2.processing.h"
#include "qcap2.buffer.h"
#include "qcap2.formats.h"
#include <stdio.h>
#include <assert.h>
#include <vector>
#include <string.h>

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
    qcap2_rcbuffer_release(out_rc);

    qcap2_rcbuffer_release(in_rc);

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
    qcap2_rcbuffer_release(out_rc);

    qcap2_rcbuffer_release(in_rc);

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
    qcap2_rcbuffer_release(out_rc);
    qcap2_rcbuffer_release(in_rc);

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

    // Pop popped frame
    qcap2_rcbuffer_t* out_rc = NULL;
    assert(qcap2_video_scaler_pop(scaler, &out_rc) == QCAP_RS_SUCCESSFUL);
    assert(out_rc != NULL);

    // Check that out_rc is indeed pool_rc1 or pool_rc2!
    assert(out_rc == pool_rc1 || out_rc == pool_rc2);

    qcap2_rcbuffer_release(out_rc);
    qcap2_rcbuffer_release(in_rc);

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
    qcap2_rcbuffer_release(out_rc);
    qcap2_rcbuffer_release(in_rc);

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

    printf("All processing unit tests passed successfully!\n");
    return 0;
}
