#include "qcap2.utils.h"
#include "qcap2.buffer.h"
#include "qcap2.formats.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>

void test_utils() {
    int64_t t1 = qcap2_get_time();
    int64_t t2 = qcap2_get_time();
    assert(t2 >= t1);

    qcap2_build_config_t config;
    memset(&config, 0, sizeof(config));
    qcap2_get_build_config(&config);
    assert(config.major >= 0);

    qcap2_rational_t r = qcap2_d2q(1.5, 100);
    assert(r.num == 3);
    assert(r.den == 2);

    qcap2_debug_set(0, 42);
    assert(qcap2_debug_get(0) == 42);

    assert(qcap2_debug_fetch_add(0, 5) == 42);
    assert(qcap2_debug_get(0) == 47);

    assert(qcap2_debug_fetch_sub(0, 7) == 47);
    assert(qcap2_debug_get(0) == 40);

    const char* fmt_name = qcap2_get_pix_fmt_name(0);
    assert(fmt_name != NULL);
    assert(strcmp(fmt_name, "PIX_FMT_RGB24") == 0);

    const char* sample_fmt = qcap2_get_sample_fmt_name(0);
    assert(sample_fmt != NULL);
    assert(strcmp(sample_fmt, "SAMPLE_FMT_S16") == 0);

    // Test print functions
    qcap2_av_frame_t frame;
    qcap2_av_frame_init(&frame);
    qcap2_av_frame_set_video_property(&frame, 1, 640, 480);
    qcap2_av_frame_set_audio_property(&frame, 2, 1, 48000, 1024);
    qcap2_av_frame_set_pts(&frame, 1000);

    uint8_t* dummy_buffer = (uint8_t*)malloc(640*480*4);
    qcap2_av_frame_set_buffer(&frame, dummy_buffer, 640*4);

    qcap2_rcbuffer_t* pRCBuffer = qcap2_rcbuffer_new(&frame, NULL);

    assert(qcap2_print_video_frame_info(pRCBuffer, "TestVideo") == QCAP_RS_SUCCESSFUL);
    assert(qcap2_print_audio_sample_info(pRCBuffer, "TestAudio") == QCAP_RS_SUCCESSFUL);
    assert(qcap2_print_packet_info(pRCBuffer, "TestPacket") == QCAP_RS_SUCCESSFUL);

    assert(qcap2_fill_video_test_pattern(pRCBuffer, 128) == QCAP_RS_SUCCESSFUL);

    uint8_t* pBuf = NULL;
    int stride = 0;
    qcap2_av_frame_get_buffer(&frame, &pBuf, &stride);
    assert(pBuf != NULL);
    assert(pBuf[0] == 128);

    assert(qcap2_save_raw_video_frame(pRCBuffer, "test_output") == QCAP_RS_SUCCESSFUL);
    FILE* fp = fopen("test_output_640x480.raw", "rb");
    assert(fp != NULL);
    fclose(fp);
    remove("test_output_640x480.raw");

    qcap2_rcbuffer_release(pRCBuffer);
    free(dummy_buffer);

    // Add simple test to test picture loading
    qcap2_av_frame_t frame2;
    qcap2_av_frame_init(&frame2);
    qcap2_rcbuffer_t* pRCBuffer2 = qcap2_rcbuffer_new(&frame2, NULL);
    QRESULT result = qcap2_load_picture(pRCBuffer2, "test_pic.jpg");
    assert(result == QCAP_RS_ERROR_GENERAL); // Should fail as test_pic.jpg doesn't exist
    qcap2_rcbuffer_release(pRCBuffer2);

    qcap2_video_format_t* pFmt = qcap2_video_format_new();
    result = qcap2_get_picture_info("test_pic.jpg", pFmt);
    assert(result == QCAP_RS_ERROR_GENERAL); // Should fail as test_pic.jpg doesn't exist
    qcap2_video_format_delete(pFmt);

    printf("All test_utils passed!\n");
}

int main() {
    test_utils();
    return 0;
}
