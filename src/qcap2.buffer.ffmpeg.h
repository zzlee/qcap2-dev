#ifndef __QCAP2_BUFFER_FFMPEG_H__
#define __QCAP2_BUFFER_FFMPEG_H__

#include "qcap2.buffer_priv.h"

#ifdef __cplusplus
extern "C" {
#endif
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#ifdef __cplusplus
}
#endif

class qcap2_avpacket_buffer : public qcap2_rcbuffer_t {
    AVPacket* pkt_;
    qcap2_av_packet_t legacy_pkt_;

protected:
    void on_release_resource() override {
        av_packet_free(&pkt_);
    }

public:
    qcap2_avpacket_buffer(AVPacket* pkt, double sample_time = 0.0) : pkt_(pkt) {
        memset(&legacy_pkt_, 0, sizeof(legacy_pkt_));
        auto* priv = (qcap2_av_packet_priv_t*)&legacy_pkt_;
        priv->nStreamIndex = pkt->stream_index;
        priv->bIsKeyFrame = (pkt->flags & AV_PKT_FLAG_KEY) ? TRUE : FALSE;
        priv->dSampleTime = sample_time;
        priv->nPTS = pkt->pts;
        priv->nDTS = pkt->dts;
        priv->pBuffer = pkt->data;
        priv->nSize = pkt->size;
        priv->bOwnsBuffer = false;
    }

    AVPacket* native_handle() const { return pkt_; }

    PVOID get_data() const override { return (PVOID)&legacy_pkt_; }
    qcap2_buffer_type_t get_type() const override { return QCAP2_BUFFER_TYPE_AVPACKET; }
    PVOID get_native_handle() const override { return pkt_; }

    QRESULT get_pts(int64_t* pts) override {
        if (!pts) return QCAP_RS_ERROR_INVALID_PARAMETER;
        *pts = pkt_->pts;
        return QCAP_RS_SUCCESSFUL;
    }
    QRESULT set_pts(int64_t pts) override {
        pkt_->pts = pts;
        auto* priv = (qcap2_av_packet_priv_t*)&legacy_pkt_;
        priv->nPTS = pts;
        return QCAP_RS_SUCCESSFUL;
    }
    QRESULT get_dts(int64_t* dts) override {
        if (!dts) return QCAP_RS_ERROR_INVALID_PARAMETER;
        *dts = pkt_->dts;
        return QCAP_RS_SUCCESSFUL;
    }
    QRESULT set_dts(int64_t dts) override {
        pkt_->dts = dts;
        auto* priv = (qcap2_av_packet_priv_t*)&legacy_pkt_;
        priv->nDTS = dts;
        return QCAP_RS_SUCCESSFUL;
    }
    QRESULT get_stream_index(int* idx) override {
        if (!idx) return QCAP_RS_ERROR_INVALID_PARAMETER;
        *idx = pkt_->stream_index;
        return QCAP_RS_SUCCESSFUL;
    }
    QRESULT set_stream_index(int idx) override {
        pkt_->stream_index = idx;
        auto* priv = (qcap2_av_packet_priv_t*)&legacy_pkt_;
        priv->nStreamIndex = idx;
        return QCAP_RS_SUCCESSFUL;
    }
    QRESULT is_keyframe(BOOL* key) override {
        if (!key) return QCAP_RS_ERROR_INVALID_PARAMETER;
        *key = (pkt_->flags & AV_PKT_FLAG_KEY) ? TRUE : FALSE;
        return QCAP_RS_SUCCESSFUL;
    }
    QRESULT set_keyframe(BOOL key) override {
        if (key) {
            pkt_->flags |= AV_PKT_FLAG_KEY;
        } else {
            pkt_->flags &= ~AV_PKT_FLAG_KEY;
        }
        auto* priv = (qcap2_av_packet_priv_t*)&legacy_pkt_;
        priv->bIsKeyFrame = key;
        return QCAP_RS_SUCCESSFUL;
    }

    QRESULT get_data_ptr(uint8_t** data, int* size) override {
        if (!data || !size) return QCAP_RS_ERROR_INVALID_PARAMETER;
        *data = pkt_->data;
        *size = pkt_->size;
        return QCAP_RS_SUCCESSFUL;
    }
    QRESULT get_video_property(ULONG* colorspace, ULONG* width, ULONG* height) override {
        return QCAP_RS_ERROR_NON_SUPPORT;
    }
    QRESULT get_plane(int plane, uint8_t** data, int* stride) override {
        return QCAP_RS_ERROR_NON_SUPPORT;
    }
};

class qcap2_avframe_buffer : public qcap2_rcbuffer_t {
    AVFrame* frame_;
    qcap2_av_frame_t legacy_frame_;

protected:
    void on_release_resource() override {
        av_frame_free(&frame_);
    }

public:
    qcap2_avframe_buffer(AVFrame* frame) : frame_(frame) {
        memset(&legacy_frame_, 0, sizeof(legacy_frame_));
        auto* priv = (qcap2_av_frame_priv_t*)&legacy_frame_;
        priv->nWidth = frame->width;
        priv->nHeight = frame->height;
        priv->nChannels = frame->ch_layout.nb_channels;
        priv->nSampleFmt = frame->format;
        priv->nSampleFrequency = frame->sample_rate;
        priv->nFrameSize = frame->nb_samples;
        priv->nPTS = frame->pts;
        for (int i = 0; i < 4; ++i) {
            priv->pBuffer[i] = frame->data[i];
            priv->pStride[i] = frame->linesize[i];
        }
        priv->bOwnsBuffer = false;
    }

    AVFrame* native_handle() const { return frame_; }

    PVOID get_data() const override { return (PVOID)&legacy_frame_; }
    qcap2_buffer_type_t get_type() const override { return QCAP2_BUFFER_TYPE_AVFRAME; }
    PVOID get_native_handle() const override { return frame_; }

    QRESULT get_pts(int64_t* pts) override {
        if (!pts) return QCAP_RS_ERROR_INVALID_PARAMETER;
        *pts = frame_->pts;
        return QCAP_RS_SUCCESSFUL;
    }
    QRESULT set_pts(int64_t pts) override {
        frame_->pts = pts;
        auto* priv = (qcap2_av_frame_priv_t*)&legacy_frame_;
        priv->nPTS = pts;
        return QCAP_RS_SUCCESSFUL;
    }
    QRESULT get_dts(int64_t* dts) override {
        if (!dts) return QCAP_RS_ERROR_INVALID_PARAMETER;
        *dts = frame_->pkt_dts;
        return QCAP_RS_SUCCESSFUL;
    }
    QRESULT set_dts(int64_t dts) override {
        frame_->pkt_dts = dts;
        return QCAP_RS_SUCCESSFUL;
    }
    QRESULT get_stream_index(int* idx) override {
        return QCAP_RS_ERROR_NON_SUPPORT;
    }
    QRESULT set_stream_index(int idx) override {
        return QCAP_RS_ERROR_NON_SUPPORT;
    }
    QRESULT is_keyframe(BOOL* key) override {
        if (!key) return QCAP_RS_ERROR_INVALID_PARAMETER;
        *key = (frame_->flags & AV_FRAME_FLAG_KEY) ? TRUE : FALSE;
        return QCAP_RS_SUCCESSFUL;
    }
    QRESULT set_keyframe(BOOL key) override {
        if (key) {
            frame_->flags |= AV_FRAME_FLAG_KEY;
        } else {
            frame_->flags &= ~AV_FRAME_FLAG_KEY;
        }
        return QCAP_RS_SUCCESSFUL;
    }

    QRESULT get_data_ptr(uint8_t** data, int* size) override {
        if (!data || !size) return QCAP_RS_ERROR_INVALID_PARAMETER;
        *data = frame_->data[0];
        *size = frame_->linesize[0] * frame_->height; // approximation
        return QCAP_RS_SUCCESSFUL;
    }
    QRESULT get_video_property(ULONG* colorspace, ULONG* width, ULONG* height) override {
        if (colorspace) *colorspace = frame_->colorspace;
        if (width) *width = frame_->width;
        if (height) *height = frame_->height;
        return QCAP_RS_SUCCESSFUL;
    }
    QRESULT get_plane(int plane, uint8_t** data, int* stride) override {
        if (plane < 0 || plane >= 4 || !data || !stride) return QCAP_RS_ERROR_INVALID_PARAMETER;
        *data = frame_->data[plane];
        *stride = frame_->linesize[plane];
        return QCAP_RS_SUCCESSFUL;
    }
};

inline qcap2_avpacket_buffer* qcap2_buffer_to_avpacket(qcap2_rcbuffer_t* buf) {
    return (buf && buf->get_type() == QCAP2_BUFFER_TYPE_AVPACKET)
        ? static_cast<qcap2_avpacket_buffer*>(buf) : nullptr;
}

inline qcap2_avframe_buffer* qcap2_buffer_to_avframe(qcap2_rcbuffer_t* buf) {
    return (buf && buf->get_type() == QCAP2_BUFFER_TYPE_AVFRAME)
        ? static_cast<qcap2_avframe_buffer*>(buf) : nullptr;
}

#endif // __QCAP2_BUFFER_FFMPEG_H__
