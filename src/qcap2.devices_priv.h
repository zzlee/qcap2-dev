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

#endif // QCAP2_DEVICES_PRIV_H
