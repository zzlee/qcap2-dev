#include "qcap2.devices.h"
#include "qcap2.buffer.h"
#include "qcap2.formats.h"
#include "qcap2.processing.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <cstdio>
#include <cassert>
#include <fstream>
#include <cstring>

int main() {
    std::cout << "--- Starting QCAP2 Demuxer -> Muxer Integration Test ---\n";

    // 1. Generate a test fixture file with both video and audio streams
    std::cout << "Generating test fixture input file...\n";
    if (system("which ffmpeg >/dev/null") != 0) { std::cout << "ffmpeg not found\n"; return 0; }
    int sys_ret = system("ffmpeg -f lavfi -i testsrc=duration=1:size=320x240:rate=10 -f lavfi -i sine=duration=1:frequency=1000 -c:v h264 -c:a aac -y input_fixture.mp4 >/dev/null 2>&1");
    if (sys_ret != 0) {
        std::cerr << "Failed to generate input_fixture.mp4\n";
        return 1;
    }

    // 2. Instantiate and configure the demuxer
    std::cout << "Initializing demuxer...\n";
    qcap2_demuxer_t* demuxer = qcap2_demuxer_new();
    assert(demuxer != nullptr);

    qcap2_demuxer_set_url(demuxer, "input_fixture.mp4");
    qcap2_demuxer_set_live_source(demuxer, true); // Read at maximum speed for testing

    QRESULT ret = qcap2_demuxer_start(demuxer);
    assert(ret == QCAP_RS_SUCCESSFUL);

    int v_encoders = qcap2_demuxer_get_video_encoder_count(demuxer);
    int a_encoders = qcap2_demuxer_get_audio_encoder_count(demuxer);
    std::cout << "  Demuxer video streams: " << v_encoders << ", audio streams: " << a_encoders << "\n";
    assert(v_encoders == 1 && "Input fixture does not have exactly 1 video stream!");
    assert(a_encoders == 1 && "Input fixture does not have exactly 1 audio stream!");

    qcap2_video_encoder_t* venc = qcap2_demuxer_get_video_encoder(demuxer, 0);
    qcap2_audio_encoder_t* aenc = qcap2_demuxer_get_audio_encoder(demuxer, 0);
    assert(venc != nullptr && aenc != nullptr);

    // Start encoder queues
    qcap2_video_encoder_start(venc);
    qcap2_audio_encoder_start(aenc);

    // 3. Instantiate and configure the muxer
    std::cout << "Initializing muxer...\n";
    qcap2_muxer_t* muxer = qcap2_muxer_new();
    assert(muxer != nullptr);

    const char* output_filename = "output_combined.mp4";
    std::remove(output_filename);

    qcap2_muxer_set_type(muxer, QCAP2_MUXER_TYPE_DEFAULT);
    qcap2_muxer_set_endpoint(muxer, output_filename, 0);

    // Map Video Decoder stream using demuxer's Video Encoder properties
    qcap2_video_decoder_t* vdec = qcap2_muxer_get_video_decoder(muxer, 0);
    assert(vdec != nullptr);

    qcap2_video_encoder_property_t* vprop = qcap2_video_encoder_property_new();
    qcap2_video_encoder_get_video_property(venc, vprop);
    qcap2_video_decoder_set_video_property(vdec, vprop);
    qcap2_video_encoder_property_delete(vprop);

    uint8_t* vextra = nullptr;
    int vextra_size = 0;
    qcap2_video_encoder_get_extra_data(venc, &vextra, &vextra_size);
    if (vextra && vextra_size > 0) {
        std::cout << "  Propagating video extra-data (size: " << vextra_size << ")\n";
        qcap2_video_decoder_set_extra_data(vdec, vextra, vextra_size);
    }

    // Map Audio Decoder stream using demuxer's Audio Encoder properties
    qcap2_audio_decoder_t* adec = qcap2_muxer_get_audio_decoder(muxer, 0);
    assert(adec != nullptr);

    qcap2_audio_encoder_property_t* aprop = qcap2_audio_encoder_property_new();
    qcap2_audio_encoder_get_audio_property(aenc, aprop);
    qcap2_audio_decoder_set_audio_property(adec, aprop);
    qcap2_audio_encoder_property_delete(aprop);

    uint8_t* aextra = nullptr;
    int aextra_size = 0;
    qcap2_audio_encoder_get_extra_data(aenc, &aextra, &aextra_size);
    if (aextra && aextra_size > 0) {
        std::cout << "  Propagating audio extra-data (size: " << aextra_size << ")\n";
        qcap2_audio_decoder_set_extra_data(adec, aextra, aextra_size);
    }

    // Muxer Program setup
    qcap2_program_info_t* prog = qcap2_program_info_new();
    assert(prog != nullptr);
    qcap2_program_info_set_video_decoder_count(prog, 1);
    qcap2_program_info_set_video_decoder_index(prog, 0, 0);
    qcap2_program_info_set_audio_decoder_count(prog, 1);
    qcap2_program_info_set_audio_decoder_index(prog, 0, 0);
    qcap2_muxer_add_program_info(muxer, prog);

    // 4. Start pipeline
    std::cout << "Starting demuxer -> muxer pipeline...\n";
    ret = qcap2_muxer_start(muxer);
    assert(ret == QCAP_RS_SUCCESSFUL);

    qcap2_demuxer_play(demuxer);

    // 5. Transfer packets from demuxer to muxer
    std::cout << "Streaming packets...\n";
    int video_packets_streamed = 0;
    int audio_packets_streamed = 0;

    bool video_eof = false;
    bool audio_eof = false;

    while (!video_eof || !audio_eof) {
        if (!video_eof) {
            qcap2_rcbuffer_t* vbuf = nullptr;
            QRESULT pop_res = qcap2_video_encoder_pop(venc, &vbuf);
            if (pop_res == QCAP_RS_SUCCESSFUL && vbuf) {
                ret = qcap2_video_decoder_push(vdec, vbuf);
                assert(ret == QCAP_RS_SUCCESSFUL);
                qcap2_rcbuffer_release(vbuf);
                video_packets_streamed++;
            } else {
                video_eof = true;
            }
        }

        if (!audio_eof) {
            qcap2_rcbuffer_t* abuf = nullptr;
            QRESULT pop_res = qcap2_audio_encoder_pop(aenc, &abuf);
            if (pop_res == QCAP_RS_SUCCESSFUL && abuf) {
                ret = qcap2_audio_decoder_push(adec, abuf);
                assert(ret == QCAP_RS_SUCCESSFUL);
                qcap2_rcbuffer_release(abuf);
                audio_packets_streamed++;
            } else {
                audio_eof = true;
            }
        }
    }

    std::cout << "Streamed " << video_packets_streamed << " video packets and " 
              << audio_packets_streamed << " audio packets.\n";

    assert(video_packets_streamed >= 10 && "Failed to stream enough video packets!");
    assert(audio_packets_streamed >= 10 && "Failed to stream enough audio packets!");

    // 6. Stop and clean up
    std::cout << "Stopping pipeline...\n";
    qcap2_muxer_stop(muxer);
    qcap2_demuxer_stop(demuxer);

    qcap2_muxer_delete(muxer);
    qcap2_demuxer_delete(demuxer);

    // 7. Verify output file structures
    std::cout << "Verifying produced output file properties...\n";
    std::ifstream f(output_filename);
    assert(f.good() && "Combined output file was not created!");
    f.close();

    // Verify video stream codec is H.264, width=320, height=240
    if (system("which ffprobe >/dev/null") != 0) { std::cout << "ffprobe not found\n"; return 0; }
    int probe_ret = system("ffprobe -v error -select_streams v:0 -show_entries stream=codec_name,width,height -of default=noprint_wrappers=1 output_combined.mp4 > video_probe.txt");
    assert(probe_ret == 0 && "ffprobe failed on video stream!");

    std::ifstream video_in("video_probe.txt");
    std::string line;
    bool has_codec = false;
    bool has_width = false;
    bool has_height = false;
    while (std::getline(video_in, line)) {
        std::cout << "  video probe entry: " << line << "\n";
        if (line == "codec_name=h264") has_codec = true;
        if (line == "width=320") has_width = true;
        if (line == "height=240") has_height = true;
    }
    video_in.close();
    std::remove("video_probe.txt");

    assert(has_codec && "Muxed video codec is not h264!");
    assert(has_width && "Muxed video width is not 320!");
    assert(has_height && "Muxed video height is not 240!");

    // Verify audio stream codec is AAC, channels=1 (matching fixture)
    probe_ret = system("ffprobe -v error -select_streams a:0 -show_entries stream=codec_name,channels -of default=noprint_wrappers=1 output_combined.mp4 > audio_probe.txt");
    assert(probe_ret == 0 && "ffprobe failed on audio stream!");

    std::ifstream audio_in("audio_probe.txt");
    bool has_audio_codec = false;
    bool has_audio_channels = false;
    while (std::getline(audio_in, line)) {
        std::cout << "  audio probe entry: " << line << "\n";
        if (line == "codec_name=aac") has_audio_codec = true;
        if (line == "channels=1") has_audio_channels = true;
    }
    audio_in.close();
    std::remove("audio_probe.txt");

    assert(has_audio_codec && "Muxed audio codec is not aac!");
    assert(has_audio_channels && "Muxed audio channels mismatch!");

    // Clean up files
    std::remove("input_fixture.mp4");
    std::remove(output_filename);

    std::cout << "--- Combined Demuxer -> Muxer Integration Test Passed! ---\n";
    return 0;
}
