#ifndef QCAP2_DEVICES_PRIV_H
#define QCAP2_DEVICES_PRIV_H

#include "qcap2.devices.h"
#include "qcap2.sync.h"

struct qcap2_video_source_priv_t {
    qcap2_rcbuffer_queue_t* queue;
    int stream_index;

    qcap2_video_source_priv_t() {
        queue = qcap2_rcbuffer_queue_new();
        stream_index = -1;
    }
    ~qcap2_video_source_priv_t() {
        if (queue) {
            qcap2_rcbuffer_queue_delete(queue);
        }
    }
};

struct qcap2_audio_source_priv_t {
    qcap2_rcbuffer_queue_t* queue;
    int stream_index;

    qcap2_audio_source_priv_t() {
        queue = qcap2_rcbuffer_queue_new();
        stream_index = -1;
    }
    ~qcap2_audio_source_priv_t() {
        if (queue) {
            qcap2_rcbuffer_queue_delete(queue);
        }
    }
};

#endif // QCAP2_DEVICES_PRIV_H
