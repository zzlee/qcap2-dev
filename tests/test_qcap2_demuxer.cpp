#include "qcap2.devices.h"
#include "qcap2.buffer.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <cstdio>

int main() {
    std::cout << "Generating test fixture...\n";
    // Generate 1-second video at 10 fps to ensure it actually has some frames
    int sys_ret = system("ffmpeg -f lavfi -i testsrc=duration=1:size=320x240:rate=10 -c:v mpeg4 -y test_fixture.mp4 >/dev/null 2>&1");
    if (sys_ret != 0) {
        std::cerr << "Failed to generate test file.\n";
        return 1;
    }

    std::cout << "Creating demuxer...\n";
    qcap2_demuxer_t* demuxer = qcap2_demuxer_new();
    if (!demuxer) {
        std::cerr << "Failed to create demuxer.\n";
        return 1;
    }

    qcap2_demuxer_set_url(demuxer, "test_fixture.mp4");

    std::cout << "Starting demuxer...\n";
    QRESULT ret = qcap2_demuxer_start(demuxer);
    if (ret == QCAP_RS_SUCCESSFUL) {
        std::cout << "Demuxer started successfully.\n";
        int v_streams = qcap2_demuxer_get_video_source_count(demuxer);
        std::cout << "Video streams: " << v_streams << "\n";

        if (v_streams > 0) {
            qcap2_video_source_t* vs = qcap2_demuxer_get_video_source(demuxer, 0);
            if (vs) {
                // start source queue
                qcap2_video_source_start(vs);

                std::cout << "Playing demuxer to start extraction thread...\n";
                qcap2_demuxer_play(demuxer);

                // Allow some time for reading packets
                std::this_thread::sleep_for(std::chrono::milliseconds(500));

                qcap2_rcbuffer_t* frame_buf = nullptr;
                QRESULT pop_res = qcap2_video_source_pop(vs, &frame_buf);
                if (pop_res == QCAP_RS_SUCCESSFUL && frame_buf) {
                    std::cout << "Successfully popped a frame from video source!\n";
                    qcap2_rcbuffer_release(frame_buf);
                } else {
                    std::cerr << "Failed to pop frame, ret=" << pop_res << "\n";
                }
            } else {
                std::cerr << "Could not get video source 0.\n";
            }
        }
    } else {
        std::cerr << "Failed to start demuxer, code: " << ret << "\n";
    }

    qcap2_demuxer_stop(demuxer);
    qcap2_demuxer_delete(demuxer);
    std::cout << "Demuxer deleted.\n";

    remove("test_fixture.mp4");

    return 0;
}
