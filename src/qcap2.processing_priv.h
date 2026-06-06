#ifndef QCAP2_PROCESSING_PRIV_H
#define QCAP2_PROCESSING_PRIV_H

#include "qcap2.processing.h"
#include "qcap2.buffer.h"
#include "qcap2.sync.h"
#include "qcap2.formats.h"
#include <mutex>
#include <queue>
#include <atomic>
#include <condition_variable>
#include <stdlib.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

typedef struct qcap2_audio_encoder_priv_t {
    std::mutex* mtx;
    std::condition_variable* cv;
    std::queue<qcap2_rcbuffer_t*> output_queue;
    qcap2_rcbuffer_queue_t* input_recycled_queue;
    qcap2_rcbuffer_queue_t* output_recycled_queue;

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
        input_recycled_queue = qcap2_rcbuffer_queue_new();
        if (input_recycled_queue) {
            qcap2_rcbuffer_queue_start(input_recycled_queue);
        }
        output_recycled_queue = qcap2_rcbuffer_queue_new();
        if (output_recycled_queue) {
            qcap2_rcbuffer_queue_start(output_recycled_queue);
        }
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
        if (input_recycled_queue) {
            qcap2_rcbuffer_queue_delete(input_recycled_queue);
        }
        if (output_recycled_queue) {
            qcap2_rcbuffer_queue_delete(output_recycled_queue);
        }
        delete cv;
        delete mtx;
    }
} qcap2_audio_encoder_priv_t;

typedef struct qcap2_video_encoder_priv_t {
    std::mutex* mtx;
    std::condition_variable* cv;
    std::queue<qcap2_rcbuffer_t*> output_queue;
    qcap2_rcbuffer_queue_t* input_recycled_queue;
    qcap2_rcbuffer_queue_t* output_recycled_queue;

    // Encoder property (owned reference)
    qcap2_video_encoder_property_t* enc_prop;

    // Dynamic property (owned reference)
    qcap2_video_encoder_dynamic_property_t* dyn_prop;

    // Extra data
    uint8_t* extra_data;
    int extra_data_size;

    // Configuration
    int frame_count;
    int frame_align;
    int frame_valign;
    int packet_count;
    int max_packet_size;
    bool multithread;
    qcap2_event_t* event;
    int num_cores;
    bool native_buffer;
    std::atomic<bool> request_idr;

    // FFmpeg encoder context
    const AVCodec* codec;
    AVCodecContext* codec_ctx;
    SwsContext* sws_ctx; // for input pixel format conversion if needed

    // Running state
    bool running;
    int64_t frame_counter;

    // Allegro VCU encoder state
    void* allegro_enc_handle;    // AL_HEncoder (opaque)
    void* allegro_scheduler;     // AL_IEncScheduler* (opaque)
    void* allegro_allocator;     // AL_TAllocator* (opaque)
    int allegro_channel_id;
    bool allegro_inited;

    // Cached input format for sws reinitialization
    ULONG cached_in_color;
    ULONG cached_in_w;
    ULONG cached_in_h;

    qcap2_video_encoder_priv_t() {
        mtx = new std::mutex();
        cv = new std::condition_variable();
        enc_prop = nullptr;
        dyn_prop = nullptr;
        extra_data = nullptr;
        extra_data_size = 0;
        frame_count = 4;
        frame_align = 16;
        frame_valign = 1;
        packet_count = 8;
        max_packet_size = 0;
        multithread = false;
        event = nullptr;
        num_cores = 0;
        native_buffer = false;
        request_idr.store(false);
        codec = nullptr;
        codec_ctx = nullptr;
        sws_ctx = nullptr;
        running = false;
        frame_counter = 0;
        cached_in_color = 0;
        cached_in_w = 0;
        cached_in_h = 0;
        allegro_enc_handle = nullptr;
        allegro_scheduler = nullptr;
        allegro_allocator = nullptr;
        allegro_channel_id = 0;
        allegro_inited = false;
        input_recycled_queue = qcap2_rcbuffer_queue_new();
        if (input_recycled_queue) {
            qcap2_rcbuffer_queue_start(input_recycled_queue);
        }
        output_recycled_queue = qcap2_rcbuffer_queue_new();
        if (output_recycled_queue) {
            qcap2_rcbuffer_queue_start(output_recycled_queue);
        }
    }

    ~qcap2_video_encoder_priv_t() {
        cleanup();
        if (enc_prop) { qcap2_video_encoder_property_delete(enc_prop); enc_prop = nullptr; }
        if (dyn_prop) { qcap2_video_encoder_dynamic_property_delete(dyn_prop); dyn_prop = nullptr; }
        if (extra_data) { free(extra_data); extra_data = nullptr; }
        if (input_recycled_queue) {
            qcap2_rcbuffer_queue_delete(input_recycled_queue);
        }
        if (output_recycled_queue) {
            qcap2_rcbuffer_queue_delete(output_recycled_queue);
        }
        delete cv;
        delete mtx;
    }

    void cleanup() {
        if (codec_ctx) {
            avcodec_free_context(&codec_ctx);
            codec_ctx = nullptr;
        }
        codec = nullptr;
        if (sws_ctx) {
            sws_freeContext(sws_ctx);
            sws_ctx = nullptr;
        }
        while (!output_queue.empty()) {
            qcap2_rcbuffer_t* buf = output_queue.front();
            output_queue.pop();
            qcap2_rcbuffer_release(buf);
        }
        frame_counter = 0;
        cached_in_color = 0;
        cached_in_w = 0;
        cached_in_h = 0;
    }
} qcap2_video_encoder_priv_t;

typedef struct qcap2_audio_decoder_priv_t {
    std::mutex* mtx;
    std::condition_variable* cv;
    std::queue<qcap2_rcbuffer_t*> output_queue;
    qcap2_rcbuffer_queue_t* input_recycled_queue;
    qcap2_rcbuffer_queue_t* output_recycled_queue;

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

    // Muxer bypass support
    bool bypass_decoding;
    std::queue<qcap2_rcbuffer_t*> input_queue;
    std::mutex* notify_mtx;
    std::condition_variable* notify_cv;

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

        bypass_decoding = false;
        notify_mtx = nullptr;
        notify_cv = nullptr;

        input_recycled_queue = qcap2_rcbuffer_queue_new();
        if (input_recycled_queue) {
            qcap2_rcbuffer_queue_start(input_recycled_queue);
        }
        output_recycled_queue = qcap2_rcbuffer_queue_new();
        if (output_recycled_queue) {
            qcap2_rcbuffer_queue_start(output_recycled_queue);
        }
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
        while (!input_queue.empty()) {
            qcap2_rcbuffer_release(input_queue.front());
            input_queue.pop();
        }
        if (input_recycled_queue) {
            qcap2_rcbuffer_queue_delete(input_recycled_queue);
        }
        if (output_recycled_queue) {
            qcap2_rcbuffer_queue_delete(output_recycled_queue);
        }
        delete cv;
        delete mtx;
    }
} qcap2_audio_decoder_priv_t;

typedef struct qcap2_video_decoder_priv_t {
    std::mutex* mtx;
    std::condition_variable* cv;
    std::queue<qcap2_rcbuffer_t*> output_queue;
    qcap2_rcbuffer_queue_t* input_recycled_queue;
    qcap2_rcbuffer_queue_t* output_recycled_queue;

    // Decoder property (owned copy of encoder properties defining output/stream properties)
    qcap2_video_encoder_property_t* dec_prop;

    // Extra data (SPS/PPS/VPS)
    uint8_t* extra_data;
    int extra_data_size;

    // Configuration / Hints
    int frame_count;
    int frame_align;
    int frame_valign;
    int packet_count;
    int max_packet_size;
    bool multithread;
    qcap2_event_t* event;
    int payload_type;

    // Registered output buffers for recycling
    std::vector<qcap2_rcbuffer_t*> registered_buffers;

    // FFmpeg decoder context
    const AVCodec* codec;
    AVCodecContext* codec_ctx;
    SwsContext* sws_ctx; // for output format conversion/scaling if needed

    // Running state
    bool running;

    // Target properties (resolution/colorspace we want to convert the decoded frame into)
    ULONG target_color;
    ULONG target_width;
    ULONG target_height;

    // Sws conversion state
    ULONG sws_src_color;
    ULONG sws_src_w;
    ULONG sws_src_h;

    // Muxer bypass support
    bool bypass_decoding;

    // Allegro VCU decoder state
    void* allegro_dec_handle;    // AL_HDecoder (opaque)
    void* allegro_scheduler;     // AL_IDecScheduler* (opaque)
    void* allegro_allocator;     // AL_TAllocator* (opaque)
    bool allegro_inited;
    std::queue<qcap2_rcbuffer_t*> input_queue;
    std::mutex* notify_mtx;
    std::condition_variable* notify_cv;

    qcap2_video_decoder_priv_t() {
        mtx = new std::mutex();
        cv = new std::condition_variable();
        dec_prop = nullptr;
        extra_data = nullptr;
        extra_data_size = 0;
        frame_count = 4;
        frame_align = 16;
        frame_valign = 1;
        packet_count = 8;
        max_packet_size = 0;
        multithread = false;
        event = nullptr;
        payload_type = 0;
        codec = nullptr;
        codec_ctx = nullptr;
        sws_ctx = nullptr;
        running = false;

        target_color = QCAP_COLORSPACE_TYPE_I420; // default output colorspace
        target_width = 0;
        target_height = 0;

        sws_src_color = 0;
        sws_src_w = 0;
        sws_src_h = 0;

        bypass_decoding = false;
        notify_mtx = nullptr;
        notify_cv = nullptr;
        allegro_dec_handle = nullptr;
        allegro_scheduler = nullptr;
        allegro_allocator = nullptr;
        allegro_inited = false;

        input_recycled_queue = qcap2_rcbuffer_queue_new();
        if (input_recycled_queue) {
            qcap2_rcbuffer_queue_start(input_recycled_queue);
        }
        output_recycled_queue = qcap2_rcbuffer_queue_new();
        if (output_recycled_queue) {
            qcap2_rcbuffer_queue_start(output_recycled_queue);
        }
    }

    ~qcap2_video_decoder_priv_t() {
        cleanup();
        if (dec_prop) {
            qcap2_video_encoder_property_delete(dec_prop);
            dec_prop = nullptr;
        }
        if (extra_data) {
            free(extra_data);
            extra_data = nullptr;
        }
        for (auto buf : registered_buffers) {
            qcap2_rcbuffer_release(buf);
        }
        registered_buffers.clear();
        while (!input_queue.empty()) {
            qcap2_rcbuffer_release(input_queue.front());
            input_queue.pop();
        }
        if (input_recycled_queue) {
            qcap2_rcbuffer_queue_delete(input_recycled_queue);
        }
        if (output_recycled_queue) {
            qcap2_rcbuffer_queue_delete(output_recycled_queue);
        }
        delete cv;
        delete mtx;
    }

    void cleanup() {
        if (codec_ctx) {
            avcodec_free_context(&codec_ctx);
            codec_ctx = nullptr;
        }
        codec = nullptr;
        if (sws_ctx) {
            sws_freeContext(sws_ctx);
            sws_ctx = nullptr;
        }
        while (!output_queue.empty()) {
            qcap2_rcbuffer_release(output_queue.front());
            output_queue.pop();
        }
        sws_src_color = 0;
        sws_src_w = 0;
        sws_src_h = 0;
    }
} qcap2_video_decoder_priv_t;

// Allegro VCU backend function declarations
// Implemented in qcap2.vcuallegro.cpp (when QCAP2_HAVE_ALLEGRO is defined)
// or as fallback stubs in qcap2.processing.cpp (when QCAP2_HAVE_ALLEGRO is not defined).
// These must have C linkage to match the call sites in qcap2.processing.cpp,
// which are compiled inside the extern "C" block from qcap2.processing.h.
#ifdef __cplusplus
extern "C" {
#endif
QRESULT allegro_encoder_start(qcap2_video_encoder_priv_t* p);
QRESULT allegro_encoder_stop(qcap2_video_encoder_priv_t* p);
QRESULT allegro_encoder_push(qcap2_video_encoder_priv_t* p, qcap2_rcbuffer_t* pRCBuffer);

QRESULT allegro_decoder_start(qcap2_video_decoder_priv_t* p);
QRESULT allegro_decoder_stop(qcap2_video_decoder_priv_t* p);
QRESULT allegro_decoder_push(qcap2_video_decoder_priv_t* p, qcap2_rcbuffer_t* pRCBuffer);
#ifdef __cplusplus
}
#endif

#endif // QCAP2_PROCESSING_PRIV_H
