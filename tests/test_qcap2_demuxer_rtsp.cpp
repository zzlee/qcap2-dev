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
#include <cstring>

// Test helper: check QRESULT and print
static void check_result(const char* label, QRESULT actual, QRESULT expected) {
    bool pass = (actual == expected);
    std::cout << "  " << label << ": "
              << (pass ? "PASS" : "FAIL")
              << " (expected=0x" << std::hex << expected
              << ", actual=0x" << actual << std::dec << ")\n";
    assert(pass);
}

// Test helper: check that a condition is true (non-QRESULT assertions)
static void check_bool(const char* label, bool condition) {
    std::cout << "  " << label << ": "
              << (condition ? "PASS" : "FAIL") << "\n";
    assert(condition);
}

int main() {
    std::cout << "=== QCAP2_DEMUXER_TYPE_RTSP Test ===\n\n";

    // ----------------------------------------------------
    // Test Case 1: RTSP API Configuration Setters
    // ----------------------------------------------------
    std::cout << "--- Test Case 1: RTSP API Configuration ---\n";

    qcap2_demuxer_t* demuxer = qcap2_demuxer_new();
    assert(demuxer != nullptr);

    // Set type to RTSP
    qcap2_demuxer_set_type(demuxer, QCAP2_DEMUXER_TYPE_RTSP);
    std::cout << "  Set type to QCAP2_DEMUXER_TYPE_RTSP: PASS\n";

    // Configure RTSP-specific options
    qcap2_demuxer_set_rtsp_timeout(demuxer, 10000);
    std::cout << "  Set RTSP timeout to 10000ms: PASS\n";

    qcap2_demuxer_set_rtsp_reconnect(demuxer, 5, 3000);
    std::cout << "  Set RTSP reconnect (5 attempts, 3000ms delay): PASS\n";

    qcap2_demuxer_set_rtsp_user_agent(demuxer, "QCAP2-RTSP-Test/1.0");
    std::cout << "  Set RTSP User-Agent: PASS\n";

    qcap2_demuxer_set_rtsp_keep_alive(demuxer, 15000);
    std::cout << "  Set RTSP keep-alive interval to 15000ms: PASS\n";

    // Verify options don't crash when called on unconfigured demuxer (rtsp == nullptr initially)
    qcap2_demuxer_t* empty = qcap2_demuxer_new();
    assert(empty != nullptr);
    qcap2_demuxer_set_type(empty, QCAP2_DEMUXER_TYPE_RTSP);
    qcap2_demuxer_set_rtsp_timeout(empty, 5000);
    qcap2_demuxer_set_rtsp_reconnect(empty, 3, 1000);
    qcap2_demuxer_set_rtsp_user_agent(empty, "Test");
    qcap2_demuxer_set_rtsp_keep_alive(empty, 30000);
    std::cout << "  RTSP config after unconfigured start: PASS\n";
    qcap2_demuxer_delete(empty);

    qcap2_demuxer_delete(demuxer);
    std::cout << "  Clean delete after RTSP configuration: PASS\n\n";

    // ----------------------------------------------------
    // Test Case 2: RTSP Options Ignored for DEFAULT Type
    // ----------------------------------------------------
    std::cout << "--- Test Case 2: RTSP Options Ignored for DEFAULT Type ---\n";

    demuxer = qcap2_demuxer_new();
    assert(demuxer != nullptr);

    // Default type - RTSP options should be silently ignored
    qcap2_demuxer_set_rtsp_timeout(demuxer, 9999);
    qcap2_demuxer_set_rtsp_reconnect(demuxer, 99, 999);
    qcap2_demuxer_set_rtsp_user_agent(demuxer, "should-be-ignored");
    qcap2_demuxer_set_rtsp_keep_alive(demuxer, 99999);
    std::cout << "  RTSP options on DEFAULT type (no crash): PASS\n";

    qcap2_demuxer_delete(demuxer);
    std::cout << "  Clean delete: PASS\n\n";

    // ----------------------------------------------------
    // Test Case 3: RTSP Connection Failure (no server)
    // ----------------------------------------------------
    std::cout << "--- Test Case 3: RTSP Connection Failure (no server) ---\n";

    demuxer = qcap2_demuxer_new();
    assert(demuxer != nullptr);

    qcap2_demuxer_set_type(demuxer, QCAP2_DEMUXER_TYPE_RTSP);
    qcap2_demuxer_set_url(demuxer, "rtsp://127.0.0.1:65535/nonexistent");
    qcap2_demuxer_set_tcp(demuxer, true);
    qcap2_demuxer_set_rtsp_timeout(demuxer, 2000);  // 2s timeout for quick failure

    std::cout << "  Attempting connection to non-existent RTSP server...\n";
    QRESULT ret = qcap2_demuxer_start(demuxer);
    // Should fail - no RTSP server at that address
    check_bool("RTSP start (no server)", ret != QCAP_RS_SUCCESSFUL);
    std::cout << "  (error 0x" << std::hex << ret << std::dec << ")\n";

    // stop should be safe even after failed start
    ret = qcap2_demuxer_stop(demuxer);
    check_result("RTSP stop after failed start", ret, QCAP_RS_SUCCESSFUL);

    qcap2_demuxer_delete(demuxer);
    std::cout << "  Clean delete: PASS\n\n";

    // ----------------------------------------------------
    // Test Case 4: RTSP Lifecycle - Start/Stop Without Connection
    // ----------------------------------------------------
    std::cout << "--- Test Case 4: RTSP Lifecycle (no URL) ---\n";

    demuxer = qcap2_demuxer_new();
    assert(demuxer != nullptr);

    qcap2_demuxer_set_type(demuxer, QCAP2_DEMUXER_TYPE_RTSP);
    // No URL set - should fail gracefully
    ret = qcap2_demuxer_start(demuxer);
    check_bool("RTSP start (no URL)", ret != QCAP_RS_SUCCESSFUL);

    ret = qcap2_demuxer_play(demuxer);
    check_result("RTSP play (no format context)", ret, QCAP_RS_ERROR_INVALID_DEVICE);

    ret = qcap2_demuxer_stop(demuxer);
    check_result("RTSP stop (no format context)", ret, QCAP_RS_SUCCESSFUL);

    qcap2_demuxer_delete(demuxer);
    std::cout << "  Clean delete: PASS\n\n";

    // ----------------------------------------------------
    // Test Case 5: Verify DEFAULT Type Still Works (File Demuxing)
    // ----------------------------------------------------
    std::cout << "--- Test Case 5: DEFAULT Type File Demuxing (backward compat) ---\n";

    std::cout << "  Generating test fixture...\n";
    int sys_ret = system("ffmpeg -f lavfi -i testsrc=duration=1:size=320x240:rate=10 -c:v mpeg4 -y test_fixture_rtsp.mp4 >/dev/null 2>&1");
    if (sys_ret != 0) {
        std::cerr << "  FAIL: Could not generate test fixture.\n";
        return 1;
    }
    std::cout << "  Test fixture generated.\n";

    // First, test that the DEFAULT type still works
    demuxer = qcap2_demuxer_new();
    assert(demuxer != nullptr);

    // Explicitly check type is DEFAULT
    qcap2_demuxer_set_url(demuxer, "test_fixture_rtsp.mp4");
    // type is already QCAP2_DEMUXER_TYPE_DEFAULT

    ret = qcap2_demuxer_start(demuxer);
    check_result("DEFAULT start (file)", ret, QCAP_RS_SUCCESSFUL);

    int v_streams = qcap2_demuxer_get_video_source_count(demuxer);
    int v_encoders = qcap2_demuxer_get_video_encoder_count(demuxer);
    int a_streams = qcap2_demuxer_get_audio_source_count(demuxer);
    int a_encoders = qcap2_demuxer_get_audio_encoder_count(demuxer);
    int programs = qcap2_demuxer_get_program_count(demuxer);

    std::cout << "  Video sources: " << v_streams << "\n";
    std::cout << "  Video encoders: " << v_encoders << "\n";
    std::cout << "  Audio sources: " << a_streams << "\n";
    std::cout << "  Audio encoders: " << a_encoders << "\n";
    std::cout << "  Programs: " << programs << "\n";

    assert(v_streams == 1);
    assert(v_encoders == 1);
    assert(a_streams == 0);
    assert(programs == 1);

    qcap2_video_source_t* vs = qcap2_demuxer_get_video_source(demuxer, 0);
    qcap2_video_encoder_t* venc = qcap2_demuxer_get_video_encoder(demuxer, 0);
    assert(vs != nullptr);
    assert(venc != nullptr);

    qcap2_video_source_start(vs);
    qcap2_video_encoder_start(venc);

    ret = qcap2_demuxer_play(demuxer);
    check_result("DEFAULT play (file)", ret, QCAP_RS_SUCCESSFUL);

    int popped_packets = 0;
    auto start_time = std::chrono::steady_clock::now();
    while (popped_packets < 10) {
        qcap2_rcbuffer_t* pkt_buf = nullptr;
        QRESULT pop_res = qcap2_video_encoder_pop(venc, &pkt_buf);
        if (pop_res == QCAP_RS_SUCCESSFUL && pkt_buf) {
            qcap2_rcbuffer_release(pkt_buf);
            popped_packets++;
        } else {
            std::cout << "  (pop ended at " << popped_packets << " packets, res=" << pop_res << ")\n";
            break;
        }
    }
    auto duration = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start_time).count();

    std::cout << "  Popped " << popped_packets << " packets in " << duration << "s.\n";
    assert(popped_packets >= 8);  // should get most of the 10 frames

    ret = qcap2_demuxer_stop(demuxer);
    check_result("DEFAULT stop", ret, QCAP_RS_SUCCESSFUL);

    qcap2_demuxer_delete(demuxer);
    std::cout << "  Clean delete: PASS\n\n";

    // ----------------------------------------------------
    // Test Case 6: Reuse Same File with RTSP Type (expect failure)
    // ----------------------------------------------------
    std::cout << "--- Test Case 6: RTSP Type with File URL (expect failure) ---\n";

    demuxer = qcap2_demuxer_new();
    assert(demuxer != nullptr);

    qcap2_demuxer_set_type(demuxer, QCAP2_DEMUXER_TYPE_RTSP);
    qcap2_demuxer_set_url(demuxer, "test_fixture_rtsp.mp4");  // file, not RTSP
    qcap2_demuxer_set_tcp(demuxer, true);
    qcap2_demuxer_set_rtsp_timeout(demuxer, 2000);

    std::cout << "  Starting RTSP demuxer with file URL (should fail)...\n";
    ret = qcap2_demuxer_start(demuxer);
    // FFmpeg's RTSP demuxer won't open a local file - expects failure
    check_bool("RTSP start with file URL", ret != QCAP_RS_SUCCESSFUL);
    std::cout << "  (error code: 0x" << std::hex << ret << std::dec << ")\n";

    ret = qcap2_demuxer_stop(demuxer);
    check_result("RTSP stop after failure", ret, QCAP_RS_SUCCESSFUL);

    qcap2_demuxer_delete(demuxer);
    std::cout << "  Clean delete: PASS\n\n";

    // ----------------------------------------------------
    // Test Case 7: RTSP API NULL-ptr safety
    // ----------------------------------------------------
    std::cout << "--- Test Case 7: NULL-ptr Safety ---\n";

    // All RTSP API functions should handle NULL gracefully
    qcap2_demuxer_set_rtsp_timeout(nullptr, 1000);
    qcap2_demuxer_set_rtsp_reconnect(nullptr, 5, 1000);
    qcap2_demuxer_set_rtsp_user_agent(nullptr, "test");
    qcap2_demuxer_set_rtsp_keep_alive(nullptr, 1000);
    std::cout << "  NULL-ptr RTSP API calls: PASS\n";

    // Standard API NULL-ptr safety
    ret = qcap2_demuxer_start(nullptr);
    check_result("start(nullptr)", ret, QCAP_RS_ERROR_INVALID_PARAMETER);
    ret = qcap2_demuxer_play(nullptr);
    check_result("play(nullptr)", ret, QCAP_RS_ERROR_INVALID_PARAMETER);
    ret = qcap2_demuxer_stop(nullptr);
    check_result("stop(nullptr)", ret, QCAP_RS_ERROR_INVALID_PARAMETER);
    assert(qcap2_demuxer_get_video_source_count(nullptr) == 0);
    assert(qcap2_demuxer_get_audio_source_count(nullptr) == 0);
    assert(qcap2_demuxer_get_video_encoder_count(nullptr) == 0);
    assert(qcap2_demuxer_get_audio_encoder_count(nullptr) == 0);
    assert(qcap2_demuxer_get_program_count(nullptr) == 0);
    assert(qcap2_demuxer_get_video_source(nullptr, 0) == nullptr);
    assert(qcap2_demuxer_get_audio_source(nullptr, 0) == nullptr);
    assert(qcap2_demuxer_get_video_encoder(nullptr, 0) == nullptr);
    assert(qcap2_demuxer_get_audio_encoder(nullptr, 0) == nullptr);
    assert(qcap2_demuxer_get_program_info(nullptr, 0) == nullptr);
    std::cout << "  NULL-ptr standard API: PASS\n";

    qcap2_demuxer_delete(nullptr);
    std::cout << "  delete(nullptr): PASS\n\n";

    // Cleanup
    remove("test_fixture_rtsp.mp4");

    std::cout << "\n=== All RTSP Demuxer Tests Passed Successfully! ===\n";
    return 0;
}
