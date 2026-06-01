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
#include <fstream>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
}

struct qcap2_muxer_user_t {
    std::string account;
    std::string password;
};

struct qcap2_muxer_rtp_ctx_t {
    AVFormatContext* format_context;
    int stream_index;
    bool is_video;
};

struct qcap2_muxer_priv_t {
    int type;
    int max_threads;
    std::string ip;
    int port;
    std::string realm;
    bool ssl;
    std::string cert_file;
    std::string key_file;
    std::vector<qcap2_muxer_user_t> users;
    
    std::vector<qcap2_program_info_t*> programs;
    std::vector<qcap2_video_decoder_t*> video_decoders;
    std::vector<qcap2_audio_decoder_t*> audio_decoders;
    std::vector<qcap2_video_sink_t*> video_sinks;
    std::vector<qcap2_audio_sink_t*> audio_sinks;

    AVFormatContext* format_context;
    std::vector<qcap2_muxer_rtp_ctx_t> rtp_contexts;
    std::unordered_map<qcap2_video_decoder_t*, AVFormatContext*> video_rtp_map;
    std::unordered_map<qcap2_audio_decoder_t*, AVFormatContext*> audio_rtp_map;
    
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

                AVFormatContext* target_ctx = nullptr;
                int target_stream_idx = -1;

                if (priv->type == QCAP2_MUXER_TYPE_SDP) {
                    if (is_video) {
                        qcap2_video_decoder_t* vd = priv->video_decoders[selected_decoder_index];
                        auto it = priv->video_rtp_map.find(vd);
                        if (it != priv->video_rtp_map.end()) {
                            target_ctx = it->second;
                            target_stream_idx = 0;
                        }
                    } else {
                        qcap2_audio_decoder_t* ad = priv->audio_decoders[selected_decoder_index];
                        auto it = priv->audio_rtp_map.find(ad);
                        if (it != priv->audio_rtp_map.end()) {
                            target_ctx = it->second;
                            target_stream_idx = 0;
                        }
                    }
                } else {
                    target_ctx = priv->format_context;
                    if (is_video) {
                        qcap2_video_decoder_t* vd = priv->video_decoders[selected_decoder_index];
                        auto it = priv->video_stream_map.find(vd);
                        if (it != priv->video_stream_map.end()) {
                            target_stream_idx = it->second;
                        }
                    } else {
                        qcap2_audio_decoder_t* ad = priv->audio_decoders[selected_decoder_index];
                        auto it = priv->audio_stream_map.find(ad);
                        if (it != priv->audio_stream_map.end()) {
                            target_stream_idx = it->second;
                        }
                    }
                }

                if (target_ctx && target_stream_idx >= 0) {
                    AVStream* stream = target_ctx->streams[target_stream_idx];
                    AVPacket* av_pkt = av_packet_alloc();
                    if (av_pkt) {
                        av_pkt->data = pBuf;
                        av_pkt->size = nSize;
                        av_pkt->stream_index = target_stream_idx;

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

                        av_interleaved_write_frame(target_ctx, av_pkt);
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
    if (pThis) {
        qcap2_muxer_priv_t* priv = reinterpret_cast<qcap2_muxer_priv_t*>(pThis);
        priv->users.push_back({strAccount ? strAccount : "", strPassword ? strPassword : ""});
    }
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

    if (priv->format_context || !priv->rtp_contexts.empty()) {
        return QCAP_RS_SUCCESSFUL;
    }

    std::string url = priv->ip;
    if (priv->type == QCAP2_MUXER_TYPE_RTSP) {
        if (url.find("rtsp://") != 0 && url.find("rtsps://") != 0) {
            std::string proto = priv->ssl ? "rtsps://" : "rtsp://";
            std::string creds = "";
            if (!priv->users.empty()) {
                creds = priv->users[0].account + ":" + priv->users[0].password + "@";
            }
            std::string host = priv->ip.empty() ? "127.0.0.1" : priv->ip;
            std::string port_str = (priv->port > 0) ? ":" + std::to_string(priv->port) : "";
            std::string path = "/live/stream";
            if (host.find('/') != std::string::npos) {
                url = proto + creds + host;
            } else {
                url = proto + creds + host + port_str + path;
            }
        }
    }

    priv->video_stream_map.clear();
    priv->audio_stream_map.clear();
    priv->video_rtp_map.clear();
    priv->audio_rtp_map.clear();
    priv->rtp_contexts.clear();

    std::string sdp_filepath = priv->ip.empty() ? "stream.sdp" : priv->ip;
    int base_port = priv->port > 0 ? priv->port : 5004;
    int current_port = base_port;

    if (priv->type != QCAP2_MUXER_TYPE_SDP) {
        int ret;
        if (priv->type == QCAP2_MUXER_TYPE_RTSP) {
            ret = avformat_alloc_output_context2(&priv->format_context, nullptr, "rtsp", url.c_str());
        } else {
            ret = avformat_alloc_output_context2(&priv->format_context, nullptr, nullptr, priv->ip.c_str());
        }
        if (ret < 0 || !priv->format_context) {
            return QCAP_RS_ERROR_GENERAL;
        }
    }

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
                    
                    AVFormatContext* target_ctx = nullptr;
                    if (priv->type == QCAP2_MUXER_TYPE_SDP) {
                        std::string rtp_url = "rtp://127.0.0.1:" + std::to_string(current_port);
                        int r = avformat_alloc_output_context2(&target_ctx, nullptr, "rtp", rtp_url.c_str());
                        if (r < 0 || !target_ctx) {
                            return QCAP_RS_ERROR_GENERAL;
                        }
                        priv->rtp_contexts.push_back({target_ctx, idx, true});
                        priv->video_rtp_map[vd] = target_ctx;
                        current_port += 2;
                    } else {
                        target_ctx = priv->format_context;
                    }

                    AVStream* stream = avformat_new_stream(target_ctx, nullptr);
                    if (!stream) {
                        return QCAP_RS_ERROR_OUT_OF_MEMORY;
                    }

                    if (priv->type != QCAP2_MUXER_TYPE_SDP) {
                        priv->video_stream_map[vd] = stream->index;
                    }

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

                    AVFormatContext* target_ctx = nullptr;
                    if (priv->type == QCAP2_MUXER_TYPE_SDP) {
                        std::string rtp_url = "rtp://127.0.0.1:" + std::to_string(current_port);
                        int r = avformat_alloc_output_context2(&target_ctx, nullptr, "rtp", rtp_url.c_str());
                        if (r < 0 || !target_ctx) {
                            return QCAP_RS_ERROR_GENERAL;
                        }
                        priv->rtp_contexts.push_back({target_ctx, idx, false});
                        priv->audio_rtp_map[ad] = target_ctx;
                        current_port += 2;
                    } else {
                        target_ctx = priv->format_context;
                    }

                    AVStream* stream = avformat_new_stream(target_ctx, nullptr);
                    if (!stream) {
                        return QCAP_RS_ERROR_OUT_OF_MEMORY;
                    }

                    if (priv->type != QCAP2_MUXER_TYPE_SDP) {
                        priv->audio_stream_map[ad] = stream->index;
                    }

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

    if (priv->type == QCAP2_MUXER_TYPE_SDP) {
        for (auto& rtp : priv->rtp_contexts) {
            int ret_open = avio_open(&rtp.format_context->pb, rtp.format_context->url, AVIO_FLAG_WRITE);
            if (ret_open < 0) {
                for (auto& rtp_cleanup : priv->rtp_contexts) {
                    if (rtp_cleanup.format_context->pb) avio_closep(&rtp_cleanup.format_context->pb);
                    avformat_free_context(rtp_cleanup.format_context);
                }
                priv->rtp_contexts.clear();
                priv->video_rtp_map.clear();
                priv->audio_rtp_map.clear();
                return QCAP_RS_ERROR_FILE_ACCESS_FAIL;
            }

            int ret_hdr = avformat_write_header(rtp.format_context, nullptr);
            if (ret_hdr < 0) {
                for (auto& rtp_cleanup : priv->rtp_contexts) {
                    if (rtp_cleanup.format_context->pb) avio_closep(&rtp_cleanup.format_context->pb);
                    avformat_free_context(rtp_cleanup.format_context);
                }
                priv->rtp_contexts.clear();
                priv->video_rtp_map.clear();
                priv->audio_rtp_map.clear();
                return QCAP_RS_ERROR_GENERAL;
            }
        }

        std::vector<AVFormatContext*> ctx_array;
        for (auto& rtp : priv->rtp_contexts) {
            ctx_array.push_back(rtp.format_context);
        }

        char sdp_buf[8192];
        memset(sdp_buf, 0, sizeof(sdp_buf));
        int ret_sdp = av_sdp_create(ctx_array.data(), ctx_array.size(), sdp_buf, sizeof(sdp_buf));
        if (ret_sdp >= 0) {
            std::ofstream sdp_file(sdp_filepath);
            if (sdp_file.is_open()) {
                sdp_file << sdp_buf;
                sdp_file.close();
            } else {
                for (auto& rtp_cleanup : priv->rtp_contexts) {
                    if (rtp_cleanup.format_context->pb) avio_closep(&rtp_cleanup.format_context->pb);
                    avformat_free_context(rtp_cleanup.format_context);
                }
                priv->rtp_contexts.clear();
                priv->video_rtp_map.clear();
                priv->audio_rtp_map.clear();
                return QCAP_RS_ERROR_FILE_ACCESS_FAIL;
            }
        } else {
            for (auto& rtp_cleanup : priv->rtp_contexts) {
                if (rtp_cleanup.format_context->pb) avio_closep(&rtp_cleanup.format_context->pb);
                avformat_free_context(rtp_cleanup.format_context);
            }
            priv->rtp_contexts.clear();
            priv->video_rtp_map.clear();
            priv->audio_rtp_map.clear();
            return QCAP_RS_ERROR_GENERAL;
        }

    } else {
        int ret;
        if (!(priv->format_context->oformat->flags & AVFMT_NOFILE)) {
            ret = avio_open(&priv->format_context->pb, url.c_str(), AVIO_FLAG_WRITE);
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
            return (priv->type == QCAP2_MUXER_TYPE_RTSP) ? QCAP_RS_ERROR_NETWORK_ACCESS_FAIL : QCAP_RS_ERROR_GENERAL;
        }
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

    if (priv->type == QCAP2_MUXER_TYPE_SDP) {
        if (priv->rtp_contexts.empty()) {
            return QCAP_RS_SUCCESSFUL;
        }
    } else {
        if (!priv->format_context) {
            return QCAP_RS_SUCCESSFUL;
        }
    }

    priv->thread_running = false;
    priv->cv.notify_all();
    if (priv->write_thread.joinable()) {
        priv->write_thread.join();
    }

    if (priv->type == QCAP2_MUXER_TYPE_SDP) {
        for (auto& rtp : priv->rtp_contexts) {
            av_write_trailer(rtp.format_context);
            if (rtp.format_context->pb) avio_closep(&rtp.format_context->pb);
            avformat_free_context(rtp.format_context);
        }
        priv->rtp_contexts.clear();
        priv->video_rtp_map.clear();
        priv->audio_rtp_map.clear();
    } else {
        av_write_trailer(priv->format_context);

        if (!(priv->format_context->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&priv->format_context->pb);
        }

        avformat_free_context(priv->format_context);
        priv->format_context = nullptr;
    }

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
