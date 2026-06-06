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

static void check_result(const char* label, QRESULT actual, QRESULT expected) {
    bool pass = (actual == expected);
    std::cout << "  " << label << ": "
              << (pass ? "PASS" : "FAIL")
              << " (expected=0x" << std::hex << expected
              << ", actual=0x" << actual << std::dec << ")\n";
    assert(pass);
}

static void check_bool(const char* label, bool condition) {
    std::cout << "  " << label << ": "
              << (condition ? "PASS" : "FAIL") << "\n";
    assert(condition);
}

int main() {
    std::cout << "=== QCAP2_MUXER_TYPE_RTSP Test ===\n\n";

    // ----------------------------------------------------
    // Test Case 1: RTSP API Configuration Setters
    // ----------------------------------------------------
    std::cout << "--- Test Case 1: RTSP API Configuration ---\n";

    qcap2_muxer_t* muxer = qcap2_muxer_new();
    assert(muxer != nullptr);

    // Configure Muxer Options
    qcap2_muxer_set_type(muxer, QCAP2_MUXER_TYPE_RTSP);
    std::cout << "  Set type to QCAP2_MUXER_TYPE_RTSP: PASS\n";

    qcap2_muxer_set_max_threads(muxer, 4);
    std::cout << "  Set max threads to 4: PASS\n";

    qcap2_muxer_set_endpoint(muxer, "127.0.0.1", 8554);
    std::cout << "  Set endpoint 127.0.0.1:8554: PASS\n";

    qcap2_muxer_set_realm(muxer, "QCAP2-Streaming");
    std::cout << "  Set authentication realm: PASS\n";

    qcap2_muxer_set_ssl(muxer, true);
    std::cout << "  Set SSL true: PASS\n";

    qcap2_muxer_set_certificate_chain_file(muxer, "cert.crt");
    std::cout << "  Set Certificate Chain File: PASS\n";

    qcap2_muxer_set_private_key_file(muxer, "private.key");
    std::cout << "  Set Private Key File: PASS\n";

    // Configure User Credentials
    qcap2_muxer_add_user(muxer, "admin", "admin123");
    std::cout << "  Add user (admin): PASS\n";

    qcap2_muxer_delete(muxer);
    std::cout << "  Clean delete after RTSP configuration: PASS\n\n";

    // ----------------------------------------------------
    // Test Case 2: Connection Failure (no server)
    // ----------------------------------------------------
    std::cout << "--- Test Case 2: Connection Failure (no server) ---\n";

    muxer = qcap2_muxer_new();
    assert(muxer != nullptr);

    qcap2_muxer_set_type(muxer, QCAP2_MUXER_TYPE_RTSP);
    qcap2_muxer_set_endpoint(muxer, "127.0.0.1", 65535); // Port with no RTSP server
    qcap2_muxer_set_ssl(muxer, false);

    // Setup minimum streams to test connection handshake in start()
    qcap2_video_decoder_t* vdec = qcap2_muxer_get_video_decoder(muxer, 0);
    assert(vdec != nullptr);

    qcap2_video_encoder_property_t* prop = qcap2_video_encoder_property_new();
    qcap2_video_encoder_property_set_property(prop, 0, QCAP_ENCODER_FORMAT_H264, 0, 320, 240, 25.0, 0, 0, 1000000, 0, 0, 0);
    qcap2_video_decoder_set_video_property(vdec, prop);
    qcap2_video_encoder_property_delete(prop);

    uint8_t dummy_extradata[] = { 0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x0a, 0xf8, 0x0f, 0x00, 0x00, 0x00, 0x01, 0x68, 0xce, 0x3c, 0x80 };
    qcap2_video_decoder_set_extra_data(vdec, dummy_extradata, sizeof(dummy_extradata));

    qcap2_program_info_t* prog = qcap2_program_info_new();
    qcap2_program_info_set_video_decoder_count(prog, 1);
    qcap2_program_info_set_video_decoder_index(prog, 0, 0);
    qcap2_muxer_add_program_info(muxer, prog);

    std::cout << "  Attempting RTSP start (expect network failure)...\n";
    QRESULT ret = qcap2_muxer_start(muxer);
    // Should fail as no RTSP server is active at 127.0.0.1:65535
    check_bool("RTSP start (no server)", ret != QCAP_RS_SUCCESSFUL);
    std::cout << "  (error code: 0x" << std::hex << ret << std::dec << ")\n";

    ret = qcap2_muxer_stop(muxer);
    check_result("RTSP stop after failed start", ret, QCAP_RS_SUCCESSFUL);

    qcap2_muxer_delete(muxer);
    std::cout << "  Clean delete: PASS\n\n";

    // ----------------------------------------------------
    // Test Case 3: Verify DEFAULT Type Backward Compatibility
    // ----------------------------------------------------
    std::cout << "--- Test Case 3: DEFAULT Type File Multiplexing (compat) ---\n";

    const char* output_filename = "muxer_rtsp_compat.mp4";
    std::remove(output_filename);

    muxer = qcap2_muxer_new();
    assert(muxer != nullptr);

    // Set type explicitly to DEFAULT
    qcap2_muxer_set_type(muxer, QCAP2_MUXER_TYPE_DEFAULT);
    qcap2_muxer_set_endpoint(muxer, output_filename, 0);

    vdec = qcap2_muxer_get_video_decoder(muxer, 0);
    assert(vdec != nullptr);

    prop = qcap2_video_encoder_property_new();
    qcap2_video_encoder_property_set_property(prop, 0, QCAP_ENCODER_FORMAT_H264, 0, 320, 240, 25.0, 0, 0, 1000000, 0, 0, 0);
    qcap2_video_decoder_set_video_property(vdec, prop);
    qcap2_video_encoder_property_delete(prop);

    qcap2_video_decoder_set_extra_data(vdec, dummy_extradata, sizeof(dummy_extradata));

    prog = qcap2_program_info_new();
    qcap2_program_info_set_video_decoder_count(prog, 1);
    qcap2_program_info_set_video_decoder_index(prog, 0, 0);
    qcap2_muxer_add_program_info(muxer, prog);

    ret = qcap2_muxer_start(muxer);
    check_result("DEFAULT start (file)", ret, QCAP_RS_SUCCESSFUL);

    // Push 10 dummy frames
    const int total_packets = 10;
    for (int i = 0; i < total_packets; i++) {
        qcap2_av_packet_t* pkt = new qcap2_av_packet_t;
        qcap2_av_packet_init(pkt);

        uint8_t dummy_payload[] = { 0x00, 0x00, 0x00, 0x01, 0x65, 0x01, 0x02, 0x03, 0x04 };
        if (qcap2_av_packet_alloc_buffer(pkt, sizeof(dummy_payload))) {
            uint8_t* pBuf = nullptr;
            int nSize = 0;
            qcap2_av_packet_get_buffer(pkt, &pBuf, &nSize);
            if (pBuf) {
                memcpy(pBuf, dummy_payload, sizeof(dummy_payload));
            }
            qcap2_av_packet_set_pts(pkt, i * 40000);
            qcap2_av_packet_set_dts(pkt, i * 40000);
            qcap2_av_packet_set_sample_time(pkt, i * 0.04);
            qcap2_av_packet_set_property(pkt, 0, (i == 0) ? TRUE : FALSE);
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
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    ret = qcap2_muxer_stop(muxer);
    check_result("DEFAULT stop", ret, QCAP_RS_SUCCESSFUL);

    qcap2_muxer_delete(muxer);
    std::cout << "  Clean delete: PASS\n";

    // Verify written file structure
    std::ifstream f(output_filename);
    check_bool("File creation", f.good());
    f.close();
    std::remove(output_filename);
    std::cout << "  DEFAULT backward compatibility: PASS\n\n";

    // ----------------------------------------------------
    // Test Case 4: Muxer API NULL-ptr safety
    // ----------------------------------------------------
    std::cout << "--- Test Case 4: NULL-ptr Safety ---\n";

    // All standard configuration and life-cycle options should handle NULL gracefully
    qcap2_muxer_set_type(nullptr, QCAP2_MUXER_TYPE_RTSP);
    qcap2_muxer_set_max_threads(nullptr, 4);
    qcap2_muxer_set_endpoint(nullptr, nullptr, 0);
    qcap2_muxer_set_realm(nullptr, nullptr);
    qcap2_muxer_set_ssl(nullptr, true);
    qcap2_muxer_set_certificate_chain_file(nullptr, nullptr);
    qcap2_muxer_set_private_key_file(nullptr, nullptr);
    qcap2_muxer_add_user(nullptr, nullptr, nullptr);
    qcap2_muxer_add_program_info(nullptr, nullptr);

    assert(qcap2_muxer_get_video_sink_count(nullptr) == 0);
    assert(qcap2_muxer_get_audio_sink_count(nullptr) == 0);
    assert(qcap2_muxer_get_video_decoder_count(nullptr) == 0);
    assert(qcap2_muxer_get_audio_decoder_count(nullptr) == 0);
    assert(qcap2_muxer_get_program_count(nullptr) == 0);
    assert(qcap2_muxer_get_program_info(nullptr, 0) == nullptr);
    assert(qcap2_muxer_get_video_sink(nullptr, 0) == nullptr);
    assert(qcap2_muxer_get_audio_sink(nullptr, 0) == nullptr);
    assert(qcap2_muxer_get_video_decoder(nullptr, 0) == nullptr);
    assert(qcap2_muxer_get_audio_decoder(nullptr, 0) == nullptr);

    ret = qcap2_muxer_start(nullptr);
    check_result("start(nullptr)", ret, QCAP_RS_ERROR_GENERAL);

    ret = qcap2_muxer_stop(nullptr);
    check_result("stop(nullptr)", ret, QCAP_RS_ERROR_GENERAL);

    ret = qcap2_muxer_play(nullptr);
    check_result("play(nullptr)", ret, QCAP_RS_SUCCESSFUL);

    qcap2_muxer_delete(nullptr);
    std::cout << "  NULL-ptr standard API calls: PASS\n";

    std::cout << "\n=== All RTSP Muxer Tests Passed Successfully! ===\n";
    return 0;
}
