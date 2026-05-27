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

int main() {
    test_audio_resampler();
    test_video_scaler_direct();
    test_video_scaler_crop();
    test_video_scaler_buffer_pool();
    test_video_scaler_filter_graph();

    printf("All processing unit tests passed successfully!\n");
    return 0;
}
