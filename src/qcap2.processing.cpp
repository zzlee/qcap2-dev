#include "qcap2.processing.h"
#include "qcap2.buffer.h"
#include "qcap2.sync.h"
#include "qcap2.formats.h"
#include <mutex>
#include <queue>
#include <vector>
#include <atomic>
#include <thread>
#include <condition_variable>
#include <stdlib.h>
#include <string.h>
#include <string>

#ifdef __cplusplus
extern "C" {
#endif

#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libavutil/pixdesc.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavcodec/avcodec.h>

#ifdef __cplusplus
}
#endif

struct ResamplerOutputFrame {
    qcap2_av_frame_t frame;
    uint8_t* audio_buffer;
    size_t buffer_size;
    qcap2_rcbuffer_t* rc_buffer;
};

typedef struct qcap2_audio_resampler_priv_t {
    std::mutex* mtx;
    std::condition_variable* cv;
    std::queue<qcap2_rcbuffer_t*> output_queue;

    // Output configuration (from set_audio_property)
    ULONG out_channels;
    ULONG out_sample_fmt;
    ULONG out_sample_freq;
    ULONG out_frame_size;
    int max_frames;
    bool multithread;
    qcap2_event_t* event;

    // Input state (cached from pushed frames)
    ULONG in_channels;
    ULONG in_sample_fmt;
    ULONG in_sample_freq;
    ULONG in_frame_size;

    // FFmpeg resampler context
    SwrContext* swr_ctx;
    bool running;
} qcap2_audio_resampler_priv_t;

#ifdef __cplusplus
extern "C" {
#endif

qcap2_audio_resampler_t* qcap2_audio_resampler_new() {
    qcap2_audio_resampler_priv_t* pThis = new qcap2_audio_resampler_priv_t;
    pThis->mtx = new std::mutex();
    pThis->cv = new std::condition_variable();
    pThis->out_channels = 2;
    pThis->out_sample_fmt = 1; // AV_SAMPLE_FMT_S16
    pThis->out_sample_freq = 44100;
    pThis->out_frame_size = 0;
    pThis->max_frames = 0;
    pThis->multithread = false;
    pThis->event = nullptr;
    pThis->swr_ctx = nullptr;
    pThis->running = false;

    // Clear input state
    pThis->in_channels = 0;
    pThis->in_sample_fmt = 0;
    pThis->in_sample_freq = 0;
    pThis->in_frame_size = 0;
    return (qcap2_audio_resampler_t*)pThis;
}

void qcap2_audio_resampler_delete(qcap2_audio_resampler_t* pThis) {
    if (pThis) {
        qcap2_audio_resampler_stop(pThis);
        qcap2_audio_resampler_priv_t* p = (qcap2_audio_resampler_priv_t*)pThis;
        delete p->cv;
        delete p->mtx;
        delete p;
    }
}

void qcap2_audio_resampler_set_audio_property(qcap2_audio_resampler_t* pThis, ULONG nChannels, ULONG nSampleFmt, ULONG nSampleFrequency, ULONG nFrameSize) {
    if (pThis) {
        qcap2_audio_resampler_priv_t* p = (qcap2_audio_resampler_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->out_channels = nChannels;
        p->out_sample_fmt = nSampleFmt;
        p->out_sample_freq = nSampleFrequency;
        p->out_frame_size = nFrameSize;
    }
}

void qcap2_audio_resampler_set_frame_count(qcap2_audio_resampler_t* pThis, int nFrameCount) {
    if (pThis) {
        qcap2_audio_resampler_priv_t* p = (qcap2_audio_resampler_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->max_frames = nFrameCount;
    }
}

void qcap2_audio_resampler_set_frame_align(qcap2_audio_resampler_t* pThis, int nFrameAlign) {
    (void)pThis;
    (void)nFrameAlign;
}

void qcap2_audio_resampler_set_multithread(qcap2_audio_resampler_t* pThis, bool bMultiThread) {
    if (pThis) {
        qcap2_audio_resampler_priv_t* p = (qcap2_audio_resampler_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->multithread = bMultiThread;
    }
}

void qcap2_audio_resampler_set_event(qcap2_audio_resampler_t* pThis, qcap2_event_t* pEvent) {
    if (pThis) {
        qcap2_audio_resampler_priv_t* p = (qcap2_audio_resampler_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->event = pEvent;
    }
}

static bool init_resampler(qcap2_audio_resampler_priv_t* p, ULONG in_ch, ULONG in_fmt, ULONG in_rate) {
    if (p->swr_ctx) {
        swr_free(&p->swr_ctx);
        p->swr_ctx = nullptr;
    }

    AVSampleFormat in_sample_fmt = (AVSampleFormat)in_fmt;
    AVSampleFormat out_sample_fmt = (AVSampleFormat)p->out_sample_fmt;

    AVChannelLayout in_layout;
    AVChannelLayout out_layout;
    av_channel_layout_default(&in_layout, in_ch);
    av_channel_layout_default(&out_layout, p->out_channels);

    int ret = swr_alloc_set_opts2(&p->swr_ctx,
                                  &out_layout, out_sample_fmt, p->out_sample_freq,
                                  &in_layout, in_sample_fmt, in_rate,
                                  0, nullptr);

    av_channel_layout_uninit(&in_layout);
    av_channel_layout_uninit(&out_layout);

    if (ret < 0 || !p->swr_ctx) {
        return false;
    }

    if (swr_init(p->swr_ctx) < 0) {
        swr_free(&p->swr_ctx);
        return false;
    }

    p->in_channels = in_ch;
    p->in_sample_fmt = in_fmt;
    p->in_sample_freq = in_rate;
    return true;
}

QRESULT qcap2_audio_resampler_start(qcap2_audio_resampler_t* pThis) {
    if (pThis) {
        qcap2_audio_resampler_priv_t* p = (qcap2_audio_resampler_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->running = true;
        return QCAP_RS_SUCCESSFUL;
    }
    return QCAP_RS_ERROR_GENERAL;
}

QRESULT qcap2_audio_resampler_stop(qcap2_audio_resampler_t* pThis) {
    if (pThis) {
        qcap2_audio_resampler_priv_t* p = (qcap2_audio_resampler_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->running = false;
        if (p->swr_ctx) {
            swr_free(&p->swr_ctx);
            p->swr_ctx = nullptr;
        }
        while (!p->output_queue.empty()) {
            qcap2_rcbuffer_t* buf = p->output_queue.front();
            p->output_queue.pop();
            qcap2_rcbuffer_release(buf);
        }
        p->cv->notify_all();
        return QCAP_RS_SUCCESSFUL;
    }
    return QCAP_RS_ERROR_GENERAL;
}

QRESULT qcap2_audio_resampler_push(qcap2_audio_resampler_t* pThis, qcap2_rcbuffer_t* pRCBuffer) {
    if (!pThis || !pRCBuffer) return QCAP_RS_ERROR_GENERAL;
    qcap2_audio_resampler_priv_t* p = (qcap2_audio_resampler_priv_t*)pThis;

    PVOID pData = qcap2_rcbuffer_lock_data(pRCBuffer);
    if (!pData) return QCAP_RS_ERROR_GENERAL;

    qcap2_av_frame_t* pFrame = (qcap2_av_frame_t*)pData;
    ULONG in_ch = 0, in_fmt = 0, in_rate = 0, in_size = 0;
    qcap2_av_frame_get_audio_property(pFrame, &in_ch, &in_fmt, &in_rate, &in_size);

    uint8_t* pInputBuffer = nullptr;
    int nInputStride = 0;
    qcap2_av_frame_get_buffer(pFrame, &pInputBuffer, &nInputStride);

    int64_t nPTS = 0;
    qcap2_av_frame_get_pts(pFrame, &nPTS);

    std::lock_guard<std::mutex> lock(*(p->mtx));
    if (!p->running) {
        qcap2_rcbuffer_unlock_data(pRCBuffer);
        return QCAP_RS_ERROR_GENERAL;
    }

    if (!p->swr_ctx || p->in_channels != in_ch || p->in_sample_fmt != in_fmt || p->in_sample_freq != in_rate) {
        if (!init_resampler(p, in_ch, in_fmt, in_rate)) {
            qcap2_rcbuffer_unlock_data(pRCBuffer);
            return QCAP_RS_ERROR_GENERAL;
        }
    }

    int64_t delay = swr_get_delay(p->swr_ctx, in_rate);
    int64_t out_samples = av_rescale_rnd(delay + in_size, p->out_sample_freq, in_rate, AV_ROUND_UP);

    int out_bytes_per_sample = av_get_bytes_per_sample((AVSampleFormat)p->out_sample_fmt);
    size_t out_buf_size = out_samples * p->out_channels * out_bytes_per_sample;
    uint8_t* pOutputBuffer = new uint8_t[out_buf_size];

    const uint8_t* in_data[8] = { pInputBuffer, nullptr };
    uint8_t* out_data[8] = { pOutputBuffer, nullptr };

    uint8_t* in_ptrs[4] = { nullptr };
    int in_strides[4] = { 0 };
    qcap2_av_frame_get_buffer1(pFrame, in_ptrs, in_strides);
    if (in_ptrs[0]) {
        for (int i = 0; i < 4; ++i) in_data[i] = in_ptrs[i];
    }

    int converted = swr_convert(p->swr_ctx, out_data, out_samples, in_data, in_size);
    qcap2_rcbuffer_unlock_data(pRCBuffer);

    if (converted < 0) {
        delete[] pOutputBuffer;
        return QCAP_RS_ERROR_GENERAL;
    }

    ResamplerOutputFrame* out_frame = new ResamplerOutputFrame;
    qcap2_av_frame_init(&out_frame->frame);
    qcap2_av_frame_set_audio_property(&out_frame->frame, p->out_channels, p->out_sample_fmt, p->out_sample_freq, converted);
    out_frame->audio_buffer = pOutputBuffer;
    out_frame->buffer_size = converted * p->out_channels * out_bytes_per_sample;

    qcap2_av_frame_set_buffer(&out_frame->frame, pOutputBuffer, converted * out_bytes_per_sample);
    qcap2_av_frame_set_pts(&out_frame->frame, nPTS);

    out_frame->rc_buffer = qcap2_rcbuffer_new(&out_frame->frame, [](PVOID pData) {
        ResamplerOutputFrame* p = qcap2_container_of(pData, ResamplerOutputFrame, frame);
        delete[] p->audio_buffer;
        delete p;
    });

    p->output_queue.push(out_frame->rc_buffer);
    p->cv->notify_all();

    if (p->event) {
        qcap2_event_notify(p->event);
    }

    return QCAP_RS_SUCCESSFUL;
}

QRESULT qcap2_audio_resampler_pop(qcap2_audio_resampler_t* pThis, qcap2_rcbuffer_t** ppRCBuffer) {
    if (!pThis || !ppRCBuffer) return QCAP_RS_ERROR_GENERAL;
    qcap2_audio_resampler_priv_t* p = (qcap2_audio_resampler_priv_t*)pThis;

    std::unique_lock<std::mutex> lock(*(p->mtx));
    p->cv->wait(lock, [p] { return !p->running || !p->output_queue.empty(); });

    if (!p->running && p->output_queue.empty()) {
        return QCAP_RS_ERROR_GENERAL;
    }

    *ppRCBuffer = p->output_queue.front();
    p->output_queue.pop();
    return QCAP_RS_SUCCESSFUL;
}

// ==============================================================================
// qcap2_audio_encoder_t Implementation
// ==============================================================================

static enum AVCodecID qcap2_audio_format_to_ffmpeg_codec_id(ULONG nEncoderFormat) {
    switch (nEncoderFormat) {
        case QCAP_ENCODER_FORMAT_PCM: return AV_CODEC_ID_PCM_S16LE;
        case QCAP_ENCODER_FORMAT_AAC:
        // case QCAP_ENCODER_FORMAT_AAC_RAW: // Same value as QCAP_ENCODER_FORMAT_AAC
        case QCAP_ENCODER_FORMAT_AAC_ADTS: return AV_CODEC_ID_AAC;
        case QCAP_ENCODER_FORMAT_MP2: return AV_CODEC_ID_MP2;
        case QCAP_ENCODER_FORMAT_MP3: return AV_CODEC_ID_MP3;
        case QCAP_ENCODER_FORMAT_OPUS: return AV_CODEC_ID_OPUS;
        case QCAP_ENCODER_FORMAT_AC3: return AV_CODEC_ID_AC3;
        case QCAP_ENCODER_FORMAT_G711_ALAW: return AV_CODEC_ID_PCM_ALAW;
        case QCAP_ENCODER_FORMAT_G711_ULAW: return AV_CODEC_ID_PCM_MULAW;
        case QCAP_ENCODER_FORMAT_G722: return AV_CODEC_ID_ADPCM_G722;
        case QCAP_ENCODER_FORMAT_G726: return AV_CODEC_ID_ADPCM_G726;
        default: return AV_CODEC_ID_NONE;
    }
}

typedef struct qcap2_audio_encoder_priv_t {
    std::mutex* mtx;
    std::condition_variable* cv;
    std::queue<qcap2_rcbuffer_t*> output_queue;

    qcap2_audio_encoder_property_t* property;
    uint8_t* extra_data;
    int extra_data_size;

    int max_frames;
    int packet_count;
    bool multithread;
    qcap2_event_t* event;

    AVCodecContext* avctx;
    bool running;

    qcap2_audio_encoder_priv_t() {
        mtx = new std::mutex();
        cv = new std::condition_variable();
        property = qcap2_audio_encoder_property_new();
        extra_data = nullptr;
        extra_data_size = 0;
        max_frames = 0;
        packet_count = 0;
        multithread = false;
        event = nullptr;
        avctx = nullptr;
        running = false;
    }

    ~qcap2_audio_encoder_priv_t() {
        if (property) {
            qcap2_audio_encoder_property_delete(property);
        }
        if (extra_data) {
            delete[] extra_data;
        }
        if (avctx) {
            avcodec_free_context(&avctx);
        }
        while (!output_queue.empty()) {
            qcap2_rcbuffer_release(output_queue.front());
            output_queue.pop();
        }
        delete cv;
        delete mtx;
    }
} qcap2_audio_encoder_priv_t;

qcap2_audio_encoder_t* qcap2_audio_encoder_new() {
    qcap2_audio_encoder_priv_t* p = new qcap2_audio_encoder_priv_t;
    return (qcap2_audio_encoder_t*)p;
}

void qcap2_audio_encoder_delete(qcap2_audio_encoder_t* pThis) {
    if (pThis) {
        qcap2_audio_encoder_stop(pThis);
        qcap2_audio_encoder_priv_t* p = (qcap2_audio_encoder_priv_t*)pThis;
        delete p;
    }
}

void qcap2_audio_encoder_set_audio_property(qcap2_audio_encoder_t* pThis, qcap2_audio_encoder_property_t* pAudioEncoderProperty) {
    if (pThis && pAudioEncoderProperty) {
        qcap2_audio_encoder_priv_t* p = (qcap2_audio_encoder_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        ULONG type = 0, fmt = 0, ch = 0, bits = 0, freq = 0, rate = 0;
        qcap2_audio_encoder_property_get_property1(pAudioEncoderProperty, &type, &fmt, &ch, &bits, &freq, &rate);
        qcap2_audio_encoder_property_set_property1(p->property, type, fmt, ch, bits, freq, rate);
    }
}

void qcap2_audio_encoder_get_audio_property(qcap2_audio_encoder_t* pThis, qcap2_audio_encoder_property_t* pAudioEncoderProperty) {
    if (pThis && pAudioEncoderProperty) {
        qcap2_audio_encoder_priv_t* p = (qcap2_audio_encoder_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        ULONG type = 0, fmt = 0, ch = 0, bits = 0, freq = 0, rate = 0;
        qcap2_audio_encoder_property_get_property1(p->property, &type, &fmt, &ch, &bits, &freq, &rate);
        qcap2_audio_encoder_property_set_property1(pAudioEncoderProperty, type, fmt, ch, bits, freq, rate);
    }
}

void qcap2_audio_encoder_get_extra_data(qcap2_audio_encoder_t* pThis, uint8_t** ppExtraData, int* pExtraDataSize) {
    if (pThis) {
        qcap2_audio_encoder_priv_t* p = (qcap2_audio_encoder_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        if (ppExtraData) *ppExtraData = p->extra_data;
        if (pExtraDataSize) *pExtraDataSize = p->extra_data_size;
    }
}

void qcap2_audio_encoder_set_extra_data(qcap2_audio_encoder_t* pThis, uint8_t* pExtraData, int nExtraDataSize) {
    if (pThis) {
        qcap2_audio_encoder_priv_t* p = (qcap2_audio_encoder_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        if (p->extra_data) {
            delete[] p->extra_data;
            p->extra_data = nullptr;
            p->extra_data_size = 0;
        }
        if (pExtraData && nExtraDataSize > 0) {
            p->extra_data = new uint8_t[nExtraDataSize + AV_INPUT_BUFFER_PADDING_SIZE];
            memcpy(p->extra_data, pExtraData, nExtraDataSize);
            memset(p->extra_data + nExtraDataSize, 0, AV_INPUT_BUFFER_PADDING_SIZE);
            p->extra_data_size = nExtraDataSize;
        }
    }
}

void qcap2_audio_encoder_set_frame_count(qcap2_audio_encoder_t* pThis, int nFrameCount) {
    if (pThis) {
        qcap2_audio_encoder_priv_t* p = (qcap2_audio_encoder_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->max_frames = nFrameCount;
    }
}

void qcap2_audio_encoder_set_packet_count(qcap2_audio_encoder_t* pThis, int nPacketCount) {
    if (pThis) {
        qcap2_audio_encoder_priv_t* p = (qcap2_audio_encoder_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->packet_count = nPacketCount;
    }
}

void qcap2_audio_encoder_set_multithread(qcap2_audio_encoder_t* pThis, bool bMultiThread) {
    if (pThis) {
        qcap2_audio_encoder_priv_t* p = (qcap2_audio_encoder_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->multithread = bMultiThread;
    }
}

void qcap2_audio_encoder_set_event(qcap2_audio_encoder_t* pThis, qcap2_event_t* pEvent) {
    if (pThis) {
        qcap2_audio_encoder_priv_t* p = (qcap2_audio_encoder_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->event = pEvent;
    }
}

QRESULT qcap2_audio_encoder_start(qcap2_audio_encoder_t* pThis) {
    if (!pThis) return QCAP_RS_ERROR_GENERAL;
    qcap2_audio_encoder_priv_t* p = (qcap2_audio_encoder_priv_t*)pThis;
    std::lock_guard<std::mutex> lock(*(p->mtx));

    if (p->running) return QCAP_RS_SUCCESSFUL;

    ULONG type = 0, fmt = 0, ch = 0, bits = 0, freq = 0, rate = 0;
    qcap2_audio_encoder_property_get_property1(p->property, &type, &fmt, &ch, &bits, &freq, &rate);

    enum AVCodecID codec_id = qcap2_audio_format_to_ffmpeg_codec_id(fmt);
    if (codec_id == AV_CODEC_ID_NONE) {
        return QCAP_RS_ERROR_NON_SUPPORT;
    }

    const AVCodec* codec = avcodec_find_encoder(codec_id);
    if (!codec) {
        return QCAP_RS_ERROR_NON_SUPPORT;
    }

    p->avctx = avcodec_alloc_context3(codec);
    if (!p->avctx) {
        return QCAP_RS_ERROR_OUT_OF_MEMORY;
    }

    p->avctx->bit_rate = rate;
    p->avctx->sample_rate = freq;
    av_channel_layout_default(&p->avctx->ch_layout, ch);

    if (codec->sample_fmts) {
        p->avctx->sample_fmt = codec->sample_fmts[0];
    } else {
        p->avctx->sample_fmt = AV_SAMPLE_FMT_S16;
    }

    if (p->extra_data && p->extra_data_size > 0) {
        p->avctx->extradata = (uint8_t*)av_mallocz(p->extra_data_size + AV_INPUT_BUFFER_PADDING_SIZE);
        if (p->avctx->extradata) {
            memcpy(p->avctx->extradata, p->extra_data, p->extra_data_size);
            p->avctx->extradata_size = p->extra_data_size;
        }
    }

    // Set AV_CODEC_FLAG_GLOBAL_HEADER if requested format is not ADTS and is AAC
    if (fmt == QCAP_ENCODER_FORMAT_AAC_RAW || fmt == QCAP_ENCODER_FORMAT_AAC) {
        p->avctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    if (avcodec_open2(p->avctx, codec, nullptr) < 0) {
        avcodec_free_context(&p->avctx);
        return QCAP_RS_ERROR_GENERAL;
    }

    // Capture extra data generated by encoder
    if (p->avctx->extradata && p->avctx->extradata_size > 0 && (!p->extra_data || p->extra_data_size == 0)) {
        p->extra_data_size = p->avctx->extradata_size;
        p->extra_data = new uint8_t[p->extra_data_size + AV_INPUT_BUFFER_PADDING_SIZE];
        memcpy(p->extra_data, p->avctx->extradata, p->extra_data_size);
        memset(p->extra_data + p->extra_data_size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    }

    p->running = true;
    return QCAP_RS_SUCCESSFUL;
}

QRESULT qcap2_audio_encoder_stop(qcap2_audio_encoder_t* pThis) {
    if (!pThis) return QCAP_RS_ERROR_GENERAL;
    qcap2_audio_encoder_priv_t* p = (qcap2_audio_encoder_priv_t*)pThis;
    std::lock_guard<std::mutex> lock(*(p->mtx));

    p->running = false;
    if (p->avctx) {
        avcodec_free_context(&p->avctx);
    }
    while (!p->output_queue.empty()) {
        qcap2_rcbuffer_release(p->output_queue.front());
        p->output_queue.pop();
    }
    p->cv->notify_all();
    return QCAP_RS_SUCCESSFUL;
}

QRESULT qcap2_audio_encoder_push(qcap2_audio_encoder_t* pThis, qcap2_rcbuffer_t* pRCBuffer) {
    if (!pThis || !pRCBuffer) return QCAP_RS_ERROR_GENERAL;
    qcap2_audio_encoder_priv_t* p = (qcap2_audio_encoder_priv_t*)pThis;

    PVOID pData = qcap2_rcbuffer_lock_data(pRCBuffer);
    if (!pData) return QCAP_RS_ERROR_GENERAL;

    qcap2_av_frame_t* pFrame = (qcap2_av_frame_t*)pData;

    std::lock_guard<std::mutex> lock(*(p->mtx));
    if (!p->running || !p->avctx) {
        qcap2_rcbuffer_unlock_data(pRCBuffer);
        return QCAP_RS_ERROR_GENERAL;
    }

    AVFrame* av_frame = av_frame_alloc();
    if (!av_frame) {
        qcap2_rcbuffer_unlock_data(pRCBuffer);
        return QCAP_RS_ERROR_OUT_OF_MEMORY;
    }

    ULONG in_ch = 0, in_fmt = 0, in_rate = 0, in_size = 0;
    qcap2_av_frame_get_audio_property(pFrame, &in_ch, &in_fmt, &in_rate, &in_size);

    uint8_t* pInputBuffer = nullptr;
    int nInputStride = 0;
    qcap2_av_frame_get_buffer(pFrame, &pInputBuffer, &nInputStride);

    int64_t nPTS = 0;
    qcap2_av_frame_get_pts(pFrame, &nPTS);
    double dSampleTime = 0;
    qcap2_av_frame_get_sample_time(pFrame, &dSampleTime);

    av_channel_layout_default(&av_frame->ch_layout, in_ch);
    av_frame->format = p->avctx->sample_fmt; // Assume input format matches encoder requirement via resampler
    av_frame->sample_rate = in_rate;
    av_frame->nb_samples = in_size;
    av_frame->pts = nPTS;

    uint8_t* in_ptrs[4] = { nullptr };
    int in_strides[4] = { 0 };
    qcap2_av_frame_get_buffer1(pFrame, in_ptrs, in_strides);

    if (in_ptrs[0]) {
        for (int i = 0; i < 4; ++i) {
            av_frame->data[i] = in_ptrs[i];
            av_frame->linesize[i] = in_strides[i];
        }
    } else {
        av_frame->data[0] = pInputBuffer;
        av_frame->linesize[0] = nInputStride;
    }

    int ret = avcodec_send_frame(p->avctx, av_frame);
    av_frame_free(&av_frame);
    qcap2_rcbuffer_unlock_data(pRCBuffer);

    if (ret < 0) {
        return QCAP_RS_ERROR_GENERAL;
    }

    AVPacket* pkt = av_packet_alloc();
    while (ret >= 0) {
        ret = avcodec_receive_packet(p->avctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            av_packet_free(&pkt);
            return QCAP_RS_ERROR_GENERAL;
        }

        qcap2_rcbuffer_t* out_rc = nullptr;
        qcap2_av_packet_t* new_packet = new qcap2_av_packet_t;
        qcap2_av_packet_init(new_packet);
        out_rc = qcap2_rcbuffer_new(new_packet, [](PVOID pData) {
            qcap2_av_packet_t* pkt = (qcap2_av_packet_t*)pData;
            if (pkt) {
                qcap2_av_packet_free_buffer(pkt);
                delete pkt;
            }
        });

        if (out_rc) {
            PVOID out_data = qcap2_rcbuffer_lock_data(out_rc);
            if (out_data) {
                qcap2_av_packet_t* out_packet = (qcap2_av_packet_t*)out_data;
                qcap2_av_packet_alloc_buffer(out_packet, pkt->size);

                uint8_t* out_buf = nullptr;
                int out_size = 0;
                qcap2_av_packet_get_buffer(out_packet, &out_buf, &out_size);
                if (out_buf && pkt->size <= out_size) {
                    memcpy(out_buf, pkt->data, pkt->size);
                }

                qcap2_av_packet_set_pts(out_packet, pkt->pts);
                qcap2_av_packet_set_dts(out_packet, pkt->dts);
                qcap2_av_packet_set_property(out_packet, 0, (pkt->flags & AV_PKT_FLAG_KEY) ? TRUE : FALSE);
                qcap2_av_packet_set_sample_time(out_packet, dSampleTime);

                qcap2_rcbuffer_unlock_data(out_rc);
                p->output_queue.push(out_rc);
            } else {
                qcap2_rcbuffer_release(out_rc);
            }
        }
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);

    p->cv->notify_all();
    if (p->event) {
        qcap2_event_notify(p->event);
    }

    return QCAP_RS_SUCCESSFUL;
}

QRESULT qcap2_audio_encoder_pop(qcap2_audio_encoder_t* pThis, qcap2_rcbuffer_t** ppRCBuffer) {
    if (!pThis || !ppRCBuffer) return QCAP_RS_ERROR_GENERAL;
    qcap2_audio_encoder_priv_t* p = (qcap2_audio_encoder_priv_t*)pThis;

    std::unique_lock<std::mutex> lock(*(p->mtx));
    p->cv->wait(lock, [p] { return !p->running || !p->output_queue.empty(); });

    if (!p->running && p->output_queue.empty()) {
        return QCAP_RS_ERROR_GENERAL;
    }

    *ppRCBuffer = p->output_queue.front();
    p->output_queue.pop();
    return QCAP_RS_SUCCESSFUL;
}

// ==============================================================================
// qcap2_audio_decoder_t Implementation
// ==============================================================================

typedef struct qcap2_audio_decoder_priv_t {
    std::mutex* mtx;
    std::condition_variable* cv;
    std::queue<qcap2_rcbuffer_t*> output_queue;

    qcap2_audio_encoder_property_t* property;
    uint8_t* extra_data;
    int extra_data_size;

    int max_frames;
    int packet_count;
    bool multithread;
    qcap2_event_t* event;

    int payload_type;
    AVCodecContext* avctx;
    bool running;

    qcap2_audio_decoder_priv_t() {
        mtx = new std::mutex();
        cv = new std::condition_variable();
        property = qcap2_audio_encoder_property_new();
        extra_data = nullptr;
        extra_data_size = 0;
        max_frames = 0;
        packet_count = 0;
        multithread = false;
        event = nullptr;
        payload_type = 0;
        avctx = nullptr;
        running = false;
    }

    ~qcap2_audio_decoder_priv_t() {
        if (property) {
            qcap2_audio_encoder_property_delete(property);
        }
        if (extra_data) {
            delete[] extra_data;
        }
        if (avctx) {
            avcodec_free_context(&avctx);
        }
        while (!output_queue.empty()) {
            qcap2_rcbuffer_release(output_queue.front());
            output_queue.pop();
        }
        delete cv;
        delete mtx;
    }
} qcap2_audio_decoder_priv_t;

qcap2_audio_decoder_t* qcap2_audio_decoder_new() {
    qcap2_audio_decoder_priv_t* p = new qcap2_audio_decoder_priv_t;
    return (qcap2_audio_decoder_t*)p;
}

void qcap2_audio_decoder_delete(qcap2_audio_decoder_t* pThis) {
    if (pThis) {
        qcap2_audio_decoder_stop(pThis);
        qcap2_audio_decoder_priv_t* p = (qcap2_audio_decoder_priv_t*)pThis;
        delete p;
    }
}

void qcap2_audio_decoder_set_audio_property(qcap2_audio_decoder_t* pThis, qcap2_audio_encoder_property_t* pAudioEncoderProperty) {
    if (pThis && pAudioEncoderProperty) {
        qcap2_audio_decoder_priv_t* p = (qcap2_audio_decoder_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        ULONG type = 0, fmt = 0, ch = 0, bits = 0, freq = 0, rate = 0;
        qcap2_audio_encoder_property_get_property1(pAudioEncoderProperty, &type, &fmt, &ch, &bits, &freq, &rate);
        qcap2_audio_encoder_property_set_property1(p->property, type, fmt, ch, bits, freq, rate);
    }
}

void qcap2_audio_decoder_get_audio_property(qcap2_audio_decoder_t* pThis, qcap2_audio_encoder_property_t* pAudioEncoderProperty) {
    if (pThis && pAudioEncoderProperty) {
        qcap2_audio_decoder_priv_t* p = (qcap2_audio_decoder_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        ULONG type = 0, fmt = 0, ch = 0, bits = 0, freq = 0, rate = 0;
        qcap2_audio_encoder_property_get_property1(p->property, &type, &fmt, &ch, &bits, &freq, &rate);
        qcap2_audio_encoder_property_set_property1(pAudioEncoderProperty, type, fmt, ch, bits, freq, rate);
    }
}

void qcap2_audio_decoder_get_extra_data(qcap2_audio_decoder_t* pThis, uint8_t** ppExtraData, int* pExtraDataSize) {
    if (pThis) {
        qcap2_audio_decoder_priv_t* p = (qcap2_audio_decoder_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        if (ppExtraData) *ppExtraData = p->extra_data;
        if (pExtraDataSize) *pExtraDataSize = p->extra_data_size;
    }
}

void qcap2_audio_decoder_set_extra_data(qcap2_audio_decoder_t* pThis, uint8_t* pExtraData, int nExtraDataSize) {
    if (pThis) {
        qcap2_audio_decoder_priv_t* p = (qcap2_audio_decoder_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        if (p->extra_data) {
            delete[] p->extra_data;
            p->extra_data = nullptr;
            p->extra_data_size = 0;
        }
        if (pExtraData && nExtraDataSize > 0) {
            p->extra_data = new uint8_t[nExtraDataSize + AV_INPUT_BUFFER_PADDING_SIZE];
            memcpy(p->extra_data, pExtraData, nExtraDataSize);
            memset(p->extra_data + nExtraDataSize, 0, AV_INPUT_BUFFER_PADDING_SIZE);
            p->extra_data_size = nExtraDataSize;
        }
    }
}

void qcap2_audio_decoder_set_frame_count(qcap2_audio_decoder_t* pThis, int nFrameCount) {
    if (pThis) {
        qcap2_audio_decoder_priv_t* p = (qcap2_audio_decoder_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->max_frames = nFrameCount;
    }
}

void qcap2_audio_decoder_set_packet_count(qcap2_audio_decoder_t* pThis, int nPacketCount) {
    if (pThis) {
        qcap2_audio_decoder_priv_t* p = (qcap2_audio_decoder_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->packet_count = nPacketCount;
    }
}

void qcap2_audio_decoder_set_multithread(qcap2_audio_decoder_t* pThis, bool bMultiThread) {
    if (pThis) {
        qcap2_audio_decoder_priv_t* p = (qcap2_audio_decoder_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->multithread = bMultiThread;
    }
}

void qcap2_audio_decoder_set_event(qcap2_audio_decoder_t* pThis, qcap2_event_t* pEvent) {
    if (pThis) {
        qcap2_audio_decoder_priv_t* p = (qcap2_audio_decoder_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->event = pEvent;
    }
}

void qcap2_audio_decoder_set_buffers(qcap2_audio_decoder_t* pThis, qcap2_rcbuffer_t** pBuffers) {
    // Currently unimplemented for audio buffers pooling but available in API
    (void)pThis;
    (void)pBuffers;
}

void qcap2_audio_decoder_set_payload_type(qcap2_audio_decoder_t* pThis, int nPayloadType) {
    if (pThis) {
        qcap2_audio_decoder_priv_t* p = (qcap2_audio_decoder_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->payload_type = nPayloadType;
    }
}

QRESULT qcap2_audio_decoder_start(qcap2_audio_decoder_t* pThis) {
    if (!pThis) return QCAP_RS_ERROR_GENERAL;
    qcap2_audio_decoder_priv_t* p = (qcap2_audio_decoder_priv_t*)pThis;
    std::lock_guard<std::mutex> lock(*(p->mtx));

    if (p->running) return QCAP_RS_SUCCESSFUL;

    ULONG type = 0, fmt = 0, ch = 0, bits = 0, freq = 0, rate = 0;
    qcap2_audio_encoder_property_get_property1(p->property, &type, &fmt, &ch, &bits, &freq, &rate);

    enum AVCodecID codec_id = qcap2_audio_format_to_ffmpeg_codec_id(fmt);
    if (codec_id == AV_CODEC_ID_NONE) {
        return QCAP_RS_ERROR_NON_SUPPORT;
    }

    const AVCodec* codec = avcodec_find_decoder(codec_id);
    if (!codec) {
        return QCAP_RS_ERROR_NON_SUPPORT;
    }

    p->avctx = avcodec_alloc_context3(codec);
    if (!p->avctx) {
        return QCAP_RS_ERROR_OUT_OF_MEMORY;
    }

    if (p->extra_data && p->extra_data_size > 0) {
        p->avctx->extradata = (uint8_t*)av_mallocz(p->extra_data_size + AV_INPUT_BUFFER_PADDING_SIZE);
        if (p->avctx->extradata) {
            memcpy(p->avctx->extradata, p->extra_data, p->extra_data_size);
            p->avctx->extradata_size = p->extra_data_size;
        }
    }

    av_channel_layout_default(&p->avctx->ch_layout, ch);
    p->avctx->sample_rate = freq;

    if (avcodec_open2(p->avctx, codec, nullptr) < 0) {
        avcodec_free_context(&p->avctx);
        return QCAP_RS_ERROR_GENERAL;
    }

    p->running = true;
    return QCAP_RS_SUCCESSFUL;
}

QRESULT qcap2_audio_decoder_stop(qcap2_audio_decoder_t* pThis) {
    if (!pThis) return QCAP_RS_ERROR_GENERAL;
    qcap2_audio_decoder_priv_t* p = (qcap2_audio_decoder_priv_t*)pThis;
    std::lock_guard<std::mutex> lock(*(p->mtx));

    p->running = false;
    if (p->avctx) {
        avcodec_free_context(&p->avctx);
    }
    while (!p->output_queue.empty()) {
        qcap2_rcbuffer_release(p->output_queue.front());
        p->output_queue.pop();
    }
    p->cv->notify_all();
    return QCAP_RS_SUCCESSFUL;
}

QRESULT qcap2_audio_decoder_push(qcap2_audio_decoder_t* pThis, qcap2_rcbuffer_t* pRCBuffer) {
    if (!pThis || !pRCBuffer) return QCAP_RS_ERROR_GENERAL;
    qcap2_audio_decoder_priv_t* p = (qcap2_audio_decoder_priv_t*)pThis;

    PVOID pData = qcap2_rcbuffer_lock_data(pRCBuffer);
    if (!pData) return QCAP_RS_ERROR_GENERAL;

    qcap2_av_packet_t* pPacket = (qcap2_av_packet_t*)pData;

    std::lock_guard<std::mutex> lock(*(p->mtx));
    if (!p->running || !p->avctx) {
        qcap2_rcbuffer_unlock_data(pRCBuffer);
        return QCAP_RS_ERROR_GENERAL;
    }

    AVPacket* av_pkt = av_packet_alloc();
    if (!av_pkt) {
        qcap2_rcbuffer_unlock_data(pRCBuffer);
        return QCAP_RS_ERROR_OUT_OF_MEMORY;
    }

    uint8_t* pInputBuffer = nullptr;
    int nInputSize = 0;
    qcap2_av_packet_get_buffer(pPacket, &pInputBuffer, &nInputSize);

    int64_t nPTS = 0, nDTS = 0;
    qcap2_av_packet_get_pts(pPacket, &nPTS);
    qcap2_av_packet_get_dts(pPacket, &nDTS);

    double dSampleTime = 0;
    qcap2_av_packet_get_sample_time(pPacket, &dSampleTime);

    av_pkt->data = pInputBuffer;
    av_pkt->size = nInputSize;
    av_pkt->pts = nPTS;
    av_pkt->dts = nDTS;

    int stream_idx = 0;
    BOOL is_key = FALSE;
    qcap2_av_packet_get_property(pPacket, &stream_idx, &is_key);
    if (is_key) {
        av_pkt->flags |= AV_PKT_FLAG_KEY;
    }

    int ret = avcodec_send_packet(p->avctx, av_pkt);
    av_packet_free(&av_pkt);
    qcap2_rcbuffer_unlock_data(pRCBuffer);

    if (ret < 0) {
        return QCAP_RS_ERROR_GENERAL;
    }

    AVFrame* frame = av_frame_alloc();
    while (ret >= 0) {
        ret = avcodec_receive_frame(p->avctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            av_frame_free(&frame);
            return QCAP_RS_ERROR_GENERAL;
        }

        qcap2_rcbuffer_t* out_rc = nullptr;
        qcap2_av_frame_t* new_frame = new qcap2_av_frame_t;
        qcap2_av_frame_init(new_frame);
        out_rc = qcap2_rcbuffer_new(new_frame, [](PVOID pData) {
            qcap2_av_frame_t* f = (qcap2_av_frame_t*)pData;
            if (f) {
                uint8_t* buf = nullptr;
                int stride = 0;
                qcap2_av_frame_get_buffer(f, &buf, &stride);
                if (buf) {
                    free(buf);
                }
                delete f;
            }
        });

        if (out_rc) {
            PVOID out_data = qcap2_rcbuffer_lock_data(out_rc);
            if (out_data) {
                qcap2_av_frame_t* out_frame = (qcap2_av_frame_t*)out_data;

                int ch = frame->ch_layout.nb_channels;
                int in_rate = frame->sample_rate;
                int format = frame->format;

                qcap2_av_frame_set_audio_property(out_frame, ch, format, in_rate, frame->nb_samples);

                int size = av_samples_get_buffer_size(nullptr, ch, frame->nb_samples, (enum AVSampleFormat)format, 1);

                uint8_t* out_buf = (uint8_t*)malloc(size);
                if (out_buf) {
                    uint8_t* out_ptrs[AV_NUM_DATA_POINTERS] = { nullptr };
                    av_samples_fill_arrays(out_ptrs, nullptr, out_buf, ch, frame->nb_samples, (enum AVSampleFormat)format, 1);
                    av_samples_copy(out_ptrs, frame->data, 0, 0, frame->nb_samples, ch, (enum AVSampleFormat)format);
                    qcap2_av_frame_set_buffer(out_frame, out_buf, size);
                }

                qcap2_av_frame_set_pts(out_frame, frame->pts);
                qcap2_av_frame_set_sample_time(out_frame, dSampleTime);

                qcap2_rcbuffer_unlock_data(out_rc);
                p->output_queue.push(out_rc);
            } else {
                qcap2_rcbuffer_release(out_rc);
            }
        }
        av_frame_unref(frame);
    }
    av_frame_free(&frame);

    p->cv->notify_all();
    if (p->event) {
        qcap2_event_notify(p->event);
    }

    return QCAP_RS_SUCCESSFUL;
}

QRESULT qcap2_audio_decoder_pop(qcap2_audio_decoder_t* pThis, qcap2_rcbuffer_t** ppRCBuffer) {
    if (!pThis || !ppRCBuffer) return QCAP_RS_ERROR_GENERAL;
    qcap2_audio_decoder_priv_t* p = (qcap2_audio_decoder_priv_t*)pThis;

    std::unique_lock<std::mutex> lock(*(p->mtx));
    p->cv->wait(lock, [p] { return !p->running || !p->output_queue.empty(); });

    if (!p->running && p->output_queue.empty()) {
        return QCAP_RS_ERROR_GENERAL;
    }

    *ppRCBuffer = p->output_queue.front();
    p->output_queue.pop();
    return QCAP_RS_SUCCESSFUL;
}

// ==============================================================================
// qcap2_frame_pool_t Implementation
// ==============================================================================

typedef struct qcap2_frame_pool_priv_t {
    std::mutex* mtx;

    int backend_type;
    int frame_count;

    // Video settings
    ULONG video_color_space;
    ULONG video_width;
    ULONG video_height;
    int video_align;
    int video_valign;
    ULONG video_width_border;
    ULONG video_height_border;
    BOOL video_mapped;

    // Audio settings
    ULONG audio_channels;
    ULONG audio_sample_fmt;
    ULONG audio_sample_freq;
    ULONG audio_frame_size;
    int audio_align;

    // Pool state
    bool running;
    bool is_video; // true if video properties were set, false if audio
    std::vector<qcap2_rcbuffer_t*> pool;

    qcap2_frame_pool_priv_t() {
        mtx = new std::mutex();
        backend_type = QCAP2_FRAME_POOL_BACKEND_TYPE_DEFAULT;
        frame_count = 4;

        video_color_space = QCAP_COLORSPACE_TYPE_BGR24;
        video_width = 0;
        video_height = 0;
        video_align = 16;
        video_valign = 1;
        video_width_border = 0;
        video_height_border = 0;
        video_mapped = FALSE;

        audio_channels = 0;
        audio_sample_fmt = 0;
        audio_sample_freq = 0;
        audio_frame_size = 0;
        audio_align = 1;

        running = false;
        is_video = true;
    }

    ~qcap2_frame_pool_priv_t() {
        cleanup();
        delete mtx;
    }

    void cleanup() {
        for (auto buf : pool) {
            qcap2_rcbuffer_release(buf);
        }
        pool.clear();
    }
} qcap2_frame_pool_priv_t;

qcap2_frame_pool_t* qcap2_frame_pool_new() {
    qcap2_frame_pool_priv_t* p = new qcap2_frame_pool_priv_t;
    return (qcap2_frame_pool_t*)p;
}

void qcap2_frame_pool_delete(qcap2_frame_pool_t* pThis) {
    if (pThis) {
        qcap2_frame_pool_stop(pThis);
        qcap2_frame_pool_priv_t* p = (qcap2_frame_pool_priv_t*)pThis;
        delete p;
    }
}

void qcap2_frame_pool_set_backend_type(qcap2_frame_pool_t* pThis, int nBackendType) {
    if (pThis) {
        qcap2_frame_pool_priv_t* p = (qcap2_frame_pool_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->backend_type = nBackendType;
    }
}

void qcap2_frame_pool_set_frame_count(qcap2_frame_pool_t* pThis, int nFrameCount) {
    if (pThis) {
        qcap2_frame_pool_priv_t* p = (qcap2_frame_pool_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->frame_count = nFrameCount;
    }
}

void qcap2_frame_pool_set_video_frame_align(qcap2_frame_pool_t* pThis, int nFrameAlign, int nFrameVAlign) {
    if (pThis) {
        qcap2_frame_pool_priv_t* p = (qcap2_frame_pool_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->video_align = nFrameAlign;
        p->video_valign = nFrameVAlign;
    }
}

void qcap2_frame_pool_set_audio_frame_align(qcap2_frame_pool_t* pThis, int nFrameAlign) {
    if (pThis) {
        qcap2_frame_pool_priv_t* p = (qcap2_frame_pool_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->audio_align = nFrameAlign;
    }
}

void qcap2_frame_pool_set_video_property(qcap2_frame_pool_t* pThis, ULONG nColorSpaceType, ULONG nFrameWidth, ULONG nFrameHeight) {
    if (pThis) {
        qcap2_frame_pool_priv_t* p = (qcap2_frame_pool_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->video_color_space = nColorSpaceType;
        p->video_width = nFrameWidth;
        p->video_height = nFrameHeight;
        p->is_video = true;
    }
}

void qcap2_frame_pool_set_video_property1(qcap2_frame_pool_t* pThis, ULONG nWidthBorder, ULONG nHeightBorder, BOOL bMapped) {
    if (pThis) {
        qcap2_frame_pool_priv_t* p = (qcap2_frame_pool_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->video_width_border = nWidthBorder;
        p->video_height_border = nHeightBorder;
        p->video_mapped = bMapped;
    }
}

void qcap2_frame_pool_set_audio_property(qcap2_frame_pool_t* pThis, ULONG nChannels, ULONG nSampleFmt, ULONG nSampleFrequency, ULONG nAudioFrameSize) {
    if (pThis) {
        qcap2_frame_pool_priv_t* p = (qcap2_frame_pool_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->audio_channels = nChannels;
        p->audio_sample_fmt = nSampleFmt;
        p->audio_sample_freq = nSampleFrequency;
        p->audio_frame_size = nAudioFrameSize;
        p->is_video = false;
    }
}

QRESULT qcap2_frame_pool_start(qcap2_frame_pool_t* pThis) {
    if (!pThis) return QCAP_RS_ERROR_GENERAL;
    qcap2_frame_pool_priv_t* p = (qcap2_frame_pool_priv_t*)pThis;
    std::lock_guard<std::mutex> lock(*(p->mtx));

    if (p->running) return QCAP_RS_SUCCESSFUL;

    // Pre-allocate frames
    int count = p->frame_count > 0 ? p->frame_count : 4;
    for (int i = 0; i < count; ++i) {
        qcap2_av_frame_t* frame = new qcap2_av_frame_t;
        qcap2_av_frame_init(frame);

        bool alloc_ok = false;
        if (p->is_video) {
            ULONG alloc_w = p->video_width + p->video_width_border * 2;
            ULONG alloc_h = p->video_height + p->video_height_border * 2;
            qcap2_av_frame_set_video_property(frame, p->video_color_space, alloc_w, alloc_h);
            alloc_ok = qcap2_av_frame_alloc_buffer(frame, p->video_align, p->video_valign);
        } else {
            qcap2_av_frame_set_audio_property(frame, p->audio_channels, p->audio_sample_fmt, p->audio_sample_freq, p->audio_frame_size);

            // Compute audio buffer size: channels * bytes_per_sample * frame_size
            int bytes_per_sample = av_get_bytes_per_sample((AVSampleFormat)p->audio_sample_fmt);
            if (bytes_per_sample <= 0) bytes_per_sample = 2; // fallback to 16-bit
            size_t buf_size = (size_t)p->audio_channels * bytes_per_sample * p->audio_frame_size;
            if (buf_size > 0) {
                uint8_t* audio_buf = (uint8_t*)malloc(buf_size);
                if (audio_buf) {
                    memset(audio_buf, 0, buf_size);
                    qcap2_av_frame_set_buffer(frame, audio_buf, (int)buf_size);
                    alloc_ok = true;
                }
            }
        }

        if (!alloc_ok) {
            delete frame;
            // Cleanup already allocated
            p->cleanup();
            return QCAP_RS_ERROR_OUT_OF_MEMORY;
        }

        qcap2_rcbuffer_t* rc = qcap2_rcbuffer_new(frame, [](PVOID pData) {
            qcap2_av_frame_t* f = (qcap2_av_frame_t*)pData;
            if (f) {
                // For video frames, free_buffer handles the owned buffer.
                // For audio frames, the buffer pointer was set externally via set_buffer,
                // so we need to free it manually before free_buffer clears pointers.
                uint8_t* buf = nullptr;
                int stride = 0;
                qcap2_av_frame_get_buffer(f, &buf, &stride);
                ULONG col = 0, w = 0, h = 0;
                qcap2_av_frame_get_video_property(f, &col, &w, &h);
                if (w == 0 && h == 0 && buf) {
                    // Audio frame: manually free the audio buffer
                    free(buf);
                } else {
                    // Video frame: use free_buffer
                    qcap2_av_frame_free_buffer(f);
                }
                delete f;
            }
        });

        if (!rc) {
            qcap2_av_frame_free_buffer(frame);
            delete frame;
            p->cleanup();
            return QCAP_RS_ERROR_OUT_OF_MEMORY;
        }

        p->pool.push_back(rc);
    }

    p->running = true;
    return QCAP_RS_SUCCESSFUL;
}

QRESULT qcap2_frame_pool_stop(qcap2_frame_pool_t* pThis) {
    if (!pThis) return QCAP_RS_ERROR_GENERAL;
    qcap2_frame_pool_priv_t* p = (qcap2_frame_pool_priv_t*)pThis;
    std::lock_guard<std::mutex> lock(*(p->mtx));

    p->running = false;
    p->cleanup();
    return QCAP_RS_SUCCESSFUL;
}

QRESULT qcap2_frame_pool_get_buffer(qcap2_frame_pool_t* pThis, qcap2_rcbuffer_t** ppBuffer) {
    if (!pThis || !ppBuffer) return QCAP_RS_ERROR_GENERAL;
    qcap2_frame_pool_priv_t* p = (qcap2_frame_pool_priv_t*)pThis;
    std::lock_guard<std::mutex> lock(*(p->mtx));

    if (!p->running) return QCAP_RS_ERROR_GENERAL;

    *ppBuffer = nullptr;

    // Find an idle buffer (use_count == 1 means only the pool holds a reference)
    for (auto buf : p->pool) {
        if (qcap2_rcbuffer_use_count(buf) == 1) {
            qcap2_rcbuffer_add_ref(buf);
            *ppBuffer = buf;
            return QCAP_RS_SUCCESSFUL;
        }
    }

    // No idle buffer available
    return QCAP_RS_ERROR_GENERAL;
}

// ==============================================================================
// qcap2_packet_pool_t Implementation
// ==============================================================================

typedef struct qcap2_packet_pool_priv_t {
    std::mutex* mtx;
    int packet_count;
    bool running;
    std::vector<qcap2_rcbuffer_t*> pool;

    qcap2_packet_pool_priv_t() {
        mtx = new std::mutex();
        packet_count = 4;
        running = false;
    }

    ~qcap2_packet_pool_priv_t() {
        cleanup();
        delete mtx;
    }

    void cleanup() {
        for (auto buf : pool) {
            qcap2_rcbuffer_release(buf);
        }
        pool.clear();
    }
} qcap2_packet_pool_priv_t;

qcap2_packet_pool_t* qcap2_packet_pool_new() {
    qcap2_packet_pool_priv_t* p = new qcap2_packet_pool_priv_t;
    return (qcap2_packet_pool_t*)p;
}

void qcap2_packet_pool_delete(qcap2_packet_pool_t* pThis) {
    if (pThis) {
        qcap2_packet_pool_stop(pThis);
        qcap2_packet_pool_priv_t* p = (qcap2_packet_pool_priv_t*)pThis;
        delete p;
    }
}

void qcap2_packet_pool_set_packet_count(qcap2_packet_pool_t* pThis, int nPacketCount) {
    if (pThis) {
        qcap2_packet_pool_priv_t* p = (qcap2_packet_pool_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->packet_count = nPacketCount;
    }
}

QRESULT qcap2_packet_pool_start(qcap2_packet_pool_t* pThis) {
    if (!pThis) return QCAP_RS_ERROR_GENERAL;
    qcap2_packet_pool_priv_t* p = (qcap2_packet_pool_priv_t*)pThis;
    std::lock_guard<std::mutex> lock(*(p->mtx));

    if (p->running) return QCAP_RS_SUCCESSFUL;

    int count = p->packet_count > 0 ? p->packet_count : 4;
    for (int i = 0; i < count; ++i) {
        qcap2_av_packet_t* packet = new qcap2_av_packet_t;
        qcap2_av_packet_init(packet);
        // Initially no buffer is allocated until size is known

        qcap2_rcbuffer_t* rc = qcap2_rcbuffer_new(packet, [](PVOID pData) {
            qcap2_av_packet_t* pkt = (qcap2_av_packet_t*)pData;
            if (pkt) {
                qcap2_av_packet_free_buffer(pkt);
                delete pkt;
            }
        });

        if (!rc) {
            qcap2_av_packet_free_buffer(packet);
            delete packet;
            p->cleanup();
            return QCAP_RS_ERROR_OUT_OF_MEMORY;
        }

        p->pool.push_back(rc);
    }

    p->running = true;
    return QCAP_RS_SUCCESSFUL;
}

QRESULT qcap2_packet_pool_stop(qcap2_packet_pool_t* pThis) {
    if (!pThis) return QCAP_RS_ERROR_GENERAL;
    qcap2_packet_pool_priv_t* p = (qcap2_packet_pool_priv_t*)pThis;
    std::lock_guard<std::mutex> lock(*(p->mtx));

    p->running = false;
    p->cleanup();
    return QCAP_RS_SUCCESSFUL;
}

QRESULT qcap2_packet_pool_get_buffer(qcap2_packet_pool_t* pThis, int nPacketSize, qcap2_rcbuffer_t** ppBuffer) {
    if (!pThis || !ppBuffer) return QCAP_RS_ERROR_GENERAL;
    qcap2_packet_pool_priv_t* p = (qcap2_packet_pool_priv_t*)pThis;
    std::lock_guard<std::mutex> lock(*(p->mtx));

    if (!p->running) return QCAP_RS_ERROR_GENERAL;

    *ppBuffer = nullptr;

    for (auto buf : p->pool) {
        if (qcap2_rcbuffer_use_count(buf) == 1) {
            qcap2_av_packet_t* pkt = (qcap2_av_packet_t*)qcap2_rcbuffer_get_data(buf);
            if (pkt) {
                if (nPacketSize > 0) {
                    uint8_t* current_buf = nullptr;
                    int current_size = 0;
                    qcap2_av_packet_get_buffer(pkt, &current_buf, &current_size);

                    if (!current_buf || current_size < nPacketSize) {
                        if (!qcap2_av_packet_alloc_buffer(pkt, nPacketSize)) {
                            return QCAP_RS_ERROR_OUT_OF_MEMORY;
                        }
                    }
                }

                qcap2_rcbuffer_add_ref(buf);
                *ppBuffer = buf;
                return QCAP_RS_SUCCESSFUL;
            }
        }
    }

    return QCAP_RS_ERROR_GENERAL;
}

// ==============================================================================
// qcap2_video_scaler_t Implementation
// ==============================================================================

static AVPixelFormat qcap2_to_ffmpeg_pix_fmt(ULONG nColorSpaceType) {
    switch (nColorSpaceType) {
        case QCAP_COLORSPACE_TYPE_RGB24: return AV_PIX_FMT_RGB24;
        case QCAP_COLORSPACE_TYPE_BGR24: return AV_PIX_FMT_BGR24;
        case QCAP_COLORSPACE_TYPE_ARGB32: return AV_PIX_FMT_ARGB;
        case QCAP_COLORSPACE_TYPE_ABGR32: return AV_PIX_FMT_ABGR;
        case QCAP_COLORSPACE_TYPE_YUY2:  return AV_PIX_FMT_YUYV422;
        case QCAP_COLORSPACE_TYPE_UYVY:  return AV_PIX_FMT_UYVY422;
        case QCAP_COLORSPACE_TYPE_Y800:  return AV_PIX_FMT_GRAY8;
        case QCAP_COLORSPACE_TYPE_I420:  return AV_PIX_FMT_YUV420P;
        case QCAP_COLORSPACE_TYPE_YV12:  return AV_PIX_FMT_YUV420P;
        case QCAP_COLORSPACE_TYPE_YV24:  return AV_PIX_FMT_YUV444P;
        case QCAP_COLORSPACE_TYPE_NV12:  return AV_PIX_FMT_NV12;
        case QCAP_COLORSPACE_TYPE_P010:  return AV_PIX_FMT_P010LE;
        case QCAP_COLORSPACE_TYPE_P210:  return AV_PIX_FMT_P210LE;
        case QCAP_COLORSPACE_TYPE_Y416:  return AV_PIX_FMT_NONE;
        default:                         return AV_PIX_FMT_NONE;
    }
}

typedef struct qcap2_video_scaler_priv_t {
    std::mutex* mtx;
    std::condition_variable* cv;
    std::queue<qcap2_rcbuffer_t*> output_queue;

    // Output target configuration
    ULONG out_color_space;
    ULONG out_width;
    ULONG out_height;
    BOOL out_interleaved;
    double out_fps;

    int max_frames;
    int align;
    int valign;
    bool multithread;
    bool auto_run;
    qcap2_event_t* event;

    // Registered pre-allocated buffers
    std::vector<qcap2_rcbuffer_t*> registered_buffers;

    // Crop settings
    int crop_x;
    int crop_y;
    int crop_w;
    int crop_h;

    // Other configurations
    int src_color_space;
    int dst_color_range;
    int src_ss_type;
    int dst_ss_type;
    int src_buffer_hint;
    int dst_buffer_hint;
    int backend_type;

    // Filter Graph setting
    std::string filter_graph_str;

    // Running state
    bool running;

    // Direct mode: swscale cached context
    SwsContext* sws_ctx;
    ULONG cached_in_color;
    ULONG cached_in_w;
    ULONG cached_in_h;

    // Filter mode: avfilter cached context
    AVFilterGraph* filter_graph;
    AVFilterContext* src_ctx;
    AVFilterContext* sink_ctx;
    ULONG filter_in_color;
    ULONG filter_in_w;
    ULONG filter_in_h;

    qcap2_video_scaler_priv_t() {
        mtx = new std::mutex();
        cv = new std::condition_variable();
        out_color_space = QCAP_COLORSPACE_TYPE_BGR24;
        out_width = 1920;
        out_height = 1080;
        out_interleaved = FALSE;
        out_fps = 30.0;

        max_frames = 0;
        align = 16;
        valign = 1;
        multithread = false;
        auto_run = true;
        event = nullptr;

        crop_x = 0;
        crop_y = 0;
        crop_w = 0;
        crop_h = 0;

        src_color_space = 0;
        dst_color_range = 0;
        src_ss_type = 0;
        dst_ss_type = 0;
        src_buffer_hint = 0;
        dst_buffer_hint = 0;
        backend_type = 0;

        running = false;
        sws_ctx = nullptr;
        cached_in_color = 0;
        cached_in_w = 0;
        cached_in_h = 0;

        filter_graph = nullptr;
        src_ctx = nullptr;
        sink_ctx = nullptr;
        filter_in_color = 0;
        filter_in_w = 0;
        filter_in_h = 0;
    }

    ~qcap2_video_scaler_priv_t() {
        cleanup();
        delete cv;
        delete mtx;
    }

    void cleanup() {
        if (sws_ctx) {
            sws_freeContext(sws_ctx);
            sws_ctx = nullptr;
        }
        if (filter_graph) {
            avfilter_graph_free(&filter_graph);
            filter_graph = nullptr;
            src_ctx = nullptr;
            sink_ctx = nullptr;
        }
        for (auto buf : registered_buffers) {
            qcap2_rcbuffer_release(buf);
        }
        registered_buffers.clear();

        while (!output_queue.empty()) {
            qcap2_rcbuffer_t* buf = output_queue.front();
            output_queue.pop();
            qcap2_rcbuffer_release(buf);
        }
    }
} qcap2_video_scaler_priv_t;

static bool init_filter_graph(qcap2_video_scaler_priv_t* p, ULONG in_color, ULONG in_w, ULONG in_h) {
    if (p->filter_graph) {
        avfilter_graph_free(&p->filter_graph);
        p->filter_graph = nullptr;
        p->src_ctx = nullptr;
        p->sink_ctx = nullptr;
    }

    AVPixelFormat in_pix_fmt = qcap2_to_ffmpeg_pix_fmt(in_color);
    AVPixelFormat out_pix_fmt = qcap2_to_ffmpeg_pix_fmt(p->out_color_space);
    if (in_pix_fmt == AV_PIX_FMT_NONE || out_pix_fmt == AV_PIX_FMT_NONE) {
        return false;
    }

    p->filter_graph = avfilter_graph_alloc();
    if (!p->filter_graph) return false;

    const AVFilter* src_filter = avfilter_get_by_name("buffer");
    const AVFilter* sink_filter = avfilter_get_by_name("buffersink");
    if (!src_filter || !sink_filter) return false;

    char args[512];
    snprintf(args, sizeof(args),
             "video_size=%lux%lu:pix_fmt=%d:time_base=1/90000:pixel_aspect=1/1",
             in_w, in_h, (int)in_pix_fmt);

    int ret = avfilter_graph_create_filter(&p->src_ctx, src_filter, "in", args, nullptr, p->filter_graph);
    if (ret < 0) return false;

    ret = avfilter_graph_create_filter(&p->sink_ctx, sink_filter, "out", nullptr, nullptr, p->filter_graph);
    if (ret < 0) return false;

    enum AVPixelFormat pix_fmts[] = { out_pix_fmt, AV_PIX_FMT_NONE };
    ret = av_opt_set_int_list(p->sink_ctx, "pix_fmts", pix_fmts, AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) return false;

    AVFilterInOut* outputs = avfilter_inout_alloc();
    AVFilterInOut* inputs = avfilter_inout_alloc();
    if (!outputs || !inputs) {
        avfilter_inout_free(&outputs);
        avfilter_inout_free(&inputs);
        return false;
    }

    outputs->name = av_strdup("in");
    outputs->filter_ctx = p->src_ctx;
    outputs->pad_idx = 0;
    outputs->next = nullptr;

    inputs->name = av_strdup("out");
    inputs->filter_ctx = p->sink_ctx;
    inputs->pad_idx = 0;
    inputs->next = nullptr;

    ret = avfilter_graph_parse_ptr(p->filter_graph, p->filter_graph_str.c_str(), &inputs, &outputs, nullptr);
    avfilter_inout_free(&outputs);
    avfilter_inout_free(&inputs);
    if (ret < 0) return false;

    ret = avfilter_graph_config(p->filter_graph, nullptr);
    if (ret < 0) return false;

    p->filter_in_color = in_color;
    p->filter_in_w = in_w;
    p->filter_in_h = in_h;
    return true;
}

static bool init_sws(qcap2_video_scaler_priv_t* p, ULONG in_color, ULONG in_w, ULONG in_h) {
    AVPixelFormat in_pix_fmt = qcap2_to_ffmpeg_pix_fmt(in_color);
    AVPixelFormat out_pix_fmt = qcap2_to_ffmpeg_pix_fmt(p->out_color_space);
    if (in_pix_fmt == AV_PIX_FMT_NONE || out_pix_fmt == AV_PIX_FMT_NONE) {
        return false;
    }

    int src_w = in_w;
    int src_h = in_h;
    if (p->crop_w > 0 && p->crop_h > 0) {
        src_w = p->crop_w;
        src_h = p->crop_h;
    }

    p->sws_ctx = sws_getCachedContext(p->sws_ctx,
                                      src_w, src_h, in_pix_fmt,
                                      p->out_width, p->out_height, out_pix_fmt,
                                      SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!p->sws_ctx) {
        return false;
    }

    p->cached_in_color = in_color;
    p->cached_in_w = in_w;
    p->cached_in_h = in_h;
    return true;
}

static qcap2_rcbuffer_t* get_output_buffer(qcap2_video_scaler_priv_t* p) {
    if (!p->registered_buffers.empty()) {
        for (auto buf : p->registered_buffers) {
            if (qcap2_rcbuffer_use_count(buf) == 1) {
                qcap2_rcbuffer_add_ref(buf);
                return buf;
            }
        }
    }

    qcap2_av_frame_t* out_frame = new qcap2_av_frame_t;
    qcap2_av_frame_init(out_frame);
    qcap2_av_frame_set_video_property(out_frame, p->out_color_space, p->out_width, p->out_height);
    if (!qcap2_av_frame_alloc_buffer(out_frame, p->align, p->valign)) {
        delete out_frame;
        return nullptr;
    }

    qcap2_rcbuffer_t* rc = qcap2_rcbuffer_new(out_frame, [](PVOID pData) {
        qcap2_av_frame_t* f = (qcap2_av_frame_t*)pData;
        qcap2_av_frame_free_buffer(f);
        delete f;
    });

    return rc;
}

qcap2_video_scaler_t* qcap2_video_scaler_new() {
    qcap2_video_scaler_priv_t* p = new qcap2_video_scaler_priv_t;
    return (qcap2_video_scaler_t*)p;
}

void qcap2_video_scaler_delete(qcap2_video_scaler_t* pThis) {
    if (pThis) {
        qcap2_video_scaler_stop(pThis);
        qcap2_video_scaler_priv_t* p = (qcap2_video_scaler_priv_t*)pThis;
        delete p;
    }
}

void qcap2_video_scaler_set_video_format(qcap2_video_scaler_t* pThis, qcap2_video_format_t* pVideoFormat) {
    if (pThis && pVideoFormat) {
        qcap2_video_scaler_priv_t* p = (qcap2_video_scaler_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        ULONG color = 0, w = 0, h = 0;
        BOOL inter = FALSE;
        double fps = 0.0;
        qcap2_video_format_get_property(pVideoFormat, &color, &w, &h, &inter, &fps);
        p->out_color_space = color;
        p->out_width = w;
        p->out_height = h;
        p->out_interleaved = inter;
        p->out_fps = fps;
    }
}

void qcap2_video_scaler_get_video_format(qcap2_video_scaler_t* pThis, qcap2_video_format_t* pVideoFormat) {
    if (pThis && pVideoFormat) {
        qcap2_video_scaler_priv_t* p = (qcap2_video_scaler_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        qcap2_video_format_set_property(pVideoFormat, p->out_color_space, p->out_width, p->out_height, p->out_interleaved, p->out_fps);
    }
}

void qcap2_video_scaler_set_backend_type(qcap2_video_scaler_t* pThis, int nBackendType) {
    if (pThis) {
        qcap2_video_scaler_priv_t* p = (qcap2_video_scaler_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->backend_type = nBackendType;
    }
}

void qcap2_video_scaler_set_frame_count(qcap2_video_scaler_t* pThis, int nFrameCount) {
    if (pThis) {
        qcap2_video_scaler_priv_t* p = (qcap2_video_scaler_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->max_frames = nFrameCount;
    }
}

void qcap2_video_scaler_set_frame_align(qcap2_video_scaler_t* pThis, int nFrameAlign) {
    if (pThis) {
        qcap2_video_scaler_priv_t* p = (qcap2_video_scaler_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->align = nFrameAlign;
    }
}

void qcap2_video_scaler_set_frame_valign(qcap2_video_scaler_t* pThis, int nFrameVAlign) {
    if (pThis) {
        qcap2_video_scaler_priv_t* p = (qcap2_video_scaler_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->valign = nFrameVAlign;
    }
}

void qcap2_video_scaler_set_multithread(qcap2_video_scaler_t* pThis, bool bMultiThread) {
    if (pThis) {
        qcap2_video_scaler_priv_t* p = (qcap2_video_scaler_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->multithread = bMultiThread;
    }
}

void qcap2_video_scaler_set_auto_run(qcap2_video_scaler_t* pThis, bool bAutoRun) {
    if (pThis) {
        qcap2_video_scaler_priv_t* p = (qcap2_video_scaler_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->auto_run = bAutoRun;
    }
}

void qcap2_video_scaler_set_event(qcap2_video_scaler_t* pThis, qcap2_event_t* pEvent) {
    if (pThis) {
        qcap2_video_scaler_priv_t* p = (qcap2_video_scaler_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->event = pEvent;
    }
}

void qcap2_video_scaler_set_buffers(qcap2_video_scaler_t* pThis, qcap2_rcbuffer_t** pBuffers) {
    if (pThis) {
        qcap2_video_scaler_priv_t* p = (qcap2_video_scaler_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        for (auto buf : p->registered_buffers) {
            qcap2_rcbuffer_release(buf);
        }
        p->registered_buffers.clear();
        if (pBuffers) {
            for (int i = 0; pBuffers[i] != nullptr; ++i) {
                qcap2_rcbuffer_add_ref(pBuffers[i]);
                p->registered_buffers.push_back(pBuffers[i]);
            }
        }
    }
}

void qcap2_video_scaler_set_src_color_space(qcap2_video_scaler_t* pThis, int nSrcColorSpace) {
    if (pThis) {
        qcap2_video_scaler_priv_t* p = (qcap2_video_scaler_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->src_color_space = nSrcColorSpace;
    }
}

void qcap2_video_scaler_set_dst_color_range(qcap2_video_scaler_t* pThis, int nDstColorRange) {
    if (pThis) {
        qcap2_video_scaler_priv_t* p = (qcap2_video_scaler_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->dst_color_range = nDstColorRange;
    }
}

void qcap2_video_scaler_set_src_ss_type(qcap2_video_scaler_t* pThis, int nSrcSSType) {
    if (pThis) {
        qcap2_video_scaler_priv_t* p = (qcap2_video_scaler_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->src_ss_type = nSrcSSType;
    }
}

void qcap2_video_scaler_set_dst_ss_type(qcap2_video_scaler_t* pThis, int nDstSSType) {
    if (pThis) {
        qcap2_video_scaler_priv_t* p = (qcap2_video_scaler_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->dst_ss_type = nDstSSType;
    }
}

void qcap2_video_scaler_set_crop(qcap2_video_scaler_t* pThis, int x, int y, int w, int h) {
    if (pThis) {
        qcap2_video_scaler_priv_t* p = (qcap2_video_scaler_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->crop_x = x;
        p->crop_y = y;
        p->crop_w = w;
        p->crop_h = h;
    }
}

void qcap2_video_scaler_set_src_buffer_hint(qcap2_video_scaler_t* pThis, int nSrcBufferHint) {
    if (pThis) {
        qcap2_video_scaler_priv_t* p = (qcap2_video_scaler_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->src_buffer_hint = nSrcBufferHint;
    }
}

void qcap2_video_scaler_set_dst_buffer_hint(qcap2_video_scaler_t* pThis, int nDstBufferHint) {
    if (pThis) {
        qcap2_video_scaler_priv_t* p = (qcap2_video_scaler_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->dst_buffer_hint = nDstBufferHint;
    }
}

void qcap2_video_scaler_set_filter_graph(qcap2_video_scaler_t* pThis, const char* strFilterGraph) {
    if (pThis) {
        qcap2_video_scaler_priv_t* p = (qcap2_video_scaler_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->filter_graph_str = strFilterGraph ? strFilterGraph : "";
    }
}

QRESULT qcap2_video_scaler_start(qcap2_video_scaler_t* pThis) {
    if (pThis) {
        qcap2_video_scaler_priv_t* p = (qcap2_video_scaler_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->running = true;
        return QCAP_RS_SUCCESSFUL;
    }
    return QCAP_RS_ERROR_GENERAL;
}

QRESULT qcap2_video_scaler_stop(qcap2_video_scaler_t* pThis) {
    if (pThis) {
        qcap2_video_scaler_priv_t* p = (qcap2_video_scaler_priv_t*)pThis;
        std::lock_guard<std::mutex> lock(*(p->mtx));
        p->running = false;
        p->cleanup();
        return QCAP_RS_SUCCESSFUL;
    }
    return QCAP_RS_ERROR_GENERAL;
}

QRESULT qcap2_video_scaler_run(qcap2_video_scaler_t* pThis) {
    (void)pThis;
    return QCAP_RS_SUCCESSFUL;
}

QRESULT qcap2_video_scaler_push(qcap2_video_scaler_t* pThis, qcap2_rcbuffer_t* pRCBuffer) {
    if (!pThis || !pRCBuffer) return QCAP_RS_ERROR_GENERAL;
    qcap2_video_scaler_priv_t* p = (qcap2_video_scaler_priv_t*)pThis;

    PVOID pData = qcap2_rcbuffer_lock_data(pRCBuffer);
    if (!pData) return QCAP_RS_ERROR_GENERAL;

    qcap2_av_frame_t* pFrame = (qcap2_av_frame_t*)pData;
    ULONG in_color = 0, in_w = 0, in_h = 0;
    qcap2_av_frame_get_video_property(pFrame, &in_color, &in_w, &in_h);

    uint8_t* in_ptrs[4] = { nullptr };
    int in_strides[4] = { 0 };
    qcap2_av_frame_get_buffer1(pFrame, in_ptrs, in_strides);

    int64_t nPTS = 0;
    qcap2_av_frame_get_pts(pFrame, &nPTS);

    double dSampleTime = 0.0;
    qcap2_av_frame_get_sample_time(pFrame, &dSampleTime);

    int nFieldType = 0;
    qcap2_av_frame_get_field_type(pFrame, &nFieldType);

    std::lock_guard<std::mutex> lock(*(p->mtx));
    if (!p->running) {
        qcap2_rcbuffer_unlock_data(pRCBuffer);
        return QCAP_RS_ERROR_GENERAL;
    }

    if (!p->filter_graph_str.empty()) {
        // --- Filter Graph Mode ---
        if (!p->filter_graph || p->filter_in_color != in_color || p->filter_in_w != in_w || p->filter_in_h != in_h) {
            if (!init_filter_graph(p, in_color, in_w, in_h)) {
                qcap2_rcbuffer_unlock_data(pRCBuffer);
                return QCAP_RS_ERROR_GENERAL;
            }
        }

        AVFrame* f = av_frame_alloc();
        if (!f) {
            qcap2_rcbuffer_unlock_data(pRCBuffer);
            return QCAP_RS_ERROR_OUT_OF_MEMORY;
        }

        f->width = in_w;
        f->height = in_h;
        f->format = qcap2_to_ffmpeg_pix_fmt(in_color);
        for (int i = 0; i < 4; ++i) {
            f->data[i] = in_ptrs[i];
            f->linesize[i] = in_strides[i];
        }
        f->pts = nPTS;

        int ret = av_buffersrc_add_frame_flags(p->src_ctx, f, AV_BUFFERSRC_FLAG_KEEP_REF);
        av_frame_free(&f);
        qcap2_rcbuffer_unlock_data(pRCBuffer);

        if (ret < 0) {
            return QCAP_RS_ERROR_GENERAL;
        }

        AVFrame* filtered_frame = av_frame_alloc();
        if (!filtered_frame) return QCAP_RS_ERROR_OUT_OF_MEMORY;

        ret = av_buffersink_get_frame(p->sink_ctx, filtered_frame);
        if (ret < 0) {
            av_frame_free(&filtered_frame);
            return QCAP_RS_SUCCESSFUL;
        }

        qcap2_rcbuffer_t* out_rc = get_output_buffer(p);
        if (!out_rc) {
            av_frame_free(&filtered_frame);
            return QCAP_RS_ERROR_GENERAL;
        }

        PVOID out_data = qcap2_rcbuffer_lock_data(out_rc);
        if (!out_data) {
            qcap2_rcbuffer_release(out_rc);
            av_frame_free(&filtered_frame);
            return QCAP_RS_ERROR_GENERAL;
        }

        qcap2_av_frame_t* out_frame = (qcap2_av_frame_t*)out_data;
        uint8_t* out_ptrs[4] = { nullptr };
        int out_strides[4] = { 0 };
        qcap2_av_frame_get_buffer1(out_frame, out_ptrs, out_strides);

        for (int i = 0; i < 4 && filtered_frame->data[i]; ++i) {
            int copy_h = (i == 0) ? filtered_frame->height : (filtered_frame->height + 1) / 2;
            if (filtered_frame->format == AV_PIX_FMT_RGB24 || filtered_frame->format == AV_PIX_FMT_BGR24 ||
                filtered_frame->format == AV_PIX_FMT_ARGB || filtered_frame->format == AV_PIX_FMT_ABGR) {
                copy_h = filtered_frame->height;
            } else if (filtered_frame->format == AV_PIX_FMT_YUV444P) {
                copy_h = filtered_frame->height;
            }
            int line_bytes = std::min(filtered_frame->linesize[i], out_strides[i]);
            for (int r = 0; r < copy_h; ++r) {
                memcpy(out_ptrs[i] + r * out_strides[i], filtered_frame->data[i] + r * filtered_frame->linesize[i], line_bytes);
            }
        }

        qcap2_av_frame_set_pts(out_frame, filtered_frame->pts);
        qcap2_av_frame_set_sample_time(out_frame, dSampleTime);
        qcap2_av_frame_set_field_type(out_frame, nFieldType);
        qcap2_av_frame_set_video_property(out_frame, p->out_color_space, filtered_frame->width, filtered_frame->height);

        qcap2_rcbuffer_unlock_data(out_rc);
        av_frame_free(&filtered_frame);

        p->output_queue.push(out_rc);
        p->cv->notify_all();

        if (p->event) {
            qcap2_event_notify(p->event);
        }

    } else {
        // --- Direct swscale Mode ---
        if (!p->sws_ctx || p->cached_in_color != in_color || p->cached_in_w != in_w || p->cached_in_h != in_h) {
            if (!init_sws(p, in_color, in_w, in_h)) {
                qcap2_rcbuffer_unlock_data(pRCBuffer);
                return QCAP_RS_ERROR_GENERAL;
            }
        }

        uint8_t* src_ptrs[4];
        int src_strides[4];
        for (int i = 0; i < 4; ++i) {
            src_ptrs[i] = in_ptrs[i];
            src_strides[i] = in_strides[i];
        }

        // Swap U and V planes for YV12 and YV24
        if (in_color == QCAP_COLORSPACE_TYPE_YV12 || in_color == QCAP_COLORSPACE_TYPE_YV24) {
            std::swap(src_ptrs[1], src_ptrs[2]);
            std::swap(src_strides[1], src_strides[2]);
        }

        int src_h = in_h;
        if (p->crop_w > 0 && p->crop_h > 0) {
            src_h = p->crop_h;
            AVPixelFormat in_pix_fmt = qcap2_to_ffmpeg_pix_fmt(in_color);
            if (in_pix_fmt != AV_PIX_FMT_NONE) {
                if (in_pix_fmt == AV_PIX_FMT_RGB24 || in_pix_fmt == AV_PIX_FMT_BGR24) {
                    src_ptrs[0] += p->crop_y * src_strides[0] + p->crop_x * 3;
                } else if (in_pix_fmt == AV_PIX_FMT_ARGB || in_pix_fmt == AV_PIX_FMT_ABGR) {
                    src_ptrs[0] += p->crop_y * src_strides[0] + p->crop_x * 4;
                } else if (in_pix_fmt == AV_PIX_FMT_YUYV422 || in_pix_fmt == AV_PIX_FMT_UYVY422) {
                    src_ptrs[0] += p->crop_y * src_strides[0] + p->crop_x * 2;
                } else if (in_pix_fmt == AV_PIX_FMT_GRAY8) {
                    src_ptrs[0] += p->crop_y * src_strides[0] + p->crop_x;
                } else if (in_pix_fmt == AV_PIX_FMT_YUV420P) {
                    src_ptrs[0] += p->crop_y * src_strides[0] + p->crop_x;
                    src_ptrs[1] += (p->crop_y / 2) * src_strides[1] + (p->crop_x / 2);
                    src_ptrs[2] += (p->crop_y / 2) * src_strides[2] + (p->crop_x / 2);
                } else if (in_pix_fmt == AV_PIX_FMT_YUV444P) {
                    src_ptrs[0] += p->crop_y * src_strides[0] + p->crop_x;
                    src_ptrs[1] += p->crop_y * src_strides[1] + p->crop_x;
                    src_ptrs[2] += p->crop_y * src_strides[2] + p->crop_x;
                } else if (in_pix_fmt == AV_PIX_FMT_NV12 || in_pix_fmt == AV_PIX_FMT_P010LE || in_pix_fmt == AV_PIX_FMT_P210LE) {
                    int bpp = (in_pix_fmt == AV_PIX_FMT_NV12) ? 1 : 2;
                    src_ptrs[0] += p->crop_y * src_strides[0] + p->crop_x * bpp;
                    int chroma_y_div = (in_pix_fmt == AV_PIX_FMT_P210LE) ? 1 : 2;
                    src_ptrs[1] += (p->crop_y / chroma_y_div) * src_strides[1] + p->crop_x * 2;
                }
            }
        }

        qcap2_rcbuffer_t* out_rc = get_output_buffer(p);
        qcap2_rcbuffer_unlock_data(pRCBuffer);

        if (!out_rc) return QCAP_RS_ERROR_GENERAL;

        PVOID out_data = qcap2_rcbuffer_lock_data(out_rc);
        if (!out_data) {
            qcap2_rcbuffer_release(out_rc);
            return QCAP_RS_ERROR_GENERAL;
        }

        qcap2_av_frame_t* out_frame = (qcap2_av_frame_t*)out_data;
        uint8_t* out_ptrs[4] = { nullptr };
        int out_strides[4] = { 0 };
        qcap2_av_frame_get_buffer1(out_frame, out_ptrs, out_strides);

        uint8_t* dst_ptrs[4];
        int dst_strides[4];
        for (int i = 0; i < 4; ++i) {
            dst_ptrs[i] = out_ptrs[i];
            dst_strides[i] = out_strides[i];
        }

        // Swap U and V planes for YV12 and YV24 target outputs
        if (p->out_color_space == QCAP_COLORSPACE_TYPE_YV12 || p->out_color_space == QCAP_COLORSPACE_TYPE_YV24) {
            std::swap(dst_ptrs[1], dst_ptrs[2]);
            std::swap(dst_strides[1], dst_strides[2]);
        }

        int ret = sws_scale(p->sws_ctx, src_ptrs, src_strides, 0, src_h, dst_ptrs, dst_strides);
        if (ret < 0) {
            qcap2_rcbuffer_unlock_data(out_rc);
            qcap2_rcbuffer_release(out_rc);
            return QCAP_RS_ERROR_GENERAL;
        }

        qcap2_av_frame_set_pts(out_frame, nPTS);
        qcap2_av_frame_set_sample_time(out_frame, dSampleTime);
        qcap2_av_frame_set_field_type(out_frame, nFieldType);

        qcap2_rcbuffer_unlock_data(out_rc);

        p->output_queue.push(out_rc);
        p->cv->notify_all();

        if (p->event) {
            qcap2_event_notify(p->event);
        }
    }

    return QCAP_RS_SUCCESSFUL;
}

QRESULT qcap2_video_scaler_pop(qcap2_video_scaler_t* pThis, qcap2_rcbuffer_t** ppRCBuffer) {
    if (!pThis || !ppRCBuffer) return QCAP_RS_ERROR_GENERAL;
    qcap2_video_scaler_priv_t* p = (qcap2_video_scaler_priv_t*)pThis;

    std::unique_lock<std::mutex> lock(*(p->mtx));
    p->cv->wait(lock, [p] { return !p->running || !p->output_queue.empty(); });

    if (!p->running && p->output_queue.empty()) {
        return QCAP_RS_ERROR_GENERAL;
    }

    *ppRCBuffer = p->output_queue.front();
    p->output_queue.pop();
    return QCAP_RS_SUCCESSFUL;
}

#ifdef __cplusplus
}
#endif
