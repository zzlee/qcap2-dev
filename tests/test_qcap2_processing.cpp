#include "qcap2.processing.h"
#include "qcap2.buffer.h"
#include <stdio.h>
#include <assert.h>
#include <vector>
#include <string.h>

int main() {
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
    // Calculated output size for 1024 samples (44100Hz -> 48000Hz) should be around 1114 samples
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
    return 0;
}
