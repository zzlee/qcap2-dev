#include "qcap2.processing.h"
#include "qcap2.buffer.h"
#include "qcap2.sync.h"
#include <mutex>
#include <queue>
#include <vector>
#include <atomic>
#include <thread>
#include <condition_variable>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>

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

#ifdef __cplusplus
}
#endif
