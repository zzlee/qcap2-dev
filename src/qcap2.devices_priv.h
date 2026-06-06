#ifndef QCAP2_DEVICES_PRIV_H
#define QCAP2_DEVICES_PRIV_H

#include "qcap2.devices.h"
#include "qcap2.sync.h"
#include "qcap2.v4l2.h"
#include <thread>
#include <atomic>
#include <linux/videodev2.h>
#include <string.h>

class qcap2_video_source_backend_t {
public:
    virtual ~qcap2_video_source_backend_t() = default;
    virtual QRESULT start() = 0;
    virtual QRESULT stop() = 0;
    virtual QRESULT push(qcap2_rcbuffer_t* pRCBuffer) = 0;
};

struct qcap2_video_source_priv_t {
    qcap2_rcbuffer_queue_t* queue;
    int stream_index;

    // Configuration
    int backend_type;
    int device_index;
    ULONG color_space;
    ULONG width;
    ULONG height;
    double frame_rate;
    int frame_count;

    qcap2_video_source_backend_t* backend;

    // V4L2 properties
    char v4l2_name[256];
    int v4l2_buf_type_val;
    int v4l2_memory_val;
    bool v4l2_exp_buf;
    char v4l2_sg_names[16][256];

    qcap2_video_source_priv_t() {
        queue = qcap2_rcbuffer_queue_new();
        stream_index = -1;
        backend_type = 0;
        device_index = 0;
        color_space = 0;
        width = 0;
        height = 0;
        frame_rate = 0.0;
        frame_count = 0;
        backend = nullptr;

        memset(v4l2_name, 0, sizeof(v4l2_name));
        v4l2_buf_type_val = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        v4l2_memory_val = V4L2_MEMORY_MMAP;
        v4l2_exp_buf = false;
        memset(v4l2_sg_names, 0, sizeof(v4l2_sg_names));
    }

    ~qcap2_video_source_priv_t();
};

class qcap2_audio_source_backend_t {
public:
    virtual ~qcap2_audio_source_backend_t() = default;
    virtual QRESULT start() = 0;
    virtual QRESULT stop() = 0;
};

struct qcap2_audio_source_priv_t {
    qcap2_rcbuffer_queue_t* queue;
    int stream_index;

    // Audio source context variables
    int backend_type;
    int frame_count;
    int period_time;
    int buffer_time;
    bool ideal_timer;
    int card;
    int device;
    ULONG channels;
    ULONG sample_fmt;
    ULONG sample_frequency;
    ULONG frame_size;

    qcap2_audio_source_backend_t* backend;

    qcap2_audio_source_priv_t() {
        queue = qcap2_rcbuffer_queue_new();
        stream_index = -1;
        backend_type = 0;
        frame_count = 0;
        period_time = 0;
        buffer_time = 0;
        ideal_timer = false;
        card = 0;
        device = 0;
        channels = 0;
        sample_fmt = 0;
        sample_frequency = 0;
        frame_size = 0;
        backend = nullptr;
    }
    ~qcap2_audio_source_priv_t();
};

class qcap2_audio_sink_backend_t {
public:
    virtual ~qcap2_audio_sink_backend_t() = default;
    virtual QRESULT start() = 0;
    virtual QRESULT stop() = 0;
    virtual QRESULT push(qcap2_rcbuffer_t* pRCBuffer) = 0;
};

struct qcap2_audio_sink_priv_t {
    qcap2_rcbuffer_queue_t* queue;
    qcap2_rcbuffer_queue_t* recycled_queue;

    // Configuration
    int backend_type;
    int card;
    int device;
    int period_time;
    int buffer_time;

    // Audio format properties
    ULONG channels;
    ULONG sample_fmt;
    ULONG sample_frequency;
    ULONG frame_size;

    qcap2_audio_sink_backend_t* backend;

    qcap2_audio_sink_priv_t() {
        queue = qcap2_rcbuffer_queue_new();
        if (queue) {
            qcap2_rcbuffer_queue_start(queue);
        }
        recycled_queue = qcap2_rcbuffer_queue_new();
        if (recycled_queue) {
            qcap2_rcbuffer_queue_start(recycled_queue);
        }
        backend_type = 0;
        card = 0;
        device = 0;
        period_time = 0;
        buffer_time = 0;
        channels = 0;
        sample_fmt = 0;
        sample_frequency = 0;
        frame_size = 0;
        backend = nullptr;
    }

    ~qcap2_audio_sink_priv_t();
};

class qcap2_video_sink_backend_t {
public:
    virtual ~qcap2_video_sink_backend_t() = default;
    virtual QRESULT start() = 0;
    virtual QRESULT stop() = 0;
    virtual QRESULT push(qcap2_rcbuffer_t* pRCBuffer) = 0;
};

struct qcap2_video_sink_priv_t {
    qcap2_rcbuffer_queue_t* queue;
    qcap2_rcbuffer_queue_t* recycled_queue;

    // Configuration
    int backend_type;
    int frame_count;
    bool multithread;
    uintptr_t native_handle;
    bool low_bandwidth;
    int display_system;
    int graphic_window_system;
    bool gpu_direct;
    ULONG scale_style;
    int device_index;
    int src_ss_type;
    int dst_ss_type;

    // Video format properties
    ULONG color_space;
    ULONG width;
    ULONG height;
    double frame_rate;

    // V4L2 properties
    char v4l2_name[256];
    int v4l2_buf_type_val;
    int v4l2_memory_val;

    // DRM properties
    uint32_t connector_id;
    uint32_t crtc_id;
    uint32_t plane_id;
    uint64_t drm_modifier;
    uint32_t drm_format;

    qcap2_video_sink_backend_t* backend;

    qcap2_video_sink_priv_t() {
        queue = qcap2_rcbuffer_queue_new();
        if (queue) {
            qcap2_rcbuffer_queue_start(queue);
        }
        recycled_queue = qcap2_rcbuffer_queue_new();
        if (recycled_queue) {
            qcap2_rcbuffer_queue_start(recycled_queue);
        }
        backend_type = 0;
        frame_count = 0;
        multithread = false;
        native_handle = 0;
        low_bandwidth = false;
        display_system = 0;
        graphic_window_system = 0;
        gpu_direct = false;
        scale_style = 0;
        device_index = 0;
        src_ss_type = 0;
        dst_ss_type = 0;

        color_space = 0;
        width = 0;
        height = 0;
        frame_rate = 0.0;

        memset(v4l2_name, 0, sizeof(v4l2_name));
        v4l2_buf_type_val = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        v4l2_memory_val = V4L2_MEMORY_MMAP;

        connector_id = 0;
        crtc_id = 0;
        plane_id = 0;
        drm_modifier = 0;
        drm_format = 0;

        backend = nullptr;
    }

    ~qcap2_video_sink_priv_t();
};

#endif // QCAP2_DEVICES_PRIV_H
