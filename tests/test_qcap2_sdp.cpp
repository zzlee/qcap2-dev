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
#include <vector>
#include <atomic>

std::atomic<bool> g_push_running(true);

static void push_packets_thread(qcap2_video_decoder_t* vdec, qcap2_audio_decoder_t* adec) {
    int i = 0;
    while (g_push_running) {
        // Push video packet
        {
            qcap2_av_packet_t* pkt = new qcap2_av_packet_t;
            qcap2_av_packet_init(pkt);

            uint8_t dummy_v_payload[] = {
                0x00, 0x00, 0x00, 0x01,
                0x65, 0x88, 0x84, 0x0f, 0xf2, 0x62, 0x80, 0x00, 0xc3, 0xec, 0x9c, 0x9c, 0x9c, 0x9c, 0x9c, 0x9c, 0x9c, 0x9c, 0x9c, 0x9c, 0x9c, 0x9c, 0x9c, 0x9c, 0x9c, 0x9c, 0x9c, 0x9c, 0x9d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5e
            };
            if (qcap2_av_packet_alloc_buffer(pkt, sizeof(dummy_v_payload))) {
                uint8_t* pBuf = nullptr;
                int nSize = 0;
                qcap2_av_packet_get_buffer(pkt, &pBuf, &nSize);
                if (pBuf) memcpy(pBuf, dummy_v_payload, sizeof(dummy_v_payload));
                qcap2_av_packet_set_pts(pkt, i * 40000);
                qcap2_av_packet_set_dts(pkt, i * 40000);
                qcap2_av_packet_set_sample_time(pkt, i * 0.04);
                qcap2_av_packet_set_property(pkt, 0, (i % 25 == 0) ? TRUE : FALSE);
            }

            qcap2_rcbuffer_t* rcbuf = qcap2_rcbuffer_new(pkt, [](PVOID pData) {
                qcap2_av_packet_t* p = (qcap2_av_packet_t*)pData;
                if (p) {
                    qcap2_av_packet_free_buffer(p);
                    delete p;
                }
            });

            qcap2_video_decoder_push(vdec, rcbuf);
            qcap2_rcbuffer_release(rcbuf);
        }

        // Push audio packet
        {
            qcap2_av_packet_t* pkt = new qcap2_av_packet_t;
            qcap2_av_packet_init(pkt);

            uint8_t dummy_a_payload[] = { 0x21, 0x10, 0x05, 0x02, 0x30 };
            if (qcap2_av_packet_alloc_buffer(pkt, sizeof(dummy_a_payload))) {
                uint8_t* pBuf = nullptr;
                int nSize = 0;
                qcap2_av_packet_get_buffer(pkt, &pBuf, &nSize);
                if (pBuf) memcpy(pBuf, dummy_a_payload, sizeof(dummy_a_payload));
                qcap2_av_packet_set_pts(pkt, i * 1024);
                qcap2_av_packet_set_dts(pkt, i * 1024);
                qcap2_av_packet_set_sample_time(pkt, i * (1024.0 / 44100.0));
            }

            qcap2_rcbuffer_t* rcbuf = qcap2_rcbuffer_new(pkt, [](PVOID pData) {
                qcap2_av_packet_t* p = (qcap2_av_packet_t*)pData;
                if (p) {
                    qcap2_av_packet_free_buffer(p);
                    delete p;
                }
            });

            qcap2_audio_decoder_push(adec, rcbuf);
            qcap2_rcbuffer_release(rcbuf);
        }

        i++;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}


static void check_result(const char* label, QRESULT actual, QRESULT expected) {
    bool pass = (actual == expected);
    std::cout << "  " << label << ": "
              << (pass ? "PASS" : "FAIL")
              << " (expected=0x" << std::hex << expected
              << ", actual=0x" << actual << std::dec << ")" << std::endl;
    assert(pass);
}

static void check_bool(const char* label, bool condition) {
    std::cout << "  " << label << ": "
              << (condition ? "PASS" : "FAIL") << std::endl;
    assert(condition);
}

int main() {
    std::cout << "=== QCAP2 SDP Muxer & Demuxer Integration Test ===\n\n";

    const char* sdp_filename = "test_qcap2_loop.sdp";
    std::remove(sdp_filename);

    // ----------------------------------------------------
    // Test Case 1: SDP Generation (Muxer Side)
    // ----------------------------------------------------
    std::cout << "--- Test Case 1: SDP Session Generation ---\n";

    qcap2_muxer_t* muxer = qcap2_muxer_new();
    assert(muxer != nullptr);

    qcap2_muxer_set_type(muxer, QCAP2_MUXER_TYPE_SDP);
    qcap2_muxer_set_endpoint(muxer, sdp_filename, 5004);

    // Setup Video Decoder Stream (H.264)
    qcap2_video_decoder_t* vdec = qcap2_muxer_get_video_decoder(muxer, 0);
    assert(vdec != nullptr);

    qcap2_video_encoder_property_t* vprop = qcap2_video_encoder_property_new();
    qcap2_video_encoder_property_set_property(vprop, 0, QCAP_ENCODER_FORMAT_H264, 0, 320, 240, 25.0, 0, 0, 1000000, 0, 0, 0);
    qcap2_video_decoder_set_video_property(vdec, vprop);
    qcap2_video_encoder_property_delete(vprop);

    uint8_t dummy_v_extradata[] = {
        0x00, 0x00, 0x00, 0x01,
        0x67, 0x42, 0xc0, 0x0b, 0xd9, 0x01, 0x41, 0xfb, 0x01, 0x10, 0x00, 0x00, 0x03, 0x00, 0x10, 0x00, 0x00, 0x03, 0x01, 0x40, 0xf1, 0x42, 0xa4, 0x80,
        0x00, 0x00, 0x00, 0x01,
        0x68, 0xcb, 0x83, 0xcb, 0x20
    };
    qcap2_video_decoder_set_extra_data(vdec, dummy_v_extradata, sizeof(dummy_v_extradata));

    // Setup Audio Decoder Stream (AAC)
    qcap2_audio_decoder_t* adec = qcap2_muxer_get_audio_decoder(muxer, 0);
    assert(adec != nullptr);

    qcap2_audio_encoder_property_t* aprop = qcap2_audio_encoder_property_new();
    qcap2_audio_encoder_property_set_property1(aprop, 0, QCAP_ENCODER_FORMAT_AAC, 2, 16, 44100, 128000);
    qcap2_audio_decoder_set_audio_property(adec, aprop);
    qcap2_audio_encoder_property_delete(aprop);

    uint8_t dummy_a_extradata[] = { 0x12, 0x10 }; // AudioSpecificConfig
    qcap2_audio_decoder_set_extra_data(adec, dummy_a_extradata, sizeof(dummy_a_extradata));

    // Define Program (Combined Video + Audio)
    qcap2_program_info_t* prog = qcap2_program_info_new();
    assert(prog != nullptr);
    qcap2_program_info_set_video_decoder_count(prog, 1);
    qcap2_program_info_set_video_decoder_index(prog, 0, 0);
    qcap2_program_info_set_audio_decoder_count(prog, 1);
    qcap2_program_info_set_audio_decoder_index(prog, 0, 0);
    qcap2_muxer_add_program_info(muxer, prog);

    // Start Muxer - Spawns 2 parallel RTP contexts and writes test_qcap2_loop.sdp
    std::cout << "  Starting SDP Muxer...\n";
    QRESULT ret = qcap2_muxer_start(muxer);
    check_result("Muxer start", ret, QCAP_RS_SUCCESSFUL);

    // Verify SDP File Structure
    std::cout << "  Reading generated SDP file...\n";
    std::ifstream sdp_in(sdp_filename);
    check_bool("SDP file created", sdp_in.good());

    std::string line;
    bool has_video_media = false;
    bool has_audio_media = false;
    bool has_h264_rtpmap = false;
    bool has_aac_rtpmap = false;

    while (std::getline(sdp_in, line)) {
        std::cout << "    [SDP] " << line << std::endl;
        if (line.find("m=video 5004") != std::string::npos) has_video_media = true;
        if (line.find("m=audio 5006") != std::string::npos) has_audio_media = true;
        if (line.find("a=rtpmap:96 H264") != std::string::npos) has_h264_rtpmap = true;
        if (line.find("a=rtpmap:97 MPEG4-GENERIC") != std::string::npos) has_aac_rtpmap = true;
    }
    sdp_in.close();

    check_bool("Global Video Stream session block", has_video_media);
    check_bool("Global Audio Stream session block", has_audio_media);
    check_bool("H.264 rtpmap definition", has_h264_rtpmap);
    check_bool("AAC rtpmap definition", has_aac_rtpmap);

    // ----------------------------------------------------
    // Test Case 2: SDP Stream Discovery (Demuxer Side)
    // ----------------------------------------------------
    std::cout << "\n--- Test Case 2: SDP Stream Discovery & Parsing ---" << std::endl;

    // Start background RTP sender thread to stream packets during demuxer initialization (avformat_find_stream_info)
    g_push_running = true;
    std::thread sender_t(push_packets_thread, vdec, adec);

    qcap2_demuxer_t* demuxer = qcap2_demuxer_new();
    assert(demuxer != nullptr);

    qcap2_demuxer_set_type(demuxer, QCAP2_DEMUXER_TYPE_SDP);
    qcap2_demuxer_set_url(demuxer, sdp_filename);

    std::cout << "  Starting SDP Demuxer (parses SDP and sets up RTP listeners)..." << std::endl;
    std::cout << std::flush;
    ret = qcap2_demuxer_start(demuxer);
    // Should succeed because SDP file is formatted and locally accessible
    check_result("Demuxer start", ret, QCAP_RS_SUCCESSFUL);

    // Stop background sender thread as demuxer has completed stream info resolution
    g_push_running = false;
    if (sender_t.joinable()) {
        sender_t.join();
    }

    int vs_count = qcap2_demuxer_get_video_source_count(demuxer);
    int as_count = qcap2_demuxer_get_audio_source_count(demuxer);
    int venc_count = qcap2_demuxer_get_video_encoder_count(demuxer);
    int aenc_count = qcap2_demuxer_get_audio_encoder_count(demuxer);

    std::cout << "  Parsed Video streams: " << vs_count << " (encoders: " << venc_count << ")\n";
    std::cout << "  Parsed Audio streams: " << as_count << " (encoders: " << aenc_count << ")\n";

    check_bool("Video stream count", vs_count == 1);
    check_bool("Video encoder count", venc_count == 1);
    check_bool("Audio stream count", as_count == 1);
    check_bool("Audio encoder count", aenc_count == 1);

    // Verify parsed stream formats
    qcap2_video_encoder_t* parsed_venc = qcap2_demuxer_get_video_encoder(demuxer, 0);
    qcap2_audio_encoder_t* parsed_aenc = qcap2_demuxer_get_audio_encoder(demuxer, 0);
    assert(parsed_venc != nullptr);
    assert(parsed_aenc != nullptr);

    qcap2_video_encoder_property_t* parsed_vprop = qcap2_video_encoder_property_new();
    qcap2_video_encoder_get_video_property(parsed_venc, parsed_vprop);
    ULONG nVFormat = 0, nWidth = 0, nHeight = 0;
    qcap2_video_encoder_property_get_property(parsed_vprop, nullptr, &nVFormat, nullptr, &nWidth, &nHeight, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    std::cout << "  Parsed Video Format: " << nVFormat << ", Width: " << nWidth << ", Height: " << nHeight << "\n";
    check_bool("H.264 format matched", nVFormat == QCAP_ENCODER_FORMAT_H264);
    check_bool("Width matched", nWidth == 320);
    check_bool("Height matched", nHeight == 240);
    qcap2_video_encoder_property_delete(parsed_vprop);

    qcap2_audio_encoder_property_t* parsed_aprop = qcap2_audio_encoder_property_new();
    qcap2_audio_encoder_get_audio_property(parsed_aenc, parsed_aprop);
    ULONG nAFormat = 0, nChannels = 0, nSampleFreq = 0;
    qcap2_audio_encoder_property_get_property1(parsed_aprop, nullptr, &nAFormat, &nChannels, nullptr, &nSampleFreq, nullptr);
    std::cout << "  Parsed Audio Format: " << nAFormat << ", Channels: " << nChannels << ", Frequency: " << nSampleFreq << "\n";
    check_bool("AAC format matched", nAFormat == QCAP_ENCODER_FORMAT_AAC);
    check_bool("Channels matched", nChannels == 2);
    check_bool("Frequency matched", nSampleFreq == 44100);
    qcap2_audio_encoder_property_delete(parsed_aprop);

    // Verify parsed extra-data propagation
    uint8_t* parsed_v_extra = nullptr;
    int parsed_v_extra_sz = 0;
    qcap2_video_encoder_get_extra_data(parsed_venc, &parsed_v_extra, &parsed_v_extra_sz);
    check_bool("Video extra-data propagated", parsed_v_extra_sz == sizeof(dummy_v_extradata));
    if (parsed_v_extra_sz > 0) {
        check_bool("Video SPS NAL matched", parsed_v_extra[4] == 0x67);
    }

    uint8_t* parsed_a_extra = nullptr;
    int parsed_a_extra_sz = 0;
    qcap2_audio_encoder_get_extra_data(parsed_aenc, &parsed_a_extra, &parsed_a_extra_sz);
    check_bool("Audio extra-data propagated", parsed_a_extra_sz == sizeof(dummy_a_extradata));
    if (parsed_a_extra_sz > 0) {
        check_bool("AudioSpecificConfig matched", parsed_a_extra[0] == 0x12 && parsed_a_extra[1] == 0x10);
    }

    // Playback Activation
    ret = qcap2_demuxer_play(demuxer);
    check_result("Demuxer play", ret, QCAP_RS_SUCCESSFUL);

    // ----------------------------------------------------
    // Test Case 3: DTS Pacing Packet Routing
    // ----------------------------------------------------
    std::cout << "\n--- Test Case 3: Concurrent Packet Routing ---\n";

    // Push dummy packets into decoders
    const int routing_count = 5;
    for (int i = 0; i < routing_count; i++) {
        qcap2_av_packet_t* pkt = new qcap2_av_packet_t;
        qcap2_av_packet_init(pkt);

        uint8_t dummy_v_payload[] = {
            0x00, 0x00, 0x00, 0x01,
            0x65, 0x88, 0x84, 0x0f, 0xf2, 0x62, 0x80, 0x00, 0xc3, 0xec, 0x9c, 0x9c, 0x9c, 0x9c, 0x9c, 0x9c, 0x9c, 0x9c, 0x9c, 0x9c, 0x9c, 0x9c, 0x9c, 0x9c, 0x9c, 0x9c, 0x9c, 0x9c, 0x9d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5e
        };
        if (qcap2_av_packet_alloc_buffer(pkt, sizeof(dummy_v_payload))) {
            uint8_t* pBuf = nullptr;
            int nSize = 0;
            qcap2_av_packet_get_buffer(pkt, &pBuf, &nSize);
            if (pBuf) memcpy(pBuf, dummy_v_payload, sizeof(dummy_v_payload));
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
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // ----------------------------------------------------
    // Test Case 4: Graceful Teardown
    // ----------------------------------------------------
    std::cout << "\n--- Test Case 4: Graceful Muxer & Demuxer Teardown ---\n";

    ret = qcap2_demuxer_stop(demuxer);
    check_result("Demuxer stop", ret, QCAP_RS_SUCCESSFUL);

    qcap2_demuxer_delete(demuxer);
    std::cout << "  Demuxer delete: PASS\n";

    ret = qcap2_muxer_stop(muxer);
    check_result("Muxer stop", ret, QCAP_RS_SUCCESSFUL);

    qcap2_muxer_delete(muxer);
    std::cout << "  Muxer delete: PASS\n";

    std::remove(sdp_filename);
    std::cout << "  SDP Loop clean up: PASS\n";

    std::cout << "\n=== All SDP Integration Tests Passed Successfully! ===\n";
    return 0;
}
