#include "qcap2.devices.h"
#include "qcap2.v4l2.h"
#include "qcap2.buffer.h"
#include "qcap2.formats.h"
#include <iostream>
#include <cassert>
#include <cstring>

int main() {
    std::cout << "--- Starting QCAP2 Video Sink Test ---\n";

    // 1. Create the video format and configure properties (RGB24, 1280x720, 30fps)
    qcap2_video_format_t* format = qcap2_video_format_new();
    assert(format != nullptr);
    qcap2_video_format_set_property(format, QCAP_COLORSPACE_TYPE_RGB24, 1280, 720, FALSE, 30.0);

    // 2. Create the video sink
    qcap2_video_sink_t* sink = qcap2_video_sink_new();
    assert(sink != nullptr);

    // 3. Set properties
    qcap2_video_sink_set_backend_type(sink, QCAP2_VIDEO_SINK_BACKEND_TYPE_V4L2);
    qcap2_video_sink_set_video_format(sink, format);
    qcap2_video_sink_set_frame_count(sink, 5);
    qcap2_video_sink_set_multithread(sink, true);
    qcap2_video_sink_set_native_handle(sink, 0x12345678);
    qcap2_video_sink_set_low_bandwidth(sink, true);
    qcap2_video_sink_set_display_system(sink, 1);
    qcap2_video_sink_set_graphic_window_system(sink, 2);
    qcap2_video_sink_set_gpu_direct(sink, true);
    qcap2_video_sink_set_scale_style(sink, 3);
    qcap2_video_sink_set_device_index(sink, 0);
    qcap2_video_sink_set_src_ss_type(sink, 4);
    qcap2_video_sink_set_dst_ss_type(sink, 5);

    // V4L2 properties
    qcap2_video_sink_set_v4l2_name(sink, "/dev/video0");
    const char* v4l2_name = qcap2_video_sink_get_v4l2_name(sink);
    assert(v4l2_name != nullptr && std::strcmp(v4l2_name, "/dev/video0") == 0);

    qcap2_video_sink_set_buf_type(sink, V4L2_BUF_TYPE_VIDEO_OUTPUT);
    qcap2_video_sink_set_memory(sink, V4L2_MEMORY_MMAP);

    // 4. Test queue push and pop mechanics
    std::cout << "Testing queue push and pop mechanics...\n";
    qcap2_av_frame_t* frame = new qcap2_av_frame_t;
    qcap2_av_frame_init(frame);

    ULONG image_stride = 1280 * 3; // RGB24
    ULONG image_size = image_stride * 720;
    uint8_t* dummy_pixels = new uint8_t[image_size];
    std::memset(dummy_pixels, 0xAA, image_size);

    qcap2_av_frame_set_video_property(frame, QCAP_COLORSPACE_TYPE_RGB24, 1280, 720);
    qcap2_av_frame_set_buffer(frame, dummy_pixels, image_stride);

    qcap2_rcbuffer_t* rcbuf = qcap2_rcbuffer_new(frame, [](PVOID pData) {
        qcap2_av_frame_t* f = (qcap2_av_frame_t*)pData;
        if (f) {
            delete f;
        }
    });

    // Push into queue
    QRESULT ret = qcap2_video_sink_push(sink, rcbuf);
    assert(ret == QCAP_RS_SUCCESSFUL);

    // Pop from queue
    qcap2_rcbuffer_t* popped_rcbuf = nullptr;
    // The public signature: QRESULT qcap2_video_sink_pop(qcap2_video_source_t* pThis, qcap2_rcbuffer_t** ppRCBuffer);
    ret = qcap2_video_sink_pop(reinterpret_cast<qcap2_video_source_t*>(sink), &popped_rcbuf);
    assert(ret == QCAP_RS_SUCCESSFUL && popped_rcbuf != nullptr);

    PVOID pData = qcap2_rcbuffer_lock_data(popped_rcbuf);
    assert(pData != nullptr);
    qcap2_av_frame_t* popped_frame = (qcap2_av_frame_t*)pData;

    ULONG cs = 0, w = 0, h = 0;
    qcap2_av_frame_get_video_property(popped_frame, &cs, &w, &h);
    std::cout << "Popped frame properties: colorspace=" << cs << ", width=" << w << ", height=" << h << "\n";
    assert(cs == QCAP_COLORSPACE_TYPE_RGB24);
    assert(w == 1280);
    assert(h == 720);

    qcap2_rcbuffer_unlock_data(popped_rcbuf);
    qcap2_rcbuffer_release(popped_rcbuf);
    qcap2_rcbuffer_release(rcbuf);
    delete[] dummy_pixels;

    // 5. Test V4L2 device open handling (expect general error in sandboxed Docker without /dev/video0)
    std::cout << "Testing V4L2 backend open handling...\n";
    ret = qcap2_video_sink_start(sink);
    if (ret == QCAP_RS_SUCCESSFUL) {
        std::cout << "  V4L2 loopback device opened successfully.\n";
        qcap2_video_sink_stop(sink);
    } else {
        std::cout << "  V4L2 device absent as expected (returned " << ret << ").\n";
        assert(ret == QCAP_RS_ERROR_GENERAL && "Expected general error for missing device!");
    }

    // 6. Delete instances
    qcap2_video_sink_delete(sink);
    qcap2_video_format_delete(format);

    std::cout << "--- All Video Sink Tests Passed Successfully! ---\n";
    return 0;
}
