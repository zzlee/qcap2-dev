#include "qcap2.formats.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

void test_qcap2_input_format() {
    qcap2_input_format_t* fmt = qcap2_input_format_new();
    assert(fmt != NULL);
    qcap2_input_format_delete(fmt);
}

void test_qcap2_video_format() {
    qcap2_video_format_t* fmt = qcap2_video_format_new();
    assert(fmt != NULL);

    qcap2_video_format_set_property(fmt, 1, 1920, 1080, TRUE, 60.0);
    ULONG color, w, h;
    BOOL inter;
    double fps;
    qcap2_video_format_get_property(fmt, &color, &w, &h, &inter, &fps);
    assert(color == 1);
    assert(w == 1920);
    assert(h == 1080);
    assert(inter == TRUE);
    assert(fps == 60.0);

    qcap2_video_format_set_input(fmt, 5);
    ULONG input;
    qcap2_video_format_get_input(fmt, &input);
    assert(input == 5);

    qcap2_video_format_delete(fmt);
}

void test_qcap2_audio_format() {
    qcap2_audio_format_t* fmt = qcap2_audio_format_new();
    assert(fmt != NULL);

    qcap2_audio_format_set_property(fmt, 2, 16, 48000);
    ULONG ch, bps, freq;
    qcap2_audio_format_get_property(fmt, &ch, &bps, &freq);
    assert(ch == 2);
    assert(bps == 16);
    assert(freq == 48000);

    qcap2_audio_format_set_input(fmt, 3);
    ULONG input;
    qcap2_audio_format_get_input(fmt, &input);
    assert(input == 3);

    qcap2_audio_format_delete(fmt);
}

void test_qcap2_video_encoder_property() {
    qcap2_video_encoder_property_t* props = qcap2_video_encoder_property_new();
    assert(props != NULL);

    qcap2_video_encoder_property_set_property(props, 1, 2, 3, 1280, 720, 30.0, 4, 5, 5000, 30, 16, 9);
    ULONG encType, encFmt, color, w, h, recMode, qual, br, gop, arX, arY;
    double fps;
    qcap2_video_encoder_property_get_property(props, &encType, &encFmt, &color, &w, &h, &fps, &recMode, &qual, &br, &gop, &arX, &arY);
    assert(encType == 1);
    assert(encFmt == 2);
    assert(color == 3);
    assert(w == 1280);
    assert(h == 720);
    assert(fps == 30.0);
    assert(recMode == 4);
    assert(qual == 5);
    assert(br == 5000);
    assert(gop == 30);
    assert(arX == 16);
    assert(arY == 9);

    qcap2_video_encoder_property_set_cbr_variation(props, 10);
    ULONG cbrVar;
    qcap2_video_encoder_property_get_cbr_variation(props, &cbrVar);
    assert(cbrVar == 10);

    qcap2_video_encoder_property_delete(props);
}

void test_qcap2_audio_encoder_property() {
    qcap2_audio_encoder_property_t* props = qcap2_audio_encoder_property_new();
    assert(props != NULL);

    qcap2_audio_encoder_property_set_property(props, 1, 2, 2, 16, 44100);
    ULONG encType, encFmt, ch, bps, freq;
    qcap2_audio_encoder_property_get_property(props, &encType, &encFmt, &ch, &bps, &freq);
    assert(encType == 1);
    assert(encFmt == 2);
    assert(ch == 2);
    assert(bps == 16);
    assert(freq == 44100);

    qcap2_audio_encoder_property_delete(props);
}

void test_qcap2_video_encoder_dynamic_property() {
    qcap2_video_encoder_dynamic_property_t* props = qcap2_video_encoder_dynamic_property_new();
    assert(props != NULL);

    qcap2_video_encoder_dynamic_set_property(props, 1, 2, 3000, 15);
    ULONG recMode, qual, br, gop;
    qcap2_video_encoder_dynamic_get_property(props, &recMode, &qual, &br, &gop);
    assert(recMode == 1);
    assert(qual == 2);
    assert(br == 3000);
    assert(gop == 15);

    qcap2_video_encoder_dynamic_property_delete(props);
}

void test_qcap2_program_info() {
    qcap2_program_info_t* info = qcap2_program_info_new();
    assert(info != NULL);

    qcap2_program_info_set_id(info, 123);
    assert(qcap2_program_info_get_id(info) == 123);

    qcap2_program_info_set_video_source_count(info, 2);
    assert(qcap2_program_info_get_video_source_count(info) == 2);

    qcap2_program_info_set_video_source_index(info, 0, 10);
    qcap2_program_info_set_video_source_index(info, 1, 20);
    assert(qcap2_program_info_get_video_source_index(info, 0) == 10);
    assert(qcap2_program_info_get_video_source_index(info, 1) == 20);

    qcap2_program_info_set_metadata(info, "author", "qcap");
    const char* val = qcap2_program_info_get_metadata(info, "author");
    assert(val != NULL);
    assert(strcmp(val, "qcap") == 0);

    qcap2_program_info_delete(info);
}

int main() {
    test_qcap2_input_format();
    test_qcap2_video_format();
    test_qcap2_audio_format();
    test_qcap2_video_encoder_property();
    test_qcap2_audio_encoder_property();
    test_qcap2_video_encoder_dynamic_property();
    test_qcap2_program_info();

    printf("All test_qcap2_formats passed!\n");
    return 0;
}
