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
#ifdef __linux__
#include <unistd.h>
#endif

struct DemuxerContext {
    qcap2_demuxer_t* demuxer;
    qcap2_video_encoder_t* venc;
    qcap2_audio_encoder_t* aenc;
    qcap2_event_t* evt;
};

struct DeviceContext {
    PVOID device;
    qcap2_event_t* evt;
};

std::atomic<int> change_count(0);
std::atomic<int> popped_video(0);
std::atomic<int> popped_video_source(0);

QRETURN on_venc_event(PVOID pUserData) {
    DeviceContext* ctx = (DeviceContext*)pUserData;
    if (ctx) {
        uint64_t u = 0;
        qcap2_event_read(ctx->evt, &u);
        qcap2_video_encoder_t* venc = (qcap2_video_encoder_t*)ctx->device;
        for (uint64_t i = 0; i < u; ++i) {
            qcap2_rcbuffer_t* v_buf = nullptr;
            if (qcap2_video_encoder_pop(venc, &v_buf) == QCAP_RS_SUCCESSFUL) {
                if (v_buf) {
                    popped_video++;
                    qcap2_video_encoder_push_output(venc, v_buf);
                    qcap2_rcbuffer_release(v_buf);
                }
            }
        }
    }
    return QCAP_RT_OK;
}

QRETURN on_aenc_event(PVOID pUserData) {
    DeviceContext* ctx = (DeviceContext*)pUserData;
    if (ctx) {
        uint64_t u = 0;
        qcap2_event_read(ctx->evt, &u);
        qcap2_audio_encoder_t* aenc = (qcap2_audio_encoder_t*)ctx->device;
        for (uint64_t i = 0; i < u; ++i) {
            qcap2_rcbuffer_t* a_buf = nullptr;
            if (qcap2_audio_encoder_pop(aenc, &a_buf) == QCAP_RS_SUCCESSFUL) {
                if (a_buf) {
                    qcap2_audio_encoder_push_output(aenc, a_buf);
                    qcap2_rcbuffer_release(a_buf);
                }
            }
        }
    }
    return QCAP_RT_OK;
}

QRETURN on_vs_event(PVOID pUserData) {
    DeviceContext* ctx = (DeviceContext*)pUserData;
    if (ctx) {
        uint64_t u = 0;
        qcap2_event_read(ctx->evt, &u);
        qcap2_video_source_t* vs = (qcap2_video_source_t*)ctx->device;
        for (uint64_t i = 0; i < u; ++i) {
            qcap2_rcbuffer_t* v_buf = nullptr;
            if (qcap2_video_source_pop(vs, &v_buf) == QCAP_RS_SUCCESSFUL) {
                if (v_buf) {
                    popped_video_source++;
                    qcap2_video_source_push(vs, v_buf);
                    qcap2_rcbuffer_release(v_buf);
                }
            }
        }
    }
    return QCAP_RT_OK;
}

QRETURN on_as_event(PVOID pUserData) {
    DeviceContext* ctx = (DeviceContext*)pUserData;
    if (ctx) {
        uint64_t u = 0;
        qcap2_event_read(ctx->evt, &u);
        qcap2_audio_source_t* as = (qcap2_audio_source_t*)ctx->device;
        for (uint64_t i = 0; i < u; ++i) {
            qcap2_rcbuffer_t* a_buf = nullptr;
            if (qcap2_audio_source_pop(as, &a_buf) == QCAP_RS_SUCCESSFUL) {
                if (a_buf) {
                    qcap2_audio_source_push(as, a_buf);
                    qcap2_rcbuffer_release(a_buf);
                }
            }
        }
    }
    return QCAP_RT_OK;
}

QRETURN on_dmx_event(PVOID pUserData) {
    std::cout << "  [Event] Received demuxer format change event!\n";
    DemuxerContext* ctx = (DemuxerContext*)pUserData;
    if (ctx) {
        // Drain the event to reset it (otherwise level-triggered poll will loop infinitely)
        uint64_t u = 0;
        qcap2_event_read(ctx->evt, &u);

        // Update demuxer internal state
        qcap2_demuxer_update(ctx->demuxer);

        // Stop encoders during reconfig
        qcap2_video_encoder_stop(ctx->venc);
        qcap2_audio_encoder_stop(ctx->aenc);

        // Assuming in real life we fetch new prog info and configure decoder here...
        // Mock test just verifies we can fetch the updated program info cleanly.
        qcap2_program_info_t* prog = qcap2_demuxer_get_program_info(ctx->demuxer, 0);
        assert(prog != nullptr);

        qcap2_video_encoder_start(ctx->venc);
        qcap2_audio_encoder_start(ctx->aenc);
    }
    change_count++;
    return QCAP_RT_OK;
}

int main() {
    std::cout << "--- Starting QCAP2 Demuxer Mock Integration Test ---" << std::endl;

    qcap2_demuxer_t* demuxer = qcap2_demuxer_new();
    assert(demuxer != nullptr);

    qcap2_demuxer_set_type(demuxer, QCAP2_DEMUXER_TYPE_MOCK);

    qcap2_event_t* evt = qcap2_event_new();
    qcap2_demuxer_set_event(demuxer, evt);



    std::cout << "  Starting demuxer...\n";
    QRESULT ret = qcap2_demuxer_start(demuxer);
    assert(ret == QCAP_RS_SUCCESSFUL);

    int v_sources = qcap2_demuxer_get_video_source_count(demuxer);
    int a_sources = qcap2_demuxer_get_audio_source_count(demuxer);
    std::cout << "  Demuxer video sources: " << v_sources << ", audio sources: " << a_sources << "\n";
    assert(v_sources == 1);
    assert(a_sources == 1);

    qcap2_video_source_t* vs = qcap2_demuxer_get_video_source(demuxer, 0);
    qcap2_audio_source_t* as = qcap2_demuxer_get_audio_source(demuxer, 0);
    assert(vs != nullptr && as != nullptr);

    int v_encoders = qcap2_demuxer_get_video_encoder_count(demuxer);
    int a_encoders = qcap2_demuxer_get_audio_encoder_count(demuxer);
    std::cout << "  Demuxer video encoders: " << v_encoders << ", audio encoders: " << a_encoders << "\n";
    assert(v_encoders == 1);
    assert(a_encoders == 1);

    qcap2_video_encoder_t* venc = qcap2_demuxer_get_video_encoder(demuxer, 0);
    qcap2_audio_encoder_t* aenc = qcap2_demuxer_get_audio_encoder(demuxer, 0);
    assert(venc != nullptr && aenc != nullptr);

    qcap2_event_t* vs_evt = qcap2_event_new();
    qcap2_video_source_set_event(vs, vs_evt);

    qcap2_event_t* as_evt = qcap2_event_new();
    qcap2_audio_source_set_event(as, as_evt);

    qcap2_event_t* venc_evt = qcap2_event_new();
    qcap2_video_encoder_set_event(venc, venc_evt);

    qcap2_event_t* aenc_evt = qcap2_event_new();
    qcap2_audio_encoder_set_event(aenc, aenc_evt);

    qcap2_video_source_start(vs);
    qcap2_audio_source_start(as);
    qcap2_video_encoder_start(venc);
    qcap2_audio_encoder_start(aenc);

    DemuxerContext ctx = { demuxer, venc, aenc, evt };
    DeviceContext venc_ctx = { venc, venc_evt };
    DeviceContext aenc_ctx = { aenc, aenc_evt };
    DeviceContext vs_ctx = { vs, vs_evt };
    DeviceContext as_ctx = { as, as_evt };

    qcap2_event_handlers_t* evt_handlers = qcap2_event_handlers_new();
    
    uintptr_t handle;
    qcap2_event_get_native_handle(evt, &handle);
    qcap2_event_handlers_add_handler(evt_handlers, handle, on_dmx_event, &ctx);

    uintptr_t vs_handle;
    qcap2_event_get_native_handle(vs_evt, &vs_handle);
    qcap2_event_handlers_add_handler(evt_handlers, vs_handle, on_vs_event, &vs_ctx);

    uintptr_t as_handle;
    qcap2_event_get_native_handle(as_evt, &as_handle);
    qcap2_event_handlers_add_handler(evt_handlers, as_handle, on_as_event, &as_ctx);

    uintptr_t venc_handle;
    qcap2_event_get_native_handle(venc_evt, &venc_handle);
    qcap2_event_handlers_add_handler(evt_handlers, venc_handle, on_venc_event, &venc_ctx);

    uintptr_t aenc_handle;
    qcap2_event_get_native_handle(aenc_evt, &aenc_handle);
    qcap2_event_handlers_add_handler(evt_handlers, aenc_handle, on_aenc_event, &aenc_ctx);

    qcap2_event_handlers_start(evt_handlers);

    std::cout << "  Playing demuxer...\n";
    qcap2_demuxer_play(demuxer);

    // Read loop
    while (change_count < 2) {
        // Wait a small amount of time to avoid hogging CPU
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::cout << "  Received " << change_count << " format change events.\n";
    std::cout << "  Popped " << popped_video.load() << " video frames total.\n";
    std::cout << "  Popped video source " << popped_video_source.load() << " frames total.\n";
    assert(popped_video_source.load() > 0);

    qcap2_event_handlers_stop(evt_handlers);
    qcap2_event_handlers_delete(evt_handlers);
    qcap2_video_source_stop(vs);
    qcap2_audio_source_stop(as);
    qcap2_demuxer_stop(demuxer);
    qcap2_event_delete(evt);
    qcap2_event_delete(vs_evt);
    qcap2_event_delete(as_evt);
    qcap2_event_delete(venc_evt);
    qcap2_event_delete(aenc_evt);
    qcap2_demuxer_delete(demuxer);

    std::cout << "\n=== All Demuxer Mock Integration Tests Passed Successfully! ===\n";
    return 0;
}
