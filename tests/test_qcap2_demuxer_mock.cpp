#include "qcap2.devices.h"
#include "qcap2.buffer.h"
#include "qcap2.formats.h"
#include "qcap2.processing.h"
#include "qcap2.sync.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <cstdio>
#include <cassert>
#include <atomic>
#include <mutex>
#include <condition_variable>

std::atomic<bool> format_changed(false);
std::atomic<int> change_count(0);
std::mutex g_mtx;
std::condition_variable g_cv;

QRETURN on_dmx_event(PVOID /*pUserData*/) {
    std::cout << "  [Event] Received demuxer format change event!\n";
    std::lock_guard<std::mutex> lock(g_mtx);
    format_changed = true;
    change_count++;
    g_cv.notify_all();
    return QCAP_RT_OK;
}

int main() {
    std::cout << "--- Starting QCAP2 Demuxer Mock Integration Test ---\n";

    qcap2_demuxer_t* demuxer = qcap2_demuxer_new();
    assert(demuxer != nullptr);

    qcap2_demuxer_set_type(demuxer, QCAP2_DEMUXER_TYPE_MOCK);

    qcap2_event_t* evt = qcap2_event_new();
    qcap2_demuxer_set_event(demuxer, evt);

    qcap2_event_handlers_t* evt_handlers = qcap2_event_handlers_new();
    uintptr_t handle;
    qcap2_event_get_native_handle(evt, &handle);
    qcap2_event_handlers_add_handler(evt_handlers, handle, on_dmx_event, nullptr);
    qcap2_event_handlers_start(evt_handlers);

    std::cout << "  Starting demuxer...\n";
    QRESULT ret = qcap2_demuxer_start(demuxer);
    assert(ret == QCAP_RS_SUCCESSFUL);

    int v_encoders = qcap2_demuxer_get_video_encoder_count(demuxer);
    int a_encoders = qcap2_demuxer_get_audio_encoder_count(demuxer);
    std::cout << "  Demuxer video encoders: " << v_encoders << ", audio encoders: " << a_encoders << "\n";
    assert(v_encoders == 1);
    assert(a_encoders == 1);

    qcap2_video_encoder_t* venc = qcap2_demuxer_get_video_encoder(demuxer, 0);
    qcap2_audio_encoder_t* aenc = qcap2_demuxer_get_audio_encoder(demuxer, 0);
    assert(venc != nullptr && aenc != nullptr);

    qcap2_video_encoder_start(venc);
    qcap2_audio_encoder_start(aenc);

    std::cout << "  Playing demuxer...\n";
    qcap2_demuxer_play(demuxer);

    // Read loop
    int popped_video = 0;
    while (change_count < 2) {
        qcap2_rcbuffer_t* v_buf = nullptr;
        QRESULT v_res = qcap2_video_encoder_pop(venc, &v_buf);
        if (v_res == QCAP_RS_SUCCESSFUL && v_buf) {
            popped_video++;
            qcap2_rcbuffer_release(v_buf);
        }

        qcap2_rcbuffer_t* a_buf = nullptr;
        QRESULT a_res = qcap2_audio_encoder_pop(aenc, &a_buf);
        if (a_res == QCAP_RS_SUCCESSFUL && a_buf) {
            qcap2_rcbuffer_release(a_buf);
        }
        if (v_res == QCAP_RS_SUCCESSFUL && v_buf) {
            popped_video++;
            qcap2_rcbuffer_release(v_buf);
        }

        // Wait for event to fire, check if format changed
        {
            std::unique_lock<std::mutex> lock(g_mtx);
            if (g_cv.wait_for(lock, std::chrono::milliseconds(10), []{ return format_changed.load(); })) {
                std::cout << "  [Main] Format change detected!\n";
                format_changed = false;

                // Update demuxer internal state
                qcap2_demuxer_update(demuxer);

                // Stop encoders during reconfig
                qcap2_video_encoder_stop(venc);
                qcap2_audio_encoder_stop(aenc);

                // Assuming in real life we fetch new prog info and configure decoder here...
                // Mock test just verifies we can fetch the updated program info cleanly.
                qcap2_program_info_t* prog = qcap2_demuxer_get_program_info(demuxer, 0);
                assert(prog != nullptr);

                qcap2_video_encoder_start(venc);
                qcap2_audio_encoder_start(aenc);
            }
        }
    }

    std::cout << "  Received " << change_count << " format change events.\n";
    std::cout << "  Popped " << popped_video << " video frames total.\n";

    qcap2_demuxer_stop(demuxer);
    qcap2_event_handlers_stop(evt_handlers);
    qcap2_event_handlers_delete(evt_handlers);
    qcap2_event_delete(evt);
    qcap2_demuxer_delete(demuxer);

    std::cout << "\n=== All Demuxer Mock Integration Tests Passed Successfully! ===\n";
    return 0;
}
