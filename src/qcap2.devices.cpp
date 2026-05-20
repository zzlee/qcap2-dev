#include "qcap2.devices_priv.h"
#include <new>

// qcap2_video_source_t implementation
qcap2_video_source_t* qcap2_video_source_new() {
    return reinterpret_cast<qcap2_video_source_t*>(new (std::nothrow) qcap2_video_source_priv_t());
}

void qcap2_video_source_delete(qcap2_video_source_t* pThis) {
    if (pThis) {
        delete reinterpret_cast<qcap2_video_source_priv_t*>(pThis);
    }
}

void qcap2_video_source_set_backend_type(qcap2_video_source_t* pThis, int nBackendType) {
    (void)pThis; (void)nBackendType;
}

void qcap2_video_source_set_frame_count(qcap2_video_source_t* pThis, int nFrameCount) {
    (void)pThis; (void)nFrameCount;
}

void qcap2_video_source_set_event(qcap2_video_source_t* pThis, qcap2_event_t* pEvent) {
    if (pThis) {
        qcap2_video_source_priv_t* priv = reinterpret_cast<qcap2_video_source_priv_t*>(pThis);
        qcap2_rcbuffer_queue_set_event(priv->queue, pEvent);
    }
}

void qcap2_video_source_set_auto_run(qcap2_video_source_t* pThis, bool bAutoRun) {
    (void)pThis; (void)bAutoRun;
}

void qcap2_video_source_set_video_format(qcap2_video_source_t* pThis, qcap2_video_format_t* pVideoFormat) {
    (void)pThis; (void)pVideoFormat;
}

void qcap2_video_source_get_video_format(qcap2_video_source_t* pThis, qcap2_video_format_t* pVideoFormat) {
    (void)pThis; (void)pVideoFormat;
}

void qcap2_video_source_set_buffers(qcap2_video_source_t* pThis, qcap2_rcbuffer_t** pBuffers) {
    if (pThis) {
        qcap2_video_source_priv_t* priv = reinterpret_cast<qcap2_video_source_priv_t*>(pThis);
        qcap2_rcbuffer_queue_set_buffers(priv->queue, pBuffers);
    }
}

void qcap2_video_source_set_device_index(qcap2_video_source_t* pThis, int nDeviceIndex) {
    (void)pThis; (void)nDeviceIndex;
}

void qcap2_video_source_set_stream_index(qcap2_video_source_t* pThis, int nStreamIndex) {
    if (pThis) {
        reinterpret_cast<qcap2_video_source_priv_t*>(pThis)->stream_index = nStreamIndex;
    }
}

void qcap2_video_source_set_src_ss_type(qcap2_video_source_t* pThis, int nSrcSSType) {
    (void)pThis; (void)nSrcSSType;
}

void qcap2_video_source_set_dst_ss_type(qcap2_video_source_t* pThis, int nDstSSType) {
    (void)pThis; (void)nDstSSType;
}

QRESULT qcap2_video_source_start(qcap2_video_source_t* pThis) {
    if (!pThis) return QCAP_RS_ERROR_INVALID_PARAMETER;
    qcap2_video_source_priv_t* priv = reinterpret_cast<qcap2_video_source_priv_t*>(pThis);
    return qcap2_rcbuffer_queue_start(priv->queue);
}

QRESULT qcap2_video_source_stop(qcap2_video_source_t* pThis) {
    if (!pThis) return QCAP_RS_ERROR_INVALID_PARAMETER;
    qcap2_video_source_priv_t* priv = reinterpret_cast<qcap2_video_source_priv_t*>(pThis);
    return qcap2_rcbuffer_queue_stop(priv->queue);
}

QRESULT qcap2_video_source_run(qcap2_video_source_t* pThis) {
    return QCAP_RS_SUCCESSFUL;
}

QRESULT qcap2_video_source_pop(qcap2_video_source_t* pThis, qcap2_rcbuffer_t** ppRCBuffer) {
    if (!pThis || !ppRCBuffer) return QCAP_RS_ERROR_INVALID_PARAMETER;
    qcap2_video_source_priv_t* priv = reinterpret_cast<qcap2_video_source_priv_t*>(pThis);
    return qcap2_rcbuffer_queue_pop(priv->queue, ppRCBuffer);
}

// qcap2_audio_source_t implementation
qcap2_audio_source_t* qcap2_audio_source_new() {
    return reinterpret_cast<qcap2_audio_source_t*>(new (std::nothrow) qcap2_audio_source_priv_t());
}

void qcap2_audio_source_delete(qcap2_audio_source_t* pThis) {
    if (pThis) {
        delete reinterpret_cast<qcap2_audio_source_priv_t*>(pThis);
    }
}

void qcap2_audio_source_set_backend_type(qcap2_audio_source_t* pThis, int nBackendType) {
    (void)pThis; (void)nBackendType;
}

void qcap2_audio_source_set_frame_count(qcap2_audio_source_t* pThis, int nFrameCount) {
    (void)pThis; (void)nFrameCount;
}

void qcap2_audio_source_set_event(qcap2_audio_source_t* pThis, qcap2_event_t* pEvent) {
    if (pThis) {
        qcap2_audio_source_priv_t* priv = reinterpret_cast<qcap2_audio_source_priv_t*>(pThis);
        qcap2_rcbuffer_queue_set_event(priv->queue, pEvent);
    }
}

void qcap2_audio_source_set_audio_format(qcap2_audio_source_t* pThis, qcap2_audio_format_t* pAudioFormat) {
    (void)pThis; (void)pAudioFormat;
}

void qcap2_audio_source_get_audio_format(qcap2_audio_source_t* pThis, qcap2_audio_format_t* pAudioFormat) {
    (void)pThis; (void)pAudioFormat;
}

void qcap2_audio_source_set_buffers(qcap2_audio_source_t* pThis, qcap2_rcbuffer_t** pBuffers) {
    if (pThis) {
        qcap2_audio_source_priv_t* priv = reinterpret_cast<qcap2_audio_source_priv_t*>(pThis);
        qcap2_rcbuffer_queue_set_buffers(priv->queue, pBuffers);
    }
}

void qcap2_audio_source_set_period_time(qcap2_audio_source_t* pThis, int nPeriodTime) {
    (void)pThis; (void)nPeriodTime;
}

void qcap2_audio_source_set_buffer_time(qcap2_audio_source_t* pThis, int nBufferTime) {
    (void)pThis; (void)nBufferTime;
}

void qcap2_audio_source_set_ideal_timer(qcap2_audio_source_t* pThis, bool bIdealTimer) {
    (void)pThis; (void)bIdealTimer;
}

void qcap2_audio_source_set_card(qcap2_audio_source_t* pThis, int nCard) {
    (void)pThis; (void)nCard;
}

void qcap2_audio_source_set_device(qcap2_audio_source_t* pThis, int nDevice) {
    (void)pThis; (void)nDevice;
}

QRESULT qcap2_audio_source_start(qcap2_audio_source_t* pThis) {
    if (!pThis) return QCAP_RS_ERROR_INVALID_PARAMETER;
    qcap2_audio_source_priv_t* priv = reinterpret_cast<qcap2_audio_source_priv_t*>(pThis);
    return qcap2_rcbuffer_queue_start(priv->queue);
}

QRESULT qcap2_audio_source_stop(qcap2_audio_source_t* pThis) {
    if (!pThis) return QCAP_RS_ERROR_INVALID_PARAMETER;
    qcap2_audio_source_priv_t* priv = reinterpret_cast<qcap2_audio_source_priv_t*>(pThis);
    return qcap2_rcbuffer_queue_stop(priv->queue);
}

QRESULT qcap2_audio_source_pop(qcap2_audio_source_t* pThis, qcap2_rcbuffer_t** ppRCBuffer) {
    if (!pThis || !ppRCBuffer) return QCAP_RS_ERROR_INVALID_PARAMETER;
    qcap2_audio_source_priv_t* priv = reinterpret_cast<qcap2_audio_source_priv_t*>(pThis);
    return qcap2_rcbuffer_queue_pop(priv->queue, ppRCBuffer);
}
