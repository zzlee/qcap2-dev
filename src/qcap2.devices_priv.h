#ifndef QCAP2_DEVICES_PRIV_H
#define QCAP2_DEVICES_PRIV_H

#include "qcap2.devices.h"
#include "qcap2.sync.h"
#include "qcap2.v4l2.h"
#include "qcap2.coe.h"
#include "qcap2.hsb.h"
#include "qcap2.pylon.h"
#include "qcap2.nvt.hdal.h"
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

    qcap2_video_source_backend_t* backend;

    // V4L2 properties
    char v4l2_name[256];
    int v4l2_buf_type_val;
    int v4l2_memory_val;
    bool v4l2_exp_buf;
    char v4l2_sg_names[16][256];

    // COE properties
    char coe_config_file[512];
    int coe_verbosity;

    // HSB properties
    int hsb_device_ordinal;
    char hsb_hololink_ip[64];
    char hsb_ibv_name[64];
    uint32_t hsb_ibv_port;

    // Pylon properties
    bool pylon_trigger_mode;
    int pylon_offset_x;
    int pylon_offset_y;
    float pylon_exposure_time;
    int pylon_white_balance_auto;
    float pylon_auto_gain_lower_limit;
    float pylon_auto_gain_upper_limit;
    float pylon_gain;
    int pylon_gain_auto;

    // HDAL properties
    int hdal_vcap_id;
    int hdal_vcap_id2;
    HD_DIM hdal_src_dim;
    HD_VIDEO_PXLFMT hdal_src_pxl_fmt;
    int hdal_vproc_id;
    HD_VIDEOCAP_DRV_CONFIG hdal_drv_config;
    HD_VIDEOCAP_CTRLFUNC hdal_ctrl_func;
    char hdal_vendor_isp_config[512];

    qcap2_video_source_priv_t() {
        queue = qcap2_rcbuffer_queue_new();
        stream_index = -1;
        backend_type = 0;
        device_index = 0;
        color_space = 0;
        width = 0;
        height = 0;
        frame_rate = 0.0;
        backend = nullptr;

        memset(v4l2_name, 0, sizeof(v4l2_name));
        v4l2_buf_type_val = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        v4l2_memory_val = V4L2_MEMORY_MMAP;
        v4l2_exp_buf = false;
        memset(v4l2_sg_names, 0, sizeof(v4l2_sg_names));

        memset(coe_config_file, 0, sizeof(coe_config_file));
        coe_verbosity = 0;

        hsb_device_ordinal = 0;
        memset(hsb_hololink_ip, 0, sizeof(hsb_hololink_ip));
        memset(hsb_ibv_name, 0, sizeof(hsb_ibv_name));
        hsb_ibv_port = 0;

        pylon_trigger_mode = false;
        pylon_offset_x = 0;
        pylon_offset_y = 0;
        pylon_exposure_time = 0.0f;
        pylon_white_balance_auto = 0;
        pylon_auto_gain_lower_limit = 0.0f;
        pylon_auto_gain_upper_limit = 0.0f;
        pylon_gain = 0.0f;
        pylon_gain_auto = 0;

        hdal_vcap_id = 0;
        hdal_vcap_id2 = 0;
        memset(&hdal_src_dim, 0, sizeof(hdal_src_dim));
        hdal_src_pxl_fmt = HD_VIDEO_PXLFMT_YUYV;
        hdal_vproc_id = 0;
        memset(&hdal_drv_config, 0, sizeof(hdal_drv_config));
        hdal_ctrl_func = HD_VIDEOCAP_CTRLFUNC_DEFAULT;
        memset(hdal_vendor_isp_config, 0, sizeof(hdal_vendor_isp_config));
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
