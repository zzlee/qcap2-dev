#include "qcap2.devices_priv.h"
#include "qcap2.user.h"
#include "qcap2.buffer.h"
#include "qcap2.processing_priv.h"
#include "qcap.ext.core.h"
#include <vector>
#include <thread>
#include <atomic>
#include <string>
#include <new>
#include <cstdlib>
#include <unordered_map>
#include <chrono>
#include <mutex>
#include <condition_variable>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
}

// Dummy/Placeholder for sinks since they are only declared but not used in our test cases
typedef struct qcap2_video_sink_t qcap2_video_sink_t;
typedef struct qcap2_audio_sink_t qcap2_audio_sink_t;

struct qcap2_muxer_priv_t {
    int type;
    int max_threads;
    std::string ip;
    int port;
    std::string realm;
    bool ssl;
    std::string cert_file;
    std::string key_file;
    
    std::vector<qcap2_program_info_t*> programs;
    std::vector<qcap2_video_decoder_t*> video_decoders;
    std::vector<qcap2_audio_decoder_t*> audio_decoders;
    std::vector<qcap2_video_sink_t*> video_sinks;
    std::vector<qcap2_audio_sink_t*> audio_sinks;

    AVFormatContext* format_context;
    std::thread write_thread;
    std::atomic<bool> thread_running;

    std::mutex mtx;
    std::condition_variable cv;

    // Maps to correlate the decoders to their created FFmpeg stream index
    std::unordered_map<qcap2_video_decoder_t*, int> video_stream_map;
    std::unordered_map<qcap2_audio_decoder_t*, int> audio_stream_map;

    qcap2_muxer_priv_t()
        : type(QCAP2_MUXER_TYPE_DEFAULT),
          max_threads(0),
          port(0),
          ssl(false),
          format_context(nullptr),
          thread_running(false) {}

    ~qcap2_muxer_priv_t() {
        cleanup();
    }

    void cleanup() {
        for (auto prog : programs) {
            if (prog) qcap2_program_info_delete(prog);
        }
        programs.clear();
        for (auto vd : video_decoders) {
            if (vd) qcap2_video_decoder_delete(vd);
        }
        video_decoders.clear();
        for (auto ad : audio_decoders) {
            if (ad) qcap2_audio_decoder_delete(ad);
        }
        audio_decoders.clear();
    }
};

static void muxer_write_thread(qcap2_muxer_priv_t* priv) {
    while (priv->thread_running || true) {
        qcap2_rcbuffer_t* selected_buf = nullptr;
        int selected_decoder_index = -1;
        int64_t min_dts = INT64_MAX;
        bool is_video = false;

        // Select the packet with the lowest DTS across all decoder input queues
        for (auto prog : priv->programs) {
            // Video streams
            int v_count = qcap2_program_info_get_video_decoder_count(prog);
            for (int i = 0; i < v_count; ++i) {
                int idx = qcap2_program_info_get_video_decoder_index(prog, i);
                if (idx >= 0 && idx < (int)priv->video_decoders.size()) {
                    qcap2_video_decoder_t* vd = priv->video_decoders[idx];
                    if (vd) {
                        qcap2_video_decoder_priv_t* vd_priv = (qcap2_video_decoder_priv_t*)vd;
                        std::lock_guard<std::mutex> lock(*(vd_priv->mtx));
                        if (!vd_priv->input_queue.empty()) {
                            qcap2_rcbuffer_t* buf = vd_priv->input_queue.front();
                            PVOID pData = qcap2_rcbuffer_lock_data(buf);
                            if (pData) {
                                qcap2_av_packet_t* pkt = (qcap2_av_packet_t*)pData;
                                int64_t dts = 0;
                                qcap2_av_packet_get_dts(pkt, &dts);
                                if (dts < min_dts) {
                                    min_dts = dts;
                                    selected_buf = buf;
                                    selected_decoder_index = idx;
                                    is_video = true;
                                }
                                qcap2_rcbuffer_unlock_data(buf);
                            }
                        }
                    }
                }
            }

            // Audio streams
            int a_count = qcap2_program_info_get_audio_decoder_count(prog);
            for (int i = 0; i < a_count; ++i) {
                int idx = qcap2_program_info_get_audio_decoder_index(prog, i);
                if (idx >= 0 && idx < (int)priv->audio_decoders.size()) {
                    qcap2_audio_decoder_t* ad = priv->audio_decoders[idx];
                    if (ad) {
                        qcap2_audio_decoder_priv_t* ad_priv = (qcap2_audio_decoder_priv_t*)ad;
                        std::lock_guard<std::mutex> lock(*(ad_priv->mtx));
                        if (!ad_priv->input_queue.empty()) {
                            qcap2_rcbuffer_t* buf = ad_priv->input_queue.front();
                            PVOID pData = qcap2_rcbuffer_lock_data(buf);
                            if (pData) {
                                qcap2_av_packet_t* pkt = (qcap2_av_packet_t*)pData;
                                int64_t dts = 0;
                                qcap2_av_packet_get_dts(pkt, &dts);
                                if (dts < min_dts) {
                                    min_dts = dts;
                                    selected_buf = buf;
                                    selected_decoder_index = idx;
                                    is_video = false;
                                }
                                qcap2_rcbuffer_unlock_data(buf);
                            }
                        }
                    }
                }
            }
        }

        if (selected_buf) {
            // Pop the packet from the selected queue
            if (is_video) {
                qcap2_video_decoder_t* vd = priv->video_decoders[selected_decoder_index];
                qcap2_video_decoder_priv_t* vd_priv = (qcap2_video_decoder_priv_t*)vd;
                std::lock_guard<std::mutex> lock(*(vd_priv->mtx));
                vd_priv->input_queue.pop();
            } else {
                qcap2_audio_decoder_t* ad = priv->audio_decoders[selected_decoder_index];
                qcap2_audio_decoder_priv_t* ad_priv = (qcap2_audio_decoder_priv_t*)ad;
                std::lock_guard<std::mutex> lock(*(ad_priv->mtx));
                ad_priv->input_queue.pop();
            }

            // Write the packet
            PVOID pData = qcap2_rcbuffer_lock_data(selected_buf);
            if (pData) {
                qcap2_av_packet_t* pkt = (qcap2_av_packet_t*)pData;
                uint8_t* pBuf = nullptr;
                int nSize = 0;
                qcap2_av_packet_get_buffer(pkt, &pBuf, &nSize);

                double sample_time = 0.0;
                qcap2_av_packet_get_sample_time(pkt, &sample_time);

                int64_t input_pts = 0, input_dts = 0;
                qcap2_av_packet_get_pts(pkt, &input_pts);
                qcap2_av_packet_get_dts(pkt, &input_dts);

                int ffmpeg_stream_idx = -1;
                if (is_video) {
                    qcap2_video_decoder_t* vd = priv->video_decoders[selected_decoder_index];
                    auto it = priv->video_stream_map.find(vd);
                    if (it != priv->video_stream_map.end()) {
                        ffmpeg_stream_idx = it->second;
                    }
                } else {
                    qcap2_audio_decoder_t* ad = priv->audio_decoders[selected_decoder_index];
                    auto it = priv->audio_stream_map.find(ad);
                    if (it != priv->audio_stream_map.end()) {
                        ffmpeg_stream_idx = it->second;
                    }
                }

                if (ffmpeg_stream_idx >= 0) {
                    AVStream* stream = priv->format_context->streams[ffmpeg_stream_idx];
                    AVPacket* av_pkt = av_packet_alloc();
                    if (av_pkt) {
                        av_pkt->data = pBuf;
                        av_pkt->size = nSize;
                        av_pkt->stream_index = ffmpeg_stream_idx;

                        // Calculate correct timestamp ratios relative to output container stream timebase
                        int64_t diff = input_pts - input_dts;
                        double dts_time = sample_time - (double)diff / 1000000.0;

                        av_pkt->pts = (int64_t)(sample_time / av_q2d(stream->time_base) + 0.5);
                        av_pkt->dts = (int64_t)(dts_time / av_q2d(stream->time_base) + 0.5);
                        av_pkt->duration = (int64_t)(0.04 / av_q2d(stream->time_base) + 0.5);

                        int stream_idx_prop = 0;
                        BOOL is_key = FALSE;
                        qcap2_av_packet_get_property(pkt, &stream_idx_prop, &is_key);
                        if (is_key) {
                            av_pkt->flags |= AV_PKT_FLAG_KEY;
                        }

                        av_interleaved_write_frame(priv->format_context, av_pkt);
                        av_packet_free(&av_pkt);
                    }
                }
                qcap2_rcbuffer_unlock_data(selected_buf);
            }
            qcap2_rcbuffer_release(selected_buf);
        } else {
            // Check if thread should terminate
            if (!priv->thread_running) {
                bool all_empty = true;
                for (auto prog : priv->programs) {
                    int v_count = qcap2_program_info_get_video_decoder_count(prog);
                    for (int i = 0; i < v_count; ++i) {
                        int idx = qcap2_program_info_get_video_decoder_index(prog, i);
                        if (idx >= 0 && idx < (int)priv->video_decoders.size() && priv->video_decoders[idx]) {
                            qcap2_video_decoder_priv_t* vd_priv = (qcap2_video_decoder_priv_t*)priv->video_decoders[idx];
                            std::lock_guard<std::mutex> lock(*(vd_priv->mtx));
                            if (!vd_priv->input_queue.empty()) all_empty = false;
                        }
                    }
                    int a_count = qcap2_program_info_get_audio_decoder_count(prog);
                    for (int i = 0; i < a_count; ++i) {
                        int idx = qcap2_program_info_get_audio_decoder_index(prog, i);
                        if (idx >= 0 && idx < (int)priv->audio_decoders.size() && priv->audio_decoders[idx]) {
                            qcap2_audio_decoder_priv_t* ad_priv = (qcap2_audio_decoder_priv_t*)priv->audio_decoders[idx];
                            std::lock_guard<std::mutex> lock(*(ad_priv->mtx));
                            if (!ad_priv->input_queue.empty()) all_empty = false;
                        }
                    }
                }
                if (all_empty) {
                    break;
                }
            }

            // Wait on the combined notify condition variable
            std::unique_lock<std::mutex> lock(priv->mtx);
            priv->cv.wait_for(lock, std::chrono::milliseconds(10), [priv] {
                return !priv->thread_running;
            });
        }
    }
}

extern "C" {

qcap2_muxer_t* qcap2_muxer_new() {
    qcap2_muxer_priv_t* priv = new (std::nothrow) qcap2_muxer_priv_t();
    return reinterpret_cast<qcap2_muxer_t*>(priv);
}

void qcap2_muxer_delete(qcap2_muxer_t* pThis) {
    if (pThis) {
        qcap2_muxer_priv_t* priv = reinterpret_cast<qcap2_muxer_priv_t*>(pThis);
        qcap2_muxer_stop(pThis);
        delete priv;
    }
}

void qcap2_muxer_set_type(qcap2_muxer_t* pThis, int nType) {
    if (pThis) {
        qcap2_muxer_priv_t* priv = reinterpret_cast<qcap2_muxer_priv_t*>(pThis);
        priv->type = nType;
    }
}

void qcap2_muxer_set_max_threads(qcap2_muxer_t* pThis, int nMaxThreads) {
    if (pThis) {
        qcap2_muxer_priv_t* priv = reinterpret_cast<qcap2_muxer_priv_t*>(pThis);
        priv->max_threads = nMaxThreads;
    }
}

void qcap2_muxer_set_endpoint(qcap2_muxer_t* pThis, const char* strIP, int nPort) {
    if (pThis) {
        qcap2_muxer_priv_t* priv = reinterpret_cast<qcap2_muxer_priv_t*>(pThis);
        priv->ip = strIP ? strIP : "";
        priv->port = nPort;
    }
}

void qcap2_muxer_set_realm(qcap2_muxer_t* pThis, const char* strRealm) {
    if (pThis) {
        qcap2_muxer_priv_t* priv = reinterpret_cast<qcap2_muxer_priv_t*>(pThis);
        priv->realm = strRealm ? strRealm : "";
    }
}

void qcap2_muxer_set_ssl(qcap2_muxer_t* pThis, bool bSSL) {
    if (pThis) {
        qcap2_muxer_priv_t* priv = reinterpret_cast<qcap2_muxer_priv_t*>(pThis);
        priv->ssl = bSSL;
    }
}

void qcap2_muxer_set_certificate_chain_file(qcap2_muxer_t* pThis, const char* strCertificateChainFile) {
    if (pThis) {
        qcap2_muxer_priv_t* priv = reinterpret_cast<qcap2_muxer_priv_t*>(pThis);
        priv->cert_file = strCertificateChainFile ? strCertificateChainFile : "";
    }
}

void qcap2_muxer_set_private_key_file(qcap2_muxer_t* pThis, const char* strPrivateKeyFile) {
    if (pThis) {
        qcap2_muxer_priv_t* priv = reinterpret_cast<qcap2_muxer_priv_t*>(pThis);
        priv->key_file = strPrivateKeyFile ? strPrivateKeyFile : "";
    }
}

void qcap2_muxer_add_user(qcap2_muxer_t* pThis, const char* strAccount, const char* strPassword) {
    (void)pThis;
    (void)strAccount;
    (void)strPassword;
}

void qcap2_muxer_add_program_info(qcap2_muxer_t* pThis, qcap2_program_info_t* pProgramInfo) {
    if (pThis && pProgramInfo) {
        qcap2_muxer_priv_t* priv = reinterpret_cast<qcap2_muxer_priv_t*>(pThis);
        priv->programs.push_back(pProgramInfo);
    }
}

QRESULT qcap2_muxer_start(qcap2_muxer_t* pThis) {
    if (!pThis) return QCAP_RS_ERROR_GENERAL;
    qcap2_muxer_priv_t* priv = reinterpret_cast<qcap2_muxer_priv_t*>(pThis);

    if (priv->format_context) {
        return QCAP_RS_SUCCESSFUL;
    }

    int ret = avformat_alloc_output_context2(&priv->format_context, nullptr, nullptr, priv->ip.c_str());
    if (ret < 0 || !priv->format_context) {
        return QCAP_RS_ERROR_GENERAL;
    }

    priv->video_stream_map.clear();
    priv->audio_stream_map.clear();

    // Set up target streams
    for (auto prog : priv->programs) {
        // Video Streams setup
        int v_count = qcap2_program_info_get_video_decoder_count(prog);
        for (int i = 0; i < v_count; ++i) {
            int idx = qcap2_program_info_get_video_decoder_index(prog, i);
            if (idx >= 0 && idx < (int)priv->video_decoders.size()) {
                qcap2_video_decoder_t* vd = priv->video_decoders[idx];
                if (vd) {
                    qcap2_video_decoder_priv_t* vd_priv = (qcap2_video_decoder_priv_t*)vd;
                    
                    AVStream* stream = avformat_new_stream(priv->format_context, nullptr);
                    if (!stream) {
                        return QCAP_RS_ERROR_OUT_OF_MEMORY;
                    }

                    priv->video_stream_map[vd] = stream->index;

                    ULONG nEncoderType = 0, nEncoderFormat = 0, nColorSpaceType = 0, nWidth = 0, nHeight = 0;
                    double dFrameRate = 0.0;
                    ULONG nRecordMode = 0, nQuality = 0, nBitRate = 0, nGOP = 0, nAspectRatioX = 0, nAspectRatioY = 0;
                    
                    qcap2_video_encoder_property_t* prop = qcap2_video_encoder_property_new();
                    qcap2_video_decoder_get_video_property(vd, prop);
                    qcap2_video_encoder_property_get_property(prop, &nEncoderType, &nEncoderFormat, &nColorSpaceType, &nWidth, &nHeight, &dFrameRate, &nRecordMode, &nQuality, &nBitRate, &nGOP, &nAspectRatioX, &nAspectRatioY);
                    qcap2_video_encoder_property_delete(prop);

                    AVCodecID codec_id = AV_CODEC_ID_NONE;
                    if (nEncoderFormat == QCAP_ENCODER_FORMAT_H264) codec_id = AV_CODEC_ID_H264;
                    else if (nEncoderFormat == QCAP_ENCODER_FORMAT_H265) codec_id = AV_CODEC_ID_HEVC;
                    else if (nEncoderFormat == QCAP_ENCODER_FORMAT_MPEG2) codec_id = AV_CODEC_ID_MPEG2VIDEO;
                    else if (nEncoderFormat == QCAP_ENCODER_FORMAT_MJPEG) codec_id = AV_CODEC_ID_MJPEG;

                    stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
                    stream->codecpar->codec_id = codec_id;
                    stream->codecpar->width = nWidth;
                    stream->codecpar->height = nHeight;
                    stream->codecpar->bit_rate = nBitRate;

                    if (dFrameRate > 0.0) {
                        stream->time_base = av_d2q(1.0 / dFrameRate, 1000000);
                        stream->r_frame_rate = av_d2q(dFrameRate, 1000000);
                    } else {
                        stream->time_base = { 1, 90000 };
                    }

                    // Propagate extradata
                    uint8_t* extra = nullptr;
                    int extra_sz = 0;
                    qcap2_video_decoder_get_extra_data(vd, &extra, &extra_sz);
                    if (extra && extra_sz > 0) {
                        stream->codecpar->extradata = (uint8_t*)av_mallocz(extra_sz + AV_INPUT_BUFFER_PADDING_SIZE);
                        if (stream->codecpar->extradata) {
                            memcpy(stream->codecpar->extradata, extra, extra_sz);
                            stream->codecpar->extradata_size = extra_sz;
                        }
                    }

                    vd_priv->bypass_decoding = true;
                    vd_priv->notify_mtx = &priv->mtx;
                    vd_priv->notify_cv = &priv->cv;
                }
            }
        }

        // Audio Streams setup
        int a_count = qcap2_program_info_get_audio_decoder_count(prog);
        for (int i = 0; i < a_count; ++i) {
            int idx = qcap2_program_info_get_audio_decoder_index(prog, i);
            if (idx >= 0 && idx < (int)priv->audio_decoders.size()) {
                qcap2_audio_decoder_t* ad = priv->audio_decoders[idx];
                if (ad) {
                    qcap2_audio_decoder_priv_t* ad_priv = (qcap2_audio_decoder_priv_t*)ad;

                    AVStream* stream = avformat_new_stream(priv->format_context, nullptr);
                    if (!stream) {
                        return QCAP_RS_ERROR_OUT_OF_MEMORY;
                    }

                    priv->audio_stream_map[ad] = stream->index;

                    ULONG nEncoderType = 0, nEncoderFormat = 0, nChannels = 0, nBitsPerSample = 0, nSampleFrequency = 0, nBitRate = 0;
                    
                    qcap2_audio_encoder_property_t* prop = qcap2_audio_encoder_property_new();
                    qcap2_audio_decoder_get_audio_property(ad, prop);
                    qcap2_audio_encoder_property_get_property1(prop, &nEncoderType, &nEncoderFormat, &nChannels, &nBitsPerSample, &nSampleFrequency, &nBitRate);
                    qcap2_audio_encoder_property_delete(prop);

                    AVCodecID codec_id = AV_CODEC_ID_NONE;
                    if (nEncoderFormat == QCAP_ENCODER_FORMAT_AAC) codec_id = AV_CODEC_ID_AAC;
                    else if (nEncoderFormat == QCAP_ENCODER_FORMAT_MP3) codec_id = AV_CODEC_ID_MP3;
                    else if (nEncoderFormat == QCAP_ENCODER_FORMAT_MP2) codec_id = AV_CODEC_ID_MP2;
                    else if (nEncoderFormat == QCAP_ENCODER_FORMAT_OPUS) codec_id = AV_CODEC_ID_OPUS;
                    else if (nEncoderFormat == QCAP_ENCODER_FORMAT_AC3) codec_id = AV_CODEC_ID_AC3;
                    else if (nEncoderFormat == QCAP_ENCODER_FORMAT_G711_ALAW) codec_id = AV_CODEC_ID_PCM_ALAW;
                    else if (nEncoderFormat == QCAP_ENCODER_FORMAT_G711_ULAW) codec_id = AV_CODEC_ID_PCM_MULAW;
                    else if (nEncoderFormat == QCAP_ENCODER_FORMAT_PCM) codec_id = AV_CODEC_ID_PCM_S16LE;

                    stream->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
                    stream->codecpar->codec_id = codec_id;
                    stream->codecpar->channels = nChannels;
                    stream->codecpar->sample_rate = nSampleFrequency;
                    stream->codecpar->bit_rate = nBitRate;
                    stream->time_base = { 1, (int)nSampleFrequency };

                    // Propagate extradata
                    uint8_t* extra = nullptr;
                    int extra_sz = 0;
                    qcap2_audio_decoder_get_extra_data(ad, &extra, &extra_sz);
                    if (extra && extra_sz > 0) {
                        stream->codecpar->extradata = (uint8_t*)av_mallocz(extra_sz + AV_INPUT_BUFFER_PADDING_SIZE);
                        if (stream->codecpar->extradata) {
                            memcpy(stream->codecpar->extradata, extra, extra_sz);
                            stream->codecpar->extradata_size = extra_sz;
                        }
                    }

                    ad_priv->bypass_decoding = true;
                    ad_priv->notify_mtx = &priv->mtx;
                    ad_priv->notify_cv = &priv->cv;
                }
            }
        }
    }

    if (!(priv->format_context->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&priv->format_context->pb, priv->ip.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            avformat_free_context(priv->format_context);
            priv->format_context = nullptr;
            return QCAP_RS_ERROR_FILE_ACCESS_FAIL;
        }
    }

    ret = avformat_write_header(priv->format_context, nullptr);
    if (ret < 0) {
        if (!(priv->format_context->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&priv->format_context->pb);
        }
        avformat_free_context(priv->format_context);
        priv->format_context = nullptr;
        return QCAP_RS_ERROR_GENERAL;
    }

    // Start all decoders in bypass mode
    for (auto prog : priv->programs) {
        int v_count = qcap2_program_info_get_video_decoder_count(prog);
        for (int i = 0; i < v_count; ++i) {
            int idx = qcap2_program_info_get_video_decoder_index(prog, i);
            if (idx >= 0 && idx < (int)priv->video_decoders.size() && priv->video_decoders[idx]) {
                qcap2_video_decoder_start(priv->video_decoders[idx]);
            }
        }
        int a_count = qcap2_program_info_get_audio_decoder_count(prog);
        for (int i = 0; i < a_count; ++i) {
            int idx = qcap2_program_info_get_audio_decoder_index(prog, i);
            if (idx >= 0 && idx < (int)priv->audio_decoders.size() && priv->audio_decoders[idx]) {
                qcap2_audio_decoder_start(priv->audio_decoders[idx]);
            }
        }
    }

    priv->thread_running = true;
    priv->write_thread = std::thread(muxer_write_thread, priv);

    return QCAP_RS_SUCCESSFUL;
}

QRESULT qcap2_muxer_stop(qcap2_muxer_t* pThis) {
    if (!pThis) return QCAP_RS_ERROR_GENERAL;
    qcap2_muxer_priv_t* priv = reinterpret_cast<qcap2_muxer_priv_t*>(pThis);

    if (!priv->format_context) {
        return QCAP_RS_SUCCESSFUL;
    }

    priv->thread_running = false;
    priv->cv.notify_all();
    if (priv->write_thread.joinable()) {
        priv->write_thread.join();
    }

    av_write_trailer(priv->format_context);

    if (!(priv->format_context->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&priv->format_context->pb);
    }

    avformat_free_context(priv->format_context);
    priv->format_context = nullptr;

    // Stop decoders
    for (auto prog : priv->programs) {
        int v_count = qcap2_program_info_get_video_decoder_count(prog);
        for (int i = 0; i < v_count; ++i) {
            int idx = qcap2_program_info_get_video_decoder_index(prog, i);
            if (idx >= 0 && idx < (int)priv->video_decoders.size() && priv->video_decoders[idx]) {
                qcap2_video_decoder_stop(priv->video_decoders[idx]);
            }
        }
        int a_count = qcap2_program_info_get_audio_decoder_count(prog);
        for (int i = 0; i < a_count; ++i) {
            int idx = qcap2_program_info_get_audio_decoder_index(prog, i);
            if (idx >= 0 && idx < (int)priv->audio_decoders.size() && priv->audio_decoders[idx]) {
                qcap2_audio_decoder_stop(priv->audio_decoders[idx]);
            }
        }
    }

    return QCAP_RS_SUCCESSFUL;
}

int qcap2_muxer_get_video_sink_count(qcap2_muxer_t* pThis) {
    (void)pThis;
    return 0;
}

int qcap2_muxer_get_audio_sink_count(qcap2_muxer_t* pThis) {
    (void)pThis;
    return 0;
}

int qcap2_muxer_get_video_decoder_count(qcap2_muxer_t* pThis) {
    if (!pThis) return 0;
    qcap2_muxer_priv_t* priv = reinterpret_cast<qcap2_muxer_priv_t*>(pThis);
    return priv->video_decoders.size();
}

int qcap2_muxer_get_audio_decoder_count(qcap2_muxer_t* pThis) {
    if (!pThis) return 0;
    qcap2_muxer_priv_t* priv = reinterpret_cast<qcap2_muxer_priv_t*>(pThis);
    return priv->audio_decoders.size();
}

int qcap2_muxer_get_program_count(qcap2_muxer_t* pThis) {
    if (!pThis) return 0;
    qcap2_muxer_priv_t* priv = reinterpret_cast<qcap2_muxer_priv_t*>(pThis);
    return priv->programs.size();
}

qcap2_program_info_t* qcap2_muxer_get_program_info(qcap2_muxer_t* pThis, int nIndex) {
    if (!pThis || nIndex < 0) return nullptr;
    qcap2_muxer_priv_t* priv = reinterpret_cast<qcap2_muxer_priv_t*>(pThis);
    if (nIndex >= (int)priv->programs.size()) return nullptr;
    return priv->programs[nIndex];
}

qcap2_video_sink_t* qcap2_muxer_get_video_sink(qcap2_muxer_t* pThis, int nIndex) {
    (void)pThis;
    (void)nIndex;
    return nullptr;
}

qcap2_audio_sink_t* qcap2_muxer_get_audio_sink(qcap2_muxer_t* pThis, int nIndex) {
    (void)pThis;
    (void)nIndex;
    return nullptr;
}

qcap2_video_decoder_t* qcap2_muxer_get_video_decoder(qcap2_muxer_t* pThis, int nIndex) {
    if (!pThis || nIndex < 0) return nullptr;
    qcap2_muxer_priv_t* priv = reinterpret_cast<qcap2_muxer_priv_t*>(pThis);
    if (nIndex >= (int)priv->video_decoders.size()) {
        priv->video_decoders.resize(nIndex + 1, nullptr);
    }
    if (!priv->video_decoders[nIndex]) {
        priv->video_decoders[nIndex] = qcap2_video_decoder_new();
    }
    return priv->video_decoders[nIndex];
}

qcap2_audio_decoder_t* qcap2_muxer_get_audio_decoder(qcap2_muxer_t* pThis, int nIndex) {
    if (!pThis || nIndex < 0) return nullptr;
    qcap2_muxer_priv_t* priv = reinterpret_cast<qcap2_muxer_priv_t*>(pThis);
    if (nIndex >= (int)priv->audio_decoders.size()) {
        priv->audio_decoders.resize(nIndex + 1, nullptr);
    }
    if (!priv->audio_decoders[nIndex]) {
        priv->audio_decoders[nIndex] = qcap2_audio_decoder_new();
    }
    return priv->audio_decoders[nIndex];
}

QRESULT qcap2_muxer_play(qcap2_muxer_t* pThis) {
    (void)pThis;
    return QCAP_RS_SUCCESSFUL;
}

} // extern "C"
