#include "qcap2.devices.h"
#include "qcap2.alsa.h"
#include "qcap2.buffer.h"
#include "qcap2.formats.h"
#include "qcap2.processing.h"
#include <iostream>
#include <cassert>
#include <cstring>
#include <fstream>

int main() {
    std::cout << "--- Starting QCAP2 Audio Sink Test ---\n";

    // 1. Create the audio sink
    qcap2_audio_sink_t* sink = qcap2_audio_sink_new();
    assert(sink != nullptr);

    // 2. Set backend type to ALSA
    qcap2_audio_sink_set_backend_type(sink, QCAP2_AUDIO_SINK_BACKEND_TYPE_ALSA);

    // 3. Configure audio format properties (2 channels, 16 bits, 44100Hz)
    qcap2_audio_format_t* format = qcap2_audio_format_new();
    assert(format != nullptr);
    qcap2_audio_format_set_property(format, 2, 16, 44100);
    qcap2_audio_sink_set_audio_format(sink, format);
    qcap2_audio_format_delete(format);

    // 4. Set period time, buffer time, card, and device properties
    qcap2_audio_sink_set_period_time(sink, 20);
    qcap2_audio_sink_set_buffer_time(sink, 100);
    qcap2_audio_sink_set_card(sink, 0);
    qcap2_audio_sink_set_device(sink, 0); // Test card/device C API

    qcap2_audio_sink_set_alsa_card(sink, 0);
    qcap2_audio_sink_set_alsa_device(sink, 0);

    // 5. Test queue operations directly (without starting backend if device is absent)
    std::cout << "Testing queue push and pop mechanics...\n";
    qcap2_av_frame_t* frame = new qcap2_av_frame_t;
    qcap2_av_frame_init(frame);

    uint8_t dummy_samples[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };
    qcap2_av_frame_set_audio_property(frame, 2, 16, 44100, sizeof(dummy_samples));
    qcap2_av_frame_set_buffer(frame, dummy_samples, sizeof(dummy_samples));

    qcap2_rcbuffer_t* rcbuf = qcap2_rcbuffer_new(frame, [](PVOID pData) {
        qcap2_av_frame_t* f = (qcap2_av_frame_t*)pData;
        if (f) {
            delete f;
        }
    });

    // Push into queue
    QRESULT ret = qcap2_audio_sink_push(sink, rcbuf);
    assert(ret == QCAP_RS_SUCCESSFUL);

    // Pop from queue
    qcap2_rcbuffer_t* popped_rcbuf = nullptr;
    ret = qcap2_audio_sink_pop(sink, &popped_rcbuf);
    assert(ret == QCAP_RS_SUCCESSFUL && popped_rcbuf != nullptr);

    PVOID pData = qcap2_rcbuffer_lock_data(popped_rcbuf);
    assert(pData != nullptr);
    qcap2_av_frame_t* popped_frame = (qcap2_av_frame_t*)pData;

    ULONG ch = 0, fmt = 0, freq = 0, sz = 0;
    qcap2_av_frame_get_audio_property(popped_frame, &ch, &fmt, &freq, &sz);
    std::cout << "Popped frame: channels=" << ch << ", format=" << fmt 
              << ", frequency=" << freq << ", size=" << sz << "\n";
    assert(ch == 2);
    assert(fmt == 16);
    assert(freq == 44100);
    assert(sz == sizeof(dummy_samples));

    qcap2_rcbuffer_unlock_data(popped_rcbuf);
    qcap2_rcbuffer_release(popped_rcbuf);
    qcap2_rcbuffer_release(rcbuf);

    // 6. Test ALSA device open handling (expect general error if sound card pcmC0D0p is absent in Docker)
    std::cout << "Testing ALSA backend open handling...\n";
    ret = qcap2_audio_sink_start(sink);
    if (ret == QCAP_RS_SUCCESSFUL) {
        std::cout << "  ALSA card playback device opened successfully.\n";
        qcap2_audio_sink_stop(sink);
    } else {
        std::cout << "  ALSA playback device absent as expected (returned " << ret << ").\n";
        assert(ret == QCAP_RS_ERROR_GENERAL && "Expected general error for missing device!");
    }

    // 7. Delete the audio sink
    qcap2_audio_sink_delete(sink);

    std::cout << "--- All Audio Sink Tests Passed Successfully! ---\n";
    return 0;
}
