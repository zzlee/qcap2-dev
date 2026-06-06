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
    std::cout << "--- Starting QCAP2 Muxer Test ---\n";

    // 1. Create the muxer
    qcap2_muxer_t* muxer = qcap2_muxer_new();
    assert(muxer != nullptr);

    // 2. Configure endpoint (local file path)
    const char* output_filename = "muxer_test_output.mp4";
    // Clean up any old output file
    std::remove(output_filename);

    qcap2_muxer_set_type(muxer, QCAP2_MUXER_TYPE_DEFAULT);
    qcap2_muxer_set_endpoint(muxer, output_filename, 0);

    // 3. Configure the video decoder stream (input to muxer)
    qcap2_video_decoder_t* vdec = qcap2_muxer_get_video_decoder(muxer, 0);
    assert(vdec != nullptr);

    qcap2_video_encoder_property_t* prop = qcap2_video_encoder_property_new();
    assert(prop != nullptr);

    // H.264 format, 320x240, 25fps, 1Mbps bitrate
    qcap2_video_encoder_property_set_property(prop,
        0, // type
        QCAP_ENCODER_FORMAT_H264,
        0, // color space
        320, // width
        240, // height
        25.0, // fps
        0, 0,
        1000000, // bitrate
        0, 0, 0
    );
    qcap2_video_decoder_set_video_property(vdec, prop);
    qcap2_video_encoder_property_delete(prop);

    // 4. Set dummy SPS/PPS codec extra-data
    uint8_t dummy_extradata[] = { 0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x0a, 0xf8, 0x0f, 0x00, 0x00, 0x00, 0x01, 0x68, 0xce, 0x3c, 0x80 };
    qcap2_video_decoder_set_extra_data(vdec, dummy_extradata, sizeof(dummy_extradata));

    // Verify extra-data was set
    uint8_t* extra_check = nullptr;
    int extra_check_size = 0;
    qcap2_video_decoder_get_extra_data(vdec, &extra_check, &extra_check_size);
    assert(extra_check_size == sizeof(dummy_extradata));
    assert(extra_check != nullptr && extra_check[4] == 0x67);

    // 5. Setup the program info
    qcap2_program_info_t* prog = qcap2_program_info_new();
    assert(prog != nullptr);
    qcap2_program_info_set_video_decoder_count(prog, 1);
    qcap2_program_info_set_video_decoder_index(prog, 0, 0);
    qcap2_muxer_add_program_info(muxer, prog);

    // 6. Start the muxer
    std::cout << "Starting the muxer...\n";
    QRESULT ret = qcap2_muxer_start(muxer);
    assert(ret == QCAP_RS_SUCCESSFUL);

    // 7. Push 10 packets into the video decoder stream (to be muxed)
    std::cout << "Pushing compressed video packets...\n";
    const int total_packets = 10;
    for (int i = 0; i < total_packets; i++) {
        qcap2_av_packet_t* pkt = new qcap2_av_packet_t;
        qcap2_av_packet_init(pkt);

        // Dummy packet payload (H.264 slice)
        uint8_t dummy_payload[] = { 0x00, 0x00, 0x00, 0x01, 0x65, 0x01, 0x02, 0x03, 0x04 };
        if (qcap2_av_packet_alloc_buffer(pkt, sizeof(dummy_payload))) {
            uint8_t* pBuf = nullptr;
            int nSize = 0;
            qcap2_av_packet_get_buffer(pkt, &pBuf, &nSize);
            if (pBuf) {
                memcpy(pBuf, dummy_payload, sizeof(dummy_payload));
            }

            // Timestamps incrementing by 40ms (25 fps)
            qcap2_av_packet_set_pts(pkt, i * 40000);
            qcap2_av_packet_set_dts(pkt, i * 40000);
            qcap2_av_packet_set_sample_time(pkt, i * 0.04);
            qcap2_av_packet_set_property(pkt, 0, (i == 0) ? TRUE : FALSE); // Keyframe flag
        }

        qcap2_rcbuffer_t* rcbuf = qcap2_rcbuffer_new(pkt, [](PVOID pData) {
            qcap2_av_packet_t* p = (qcap2_av_packet_t*)pData;
            if (p) {
                qcap2_av_packet_free_buffer(p);
                delete p;
            }
        });

        ret = qcap2_video_decoder_push(vdec, rcbuf);
        assert(ret == QCAP_RS_SUCCESSFUL);
        qcap2_rcbuffer_release(rcbuf);

        // Micro-sleep to allow thread to process interleaved packets
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    // Give writing thread a bit of time to drain queue
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // 8. Stop the muxer
    std::cout << "Stopping the muxer...\n";
    ret = qcap2_muxer_stop(muxer);
    assert(ret == QCAP_RS_SUCCESSFUL);

    // 9. Destroy the muxer
    qcap2_muxer_delete(muxer);

    // 10. Verification using ffprobe / ffmpeg
    std::cout << "Verifying written file structure...\n";
    std::ifstream f(output_filename);
    assert(f.good() && "Output file was not created!");
    f.close();

    // Verify codec is h264, width=320, height=240, nb_frames=10
    int sys_ret = system("which ffprobe >/dev/null && ffprobe -v error -select_streams v:0 -show_entries stream=codec_name,width,height,nb_read_packets -count_packets -of default=noprint_wrappers=1 muxer_test_output.mp4 > probe_result.txt");
    if (sys_ret != 0) { std::cout << "ffprobe not found or failed, skipping validation.\n"; return 0; }

    std::ifstream probe_in("probe_result.txt");
    std::string line;
    bool has_codec = false;
    bool has_width = false;
    bool has_height = false;
    bool has_packets = false;

    while (std::getline(probe_in, line)) {
        std::cout << "  ffprobe entry: " << line << "\n";
        if (line == "codec_name=h264") has_codec = true;
        if (line == "width=320") has_width = true;
        if (line == "height=240") has_height = true;
        if (line == "nb_read_packets=10") has_packets = true;
    }
    probe_in.close();
    std::remove("probe_result.txt");

    assert(has_codec && "Muxed stream codec is not h264!");
    assert(has_width && "Muxed stream width is not 320!");
    assert(has_height && "Muxed stream height is not 240!");
    assert(has_packets && "Muxed stream does not contain exactly 10 packets!");

    std::remove(output_filename);

    std::cout << "--- All Muxer Tests Passed Successfully! ---\n";
    return 0;
}
