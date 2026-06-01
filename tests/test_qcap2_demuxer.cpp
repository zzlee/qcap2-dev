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

int main() {
    std::cout << "Generating test fixture...\n";
    // Generate 1-second video at 10 fps to ensure it actually has some frames
    int sys_ret = system("ffmpeg -f lavfi -i testsrc=duration=1:size=320x240:rate=10 -c:v mpeg4 -y test_fixture.mp4 >/dev/null 2>&1");
    if (sys_ret != 0) {
        std::cerr << "Failed to generate test file.\n";
        return 1;
    }

    // ----------------------------------------------------
    // Test Case 1: Non-Live Source Mode (DTS Pacing)
    // ----------------------------------------------------
    std::cout << "\n--- Test Case 1: Non-Live Source Mode (DTS Pacing) ---\n";
    qcap2_demuxer_t* demuxer = qcap2_demuxer_new();
    assert(demuxer != nullptr);

    qcap2_demuxer_set_url(demuxer, "test_fixture.mp4");
    qcap2_demuxer_set_live_source(demuxer, false); // Paced by DTS

    std::cout << "Starting demuxer...\n";
    QRESULT ret = qcap2_demuxer_start(demuxer);
    assert(ret == QCAP_RS_SUCCESSFUL);

    int v_streams = qcap2_demuxer_get_video_source_count(demuxer);
    int v_encoders = qcap2_demuxer_get_video_encoder_count(demuxer);
    int programs = qcap2_demuxer_get_program_count(demuxer);

    std::cout << "Video streams (sources): " << v_streams << "\n";
    std::cout << "Video encoders: " << v_encoders << "\n";
    std::cout << "Programs: " << programs << "\n";

    assert(v_streams == 1);
    assert(v_encoders == 1);
    assert(programs == 1);

    // Verify program info
    qcap2_program_info_t* prog = qcap2_demuxer_get_program_info(demuxer, 0);
    assert(prog != nullptr);
    std::cout << "Program ID: " << qcap2_program_info_get_id(prog) << "\n";
    std::cout << "Program Number: " << qcap2_program_info_get_number(prog) << "\n";
    std::cout << "Program Video Sources: " << qcap2_program_info_get_video_source_count(prog) << "\n";
    std::cout << "Program Video Encoders: " << qcap2_program_info_get_video_encoder_count(prog) << "\n";

    assert(qcap2_program_info_get_id(prog) == 1);
    assert(qcap2_program_info_get_video_source_index(prog, 0) == 0);
    assert(qcap2_program_info_get_video_encoder_index(prog, 0) == 0);

    qcap2_video_source_t* vs = qcap2_demuxer_get_video_source(demuxer, 0);
    qcap2_video_encoder_t* venc = qcap2_demuxer_get_video_encoder(demuxer, 0);
    assert(vs != nullptr);
    assert(venc != nullptr);

    // Start queues
    qcap2_video_source_start(vs);
    // Since venc running was marked true internally, we can start it or bypass it
    qcap2_video_encoder_start(venc);

    auto start_time = std::chrono::steady_clock::now();
    qcap2_demuxer_play(demuxer);

    int popped_packets = 0;
    while (popped_packets < 10) {
        qcap2_rcbuffer_t* pkt_buf = nullptr;
        QRESULT pop_res = qcap2_video_encoder_pop(venc, &pkt_buf);
        if (pop_res == QCAP_RS_SUCCESSFUL && pkt_buf) {
            void* data = qcap2_rcbuffer_lock_data(pkt_buf);
            if (data) {
                qcap2_av_packet_t* pkt = (qcap2_av_packet_t*)data;
                uint8_t* buf = nullptr;
                int size = 0;
                int64_t pts = 0, dts = 0;
                double sample_time = 0.0;
                int stream_idx = 0;
                BOOL is_key = FALSE;

                qcap2_av_packet_get_buffer(pkt, &buf, &size);
                qcap2_av_packet_get_pts(pkt, &pts);
                qcap2_av_packet_get_dts(pkt, &dts);
                qcap2_av_packet_get_sample_time(pkt, &sample_time);
                qcap2_av_packet_get_property(pkt, &stream_idx, &is_key);

                std::cout << "Popped packet " << popped_packets << " -> size: " << size 
                          << " | PTS: " << pts << " | DTS: " << dts 
                          << " | sample_time: " << sample_time 
                          << " | keyframe: " << (is_key ? "YES" : "NO") << "\n";

                assert(size > 0);
                assert(buf != nullptr);
                qcap2_rcbuffer_unlock_data(pkt_buf);
            }
            qcap2_rcbuffer_release(pkt_buf);
            popped_packets++;
        } else {
            std::cerr << "Pop failed or EOF hit. pop_res=" << pop_res << "\n";
            break;
        }
    }
    auto end_time = std::chrono::steady_clock::now();
    double duration = std::chrono::duration<double>(end_time - start_time).count();
    std::cout << "Total demuxing duration in paced mode: " << duration << " seconds.\n";

    // Paced mode for 1-second file with 10 frames at 10 fps should take around 0.9 - 1.1s.
    assert(duration >= 0.8 && "Paced mode was too fast, pacing failed!");

    qcap2_demuxer_stop(demuxer);
    qcap2_demuxer_delete(demuxer);

    // ----------------------------------------------------
    // Test Case 2: Live Source Mode (Max Speed)
    // ----------------------------------------------------
    std::cout << "\n--- Test Case 2: Live Source Mode (Max Speed) ---\n";
    demuxer = qcap2_demuxer_new();
    assert(demuxer != nullptr);

    qcap2_demuxer_set_url(demuxer, "test_fixture.mp4");
    qcap2_demuxer_set_live_source(demuxer, true); // Maximum speed

    ret = qcap2_demuxer_start(demuxer);
    assert(ret == QCAP_RS_SUCCESSFUL);

    vs = qcap2_demuxer_get_video_source(demuxer, 0);
    venc = qcap2_demuxer_get_video_encoder(demuxer, 0);
    qcap2_video_source_start(vs);
    qcap2_video_encoder_start(venc);

    start_time = std::chrono::steady_clock::now();
    qcap2_demuxer_play(demuxer);

    popped_packets = 0;
    while (popped_packets < 10) {
        qcap2_rcbuffer_t* pkt_buf = nullptr;
        QRESULT pop_res = qcap2_video_encoder_pop(venc, &pkt_buf);
        if (pop_res == QCAP_RS_SUCCESSFUL && pkt_buf) {
            qcap2_rcbuffer_release(pkt_buf);
            popped_packets++;
        } else {
            break;
        }
    }
    end_time = std::chrono::steady_clock::now();
    duration = std::chrono::duration<double>(end_time - start_time).count();
    std::cout << "Total demuxing duration in live-source mode: " << duration << " seconds.\n";

    // Live source mode should demux 10 packets almost instantly (under 0.3s)
    assert(duration < 0.3 && "Live source mode was paced, should have been instant!");

    qcap2_demuxer_stop(demuxer);
    qcap2_demuxer_delete(demuxer);

    remove("test_fixture.mp4");
    std::cout << "\nAll demuxer tests passed successfully!\n";
    return 0;
}
