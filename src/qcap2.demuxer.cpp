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
}

struct qcap2_demuxer_priv_t {
    int type;
    qcap2_event_t* event;
    std::string url;
    int max_buffer_length;
    bool find_stream_info;
    bool push_source;
    bool live_source;
    std::string format_name;
    int buffer_size;
    std::string sdp_lines;
    bool tcp;
    bool multicast;

    AVFormatContext* format_context;

    std::vector<qcap2_video_source_t*> video_sources;
    std::vector<qcap2_audio_source_t*> audio_sources;
    std::vector<qcap2_video_encoder_t*> video_encoders;
    std::vector<qcap2_audio_encoder_t*> audio_encoders;
    std::vector<qcap2_program_info_t*> programs;

    std::thread reader_thread;
    std::atomic<bool> thread_running;
    std::mutex pacing_mtx;
    std::condition_variable pacing_cv;

    qcap2_demuxer_priv_t()
        : type(QCAP2_DEMUXER_TYPE_DEFAULT),
          event(nullptr),
          max_buffer_length(0),
          find_stream_info(true),
          push_source(false),
          live_source(false),
          buffer_size(0),
          tcp(false),
          multicast(false),
          format_context(nullptr),
          thread_running(false) {}

    void cleanup_sources() {
        for (auto vs : video_sources) {
            qcap2_video_source_delete(vs);
        }
        video_sources.clear();

        for (auto as : audio_sources) {
            qcap2_audio_source_delete(as);
        }
        audio_sources.clear();

        for (auto venc : video_encoders) {
            qcap2_video_encoder_delete(venc);
        }
        video_encoders.clear();

        for (auto aenc : audio_encoders) {
            qcap2_audio_encoder_delete(aenc);
        }
        audio_encoders.clear();

        for (auto prog : programs) {
            qcap2_program_info_delete(prog);
        }
        programs.clear();
    }

    ~qcap2_demuxer_priv_t() {
        cleanup_sources();
    }
};

static void demuxer_read_thread(qcap2_demuxer_priv_t* priv) {
    AVPacket* pkt = av_packet_alloc();
    if (!pkt) return;

    std::unordered_map<int, int64_t> stream_start_dts;
    auto start_time = std::chrono::steady_clock::now();

    while (priv->thread_running) {
        int ret = av_read_frame(priv->format_context, pkt);
        if (ret < 0) {
            break;
        }

        // Pacing by DTS if non-live source mode
        if (!priv->live_source) {
            auto stream = priv->format_context->streams[pkt->stream_index];
            if (stream_start_dts.find(pkt->stream_index) == stream_start_dts.end()) {
                stream_start_dts[pkt->stream_index] = pkt->dts;
            }
            int64_t start_dts = stream_start_dts[pkt->stream_index];
            if (pkt->dts != AV_NOPTS_VALUE && start_dts != AV_NOPTS_VALUE) {
                double elapsed_dts = (pkt->dts - start_dts) * av_q2d(stream->time_base);
                double elapsed_real = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time).count();
                double delay_s = elapsed_dts - elapsed_real;
                if (delay_s > 0.0) {
                    if (delay_s > 5.0) {
                        start_time += std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                            std::chrono::duration<double>(delay_s)
                        );
                    } else {
                        std::unique_lock<std::mutex> lock(priv->pacing_mtx);
                        priv->pacing_cv.wait_for(lock, std::chrono::duration<double>(delay_s), [priv] {
                            return !priv->thread_running;
                        });
                        if (!priv->thread_running) {
                            break;
                        }
                    }
                }
            }
        }

        // Prepare av_packet rcbuf for encoder
        qcap2_rcbuffer_t* enc_rcbuf = nullptr;
        qcap2_av_packet_t* av_pkt = new qcap2_av_packet_t;
        qcap2_av_packet_init(av_pkt);
        if (qcap2_av_packet_alloc_buffer(av_pkt, pkt->size)) {
            uint8_t* pBuf = nullptr;
            int nSize = 0;
            qcap2_av_packet_get_buffer(av_pkt, &pBuf, &nSize);
            if (pBuf) {
                memcpy(pBuf, pkt->data, pkt->size);
            }
            qcap2_av_packet_set_pts(av_pkt, pkt->pts);
            qcap2_av_packet_set_dts(av_pkt, pkt->dts);
            qcap2_av_packet_set_property(av_pkt, pkt->stream_index, (pkt->flags & AV_PKT_FLAG_KEY) ? TRUE : FALSE);
            qcap2_av_packet_set_sample_time(av_pkt, pkt->pts * av_q2d(priv->format_context->streams[pkt->stream_index]->time_base));
            
            enc_rcbuf = qcap2_rcbuffer_new(av_pkt, [](PVOID pData) {
                qcap2_av_packet_t* p = (qcap2_av_packet_t*)pData;
                if (p) {
                    qcap2_av_packet_free_buffer(p);
                    delete p;
                }
            });
        }

        // Try video encoders
        for (size_t idx = 0; idx < priv->video_sources.size(); idx++) {
            auto vs = priv->video_sources[idx];
            auto vs_priv = reinterpret_cast<qcap2_video_source_priv_t*>(vs);
            if (vs_priv->stream_index == pkt->stream_index) {
                if (enc_rcbuf && idx < priv->video_encoders.size()) {
                    auto venc = priv->video_encoders[idx];
                    auto venc_priv = reinterpret_cast<qcap2_video_encoder_priv_t*>(venc);
                    std::lock_guard<std::mutex> lock(*(venc_priv->mtx));
                    qcap2_rcbuffer_add_ref(enc_rcbuf);
                    venc_priv->output_queue.push(enc_rcbuf);
                    venc_priv->cv->notify_all();
                    if (venc_priv->event) {
                        qcap2_event_notify(venc_priv->event);
                    }
                }
                break;
            }
        }

        // Try audio encoders
        for (size_t idx = 0; idx < priv->audio_sources.size(); idx++) {
            auto as = priv->audio_sources[idx];
            auto as_priv = reinterpret_cast<qcap2_audio_source_priv_t*>(as);
            if (as_priv->stream_index == pkt->stream_index) {
                if (enc_rcbuf && idx < priv->audio_encoders.size()) {
                    auto aenc = priv->audio_encoders[idx];
                    auto aenc_priv = reinterpret_cast<qcap2_audio_encoder_priv_t*>(aenc);
                    std::lock_guard<std::mutex> lock(*(aenc_priv->mtx));
                    qcap2_rcbuffer_add_ref(enc_rcbuf);
                    aenc_priv->output_queue.push(enc_rcbuf);
                    aenc_priv->cv->notify_all();
                    if (aenc_priv->event) {
                        qcap2_event_notify(aenc_priv->event);
                    }
                }
                break;
            }
        }

        if (enc_rcbuf) qcap2_rcbuffer_release(enc_rcbuf);

        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);

    // Signal EOF/Stop to all encoders
    for (auto venc : priv->video_encoders) {
        auto venc_priv = reinterpret_cast<qcap2_video_encoder_priv_t*>(venc);
        std::lock_guard<std::mutex> lock(*(venc_priv->mtx));
        venc_priv->running = false;
        venc_priv->cv->notify_all();
        if (venc_priv->event) qcap2_event_notify(venc_priv->event);
    }
    for (auto aenc : priv->audio_encoders) {
        auto aenc_priv = reinterpret_cast<qcap2_audio_encoder_priv_t*>(aenc);
        std::lock_guard<std::mutex> lock(*(aenc_priv->mtx));
        aenc_priv->running = false;
        aenc_priv->cv->notify_all();
        if (aenc_priv->event) qcap2_event_notify(aenc_priv->event);
    }
}

QRESULT qcap2_demuxer_start(qcap2_demuxer_t* pThis) {
    if (!pThis) return QCAP_RS_ERROR_INVALID_PARAMETER;

    qcap2_demuxer_priv_t* priv = reinterpret_cast<qcap2_demuxer_priv_t*>(pThis);

    if (priv->format_context) {
        return QCAP_RS_SUCCESSFUL;
    }

    priv->format_context = avformat_alloc_context();
    if (!priv->format_context) {
        return QCAP_RS_ERROR_OUT_OF_MEMORY;
    }

    AVDictionary* options = nullptr;
    if (priv->tcp) av_dict_set(&options, "rtsp_transport", "tcp", 0);
    if (priv->multicast) av_dict_set(&options, "rtsp_transport", "udp_multicast", 0);

    const AVInputFormat* input_format = nullptr;
    if (!priv->format_name.empty()) {
        input_format = av_find_input_format(priv->format_name.c_str());
    }

    int ret = avformat_open_input(&priv->format_context, priv->url.c_str(), input_format, &options);
    if (options) av_dict_free(&options);

    if (ret < 0) {
        avformat_free_context(priv->format_context);
        priv->format_context = nullptr;
        return QCAP_RS_ERROR_FILE_ACCESS_FAIL;
    }

    if (priv->find_stream_info) {
        ret = avformat_find_stream_info(priv->format_context, nullptr);
        if (ret < 0) {
            avformat_close_input(&priv->format_context);
            return QCAP_RS_ERROR_FILE_ACCESS_FAIL;
        }
    }

    priv->cleanup_sources();

    for (unsigned int i = 0; i < priv->format_context->nb_streams; i++) {
        if (priv->format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            qcap2_video_source_t* vs = qcap2_video_source_new();
            if (vs) {
                qcap2_video_source_set_stream_index(vs, i);
                priv->video_sources.push_back(vs);
            }
            qcap2_video_encoder_t* venc = qcap2_video_encoder_new();
            if (venc) {
                auto venc_priv = reinterpret_cast<qcap2_video_encoder_priv_t*>(venc);
                venc_priv->running = true;
                venc_priv->enc_prop = qcap2_video_encoder_property_new();
                
                ULONG nEncoderFormat = 0xFFFFFFFF;
                if (priv->format_context->streams[i]->codecpar->codec_id == AV_CODEC_ID_H264) {
                    nEncoderFormat = QCAP_ENCODER_FORMAT_H264;
                } else if (priv->format_context->streams[i]->codecpar->codec_id == AV_CODEC_ID_HEVC) {
                    nEncoderFormat = QCAP_ENCODER_FORMAT_H265;
                } else if (priv->format_context->streams[i]->codecpar->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
                    nEncoderFormat = QCAP_ENCODER_FORMAT_MPEG2;
                } else if (priv->format_context->streams[i]->codecpar->codec_id == AV_CODEC_ID_MJPEG) {
                    nEncoderFormat = QCAP_ENCODER_FORMAT_MJPEG;
                }
                
                double fps = 25.0;
                if (priv->format_context->streams[i]->r_frame_rate.den > 0) {
                    fps = av_q2d(priv->format_context->streams[i]->r_frame_rate);
                } else if (priv->format_context->streams[i]->avg_frame_rate.den > 0) {
                    fps = av_q2d(priv->format_context->streams[i]->avg_frame_rate);
                }

                qcap2_video_encoder_property_set_property(venc_priv->enc_prop,
                    0, // type
                    nEncoderFormat,
                    0, // color space
                    priv->format_context->streams[i]->codecpar->width,
                    priv->format_context->streams[i]->codecpar->height,
                    fps,
                    0, 0,
                    priv->format_context->streams[i]->codecpar->bit_rate,
                    0, 0, 0
                );
                
                if (priv->format_context->streams[i]->codecpar->extradata && priv->format_context->streams[i]->codecpar->extradata_size > 0) {
                    qcap2_video_encoder_set_extra_data(venc, priv->format_context->streams[i]->codecpar->extradata, priv->format_context->streams[i]->codecpar->extradata_size);
                }
                
                priv->video_encoders.push_back(venc);
            }
        } else if (priv->format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            qcap2_audio_source_t* as = qcap2_audio_source_new();
            if (as) {
                reinterpret_cast<qcap2_audio_source_priv_t*>(as)->stream_index = i;
                priv->audio_sources.push_back(as);
            }
            qcap2_audio_encoder_t* aenc = qcap2_audio_encoder_new();
            if (aenc) {
                auto aenc_priv = reinterpret_cast<qcap2_audio_encoder_priv_t*>(aenc);
                aenc_priv->running = true;
                
                ULONG nEncoderFormat = 0xFFFFFFFF;
                if (priv->format_context->streams[i]->codecpar->codec_id == AV_CODEC_ID_AAC) {
                    nEncoderFormat = QCAP_ENCODER_FORMAT_AAC;
                } else if (priv->format_context->streams[i]->codecpar->codec_id == AV_CODEC_ID_MP3) {
                    nEncoderFormat = QCAP_ENCODER_FORMAT_MP3;
                } else if (priv->format_context->streams[i]->codecpar->codec_id == AV_CODEC_ID_MP2) {
                    nEncoderFormat = QCAP_ENCODER_FORMAT_MP2;
                } else if (priv->format_context->streams[i]->codecpar->codec_id == AV_CODEC_ID_OPUS) {
                    nEncoderFormat = QCAP_ENCODER_FORMAT_OPUS;
                } else if (priv->format_context->streams[i]->codecpar->codec_id == AV_CODEC_ID_AC3) {
                    nEncoderFormat = QCAP_ENCODER_FORMAT_AC3;
                } else if (priv->format_context->streams[i]->codecpar->codec_id == AV_CODEC_ID_PCM_ALAW) {
                    nEncoderFormat = QCAP_ENCODER_FORMAT_G711_ALAW;
                } else if (priv->format_context->streams[i]->codecpar->codec_id == AV_CODEC_ID_PCM_MULAW) {
                    nEncoderFormat = QCAP_ENCODER_FORMAT_G711_ULAW;
                } else if (priv->format_context->streams[i]->codecpar->codec_id == AV_CODEC_ID_PCM_S16LE) {
                    nEncoderFormat = QCAP_ENCODER_FORMAT_PCM;
                }

                qcap2_audio_encoder_property_set_property1(aenc_priv->property,
                    0, // type
                    nEncoderFormat,
                    priv->format_context->streams[i]->codecpar->ch_layout.nb_channels,
                    priv->format_context->streams[i]->codecpar->bits_per_coded_sample,
                    priv->format_context->streams[i]->codecpar->sample_rate,
                    priv->format_context->streams[i]->codecpar->bit_rate
                );

                if (priv->format_context->streams[i]->codecpar->extradata && priv->format_context->streams[i]->codecpar->extradata_size > 0) {
                    qcap2_audio_encoder_set_extra_data(aenc, priv->format_context->streams[i]->codecpar->extradata, priv->format_context->streams[i]->codecpar->extradata_size);
                }

                priv->audio_encoders.push_back(aenc);
            }
        }
    }

    // Program Embedding
    if (priv->format_context->nb_programs == 0) {
        qcap2_program_info_t* prog = qcap2_program_info_new();
        if (prog) {
            qcap2_program_info_set_id(prog, 1);
            qcap2_program_info_set_number(prog, 1);
            
            qcap2_program_info_set_video_source_count(prog, priv->video_sources.size());
            for (size_t i = 0; i < priv->video_sources.size(); i++) {
                qcap2_program_info_set_video_source_index(prog, i, i);
            }
            
            qcap2_program_info_set_audio_source_count(prog, priv->audio_sources.size());
            for (size_t i = 0; i < priv->audio_sources.size(); i++) {
                qcap2_program_info_set_audio_source_index(prog, i, i);
            }

            qcap2_program_info_set_video_encoder_count(prog, priv->video_encoders.size());
            for (size_t i = 0; i < priv->video_encoders.size(); i++) {
                qcap2_program_info_set_video_encoder_index(prog, i, i);
            }

            qcap2_program_info_set_audio_encoder_count(prog, priv->audio_encoders.size());
            for (size_t i = 0; i < priv->audio_encoders.size(); i++) {
                qcap2_program_info_set_audio_encoder_index(prog, i, i);
            }
            
            priv->programs.push_back(prog);
        }
    } else {
        for (unsigned int p = 0; p < priv->format_context->nb_programs; p++) {
            AVProgram* av_prog = priv->format_context->programs[p];
            qcap2_program_info_t* prog = qcap2_program_info_new();
            if (prog) {
                qcap2_program_info_set_id(prog, av_prog->id);
                qcap2_program_info_set_number(prog, av_prog->program_num);
                
                int vs_count = 0;
                int as_count = 0;
                int venc_count = 0;
                int aenc_count = 0;
                
                for (unsigned int s = 0; s < av_prog->nb_stream_indexes; s++) {
                    unsigned int stream_idx = av_prog->stream_index[s];
                    
                    // Find matching video source
                    for (size_t i = 0; i < priv->video_sources.size(); i++) {
                        auto vs_priv = reinterpret_cast<qcap2_video_source_priv_t*>(priv->video_sources[i]);
                        if (vs_priv->stream_index == (int)stream_idx) {
                            qcap2_program_info_set_video_source_index(prog, vs_count++, i);
                            qcap2_program_info_set_video_encoder_index(prog, venc_count++, i);
                            break;
                        }
                    }
                    
                    // Find matching audio source
                    for (size_t i = 0; i < priv->audio_sources.size(); i++) {
                        auto as_priv = reinterpret_cast<qcap2_audio_source_priv_t*>(priv->audio_sources[i]);
                        if (as_priv->stream_index == (int)stream_idx) {
                            qcap2_program_info_set_audio_source_index(prog, as_count++, i);
                            qcap2_program_info_set_audio_encoder_index(prog, aenc_count++, i);
                            break;
                        }
                    }
                }
                
                qcap2_program_info_set_video_source_count(prog, vs_count);
                qcap2_program_info_set_video_encoder_count(prog, venc_count);
                qcap2_program_info_set_audio_source_count(prog, as_count);
                qcap2_program_info_set_audio_encoder_count(prog, aenc_count);
                
                AVDictionaryEntry* entry = av_dict_get(av_prog->metadata, "service_name", nullptr, 0);
                if (entry) {
                    qcap2_program_info_set_metadata(prog, "service_name", entry->value);
                }
                entry = av_dict_get(av_prog->metadata, "service_provider", nullptr, 0);
                if (entry) {
                    qcap2_program_info_set_metadata(prog, "service_provider", entry->value);
                }
                
                priv->programs.push_back(prog);
            }
        }
    }

    return QCAP_RS_SUCCESSFUL;
}

QRESULT qcap2_demuxer_play(qcap2_demuxer_t* pThis) {
    if (!pThis) return QCAP_RS_ERROR_INVALID_PARAMETER;
    qcap2_demuxer_priv_t* priv = reinterpret_cast<qcap2_demuxer_priv_t*>(pThis);

    if (!priv->format_context) return QCAP_RS_ERROR_INVALID_DEVICE;
    if (priv->thread_running) return QCAP_RS_SUCCESSFUL;

    priv->thread_running = true;
    priv->reader_thread = std::thread(demuxer_read_thread, priv);

    return QCAP_RS_SUCCESSFUL;
}

QRESULT qcap2_demuxer_stop(qcap2_demuxer_t* pThis) {
    if (!pThis) return QCAP_RS_ERROR_INVALID_PARAMETER;

    qcap2_demuxer_priv_t* priv = reinterpret_cast<qcap2_demuxer_priv_t*>(pThis);

    if (priv->thread_running) {
        priv->thread_running = false;
        priv->pacing_cv.notify_all();
        if (priv->reader_thread.joinable()) {
            priv->reader_thread.join();
        }
    }

    if (priv->format_context) {
        avformat_close_input(&priv->format_context);
        priv->format_context = nullptr;
    }

    priv->cleanup_sources();

    return QCAP_RS_SUCCESSFUL;
}
qcap2_demuxer_t* qcap2_demuxer_new() {
    return reinterpret_cast<qcap2_demuxer_t*>(new (std::nothrow) qcap2_demuxer_priv_t());
}

void qcap2_demuxer_delete(qcap2_demuxer_t* pThis) {
    if (pThis) delete reinterpret_cast<qcap2_demuxer_priv_t*>(pThis);
}

void qcap2_demuxer_set_type(qcap2_demuxer_t* pThis, int nType) {
    if (pThis) reinterpret_cast<qcap2_demuxer_priv_t*>(pThis)->type = nType;
}

void qcap2_demuxer_set_event(qcap2_demuxer_t* pThis, qcap2_event_t* pEvent) {
    if (pThis) reinterpret_cast<qcap2_demuxer_priv_t*>(pThis)->event = pEvent;
}

void qcap2_demuxer_set_url(qcap2_demuxer_t* pThis, const char* strURL) {
    if (pThis && strURL) reinterpret_cast<qcap2_demuxer_priv_t*>(pThis)->url = strURL;
}

void qcap2_demuxer_set_max_buffer_length(qcap2_demuxer_t* pThis, int nMaxBufferLength) {
    if (pThis) reinterpret_cast<qcap2_demuxer_priv_t*>(pThis)->max_buffer_length = nMaxBufferLength;
}

void qcap2_demuxer_set_find_stream_info(qcap2_demuxer_t* pThis, bool bFindStreamInfo) {
    if (pThis) reinterpret_cast<qcap2_demuxer_priv_t*>(pThis)->find_stream_info = bFindStreamInfo;
}

void qcap2_demuxer_set_push_source(qcap2_demuxer_t* pThis, bool bPushSource) {
    if (pThis) reinterpret_cast<qcap2_demuxer_priv_t*>(pThis)->push_source = bPushSource;
}

void qcap2_demuxer_set_live_source(qcap2_demuxer_t* pThis, bool bLiveSource) {
    if (pThis) reinterpret_cast<qcap2_demuxer_priv_t*>(pThis)->live_source = bLiveSource;
}

void qcap2_demuxer_set_format_name(qcap2_demuxer_t* pThis, const char* strFormatName) {
    if (pThis && strFormatName) reinterpret_cast<qcap2_demuxer_priv_t*>(pThis)->format_name = strFormatName;
}

void qcap2_demuxer_set_buffer_size(qcap2_demuxer_t* pThis, int nBufferSize) {
    if (pThis) reinterpret_cast<qcap2_demuxer_priv_t*>(pThis)->buffer_size = nBufferSize;
}

void qcap2_demuxer_set_sdp_lines(qcap2_demuxer_t* pThis, const char* strSdpLines) {
    if (pThis && strSdpLines) reinterpret_cast<qcap2_demuxer_priv_t*>(pThis)->sdp_lines = strSdpLines;
}

void qcap2_demuxer_set_tcp(qcap2_demuxer_t* pThis, bool bUseTCP) {
    if (pThis) reinterpret_cast<qcap2_demuxer_priv_t*>(pThis)->tcp = bUseTCP;
}

void qcap2_demuxer_set_multicast(qcap2_demuxer_t* pThis, bool bMulticast) {
    if (pThis) reinterpret_cast<qcap2_demuxer_priv_t*>(pThis)->multicast = bMulticast;
}

QRESULT qcap2_demuxer_push(qcap2_demuxer_t* pThis, qcap2_rcbuffer_t* pRCBuffer) {
    (void)pThis; (void)pRCBuffer;
    return QCAP_RS_ERROR_NON_SUPPORT;
}

int qcap2_demuxer_get_video_source_count(qcap2_demuxer_t* pThis) {
    if (!pThis) return 0;
    return static_cast<int>(reinterpret_cast<qcap2_demuxer_priv_t*>(pThis)->video_sources.size());
}

int qcap2_demuxer_get_audio_source_count(qcap2_demuxer_t* pThis) {
    if (!pThis) return 0;
    return static_cast<int>(reinterpret_cast<qcap2_demuxer_priv_t*>(pThis)->audio_sources.size());
}

int qcap2_demuxer_get_video_encoder_count(qcap2_demuxer_t* pThis) {
    if (!pThis) return 0;
    return static_cast<int>(reinterpret_cast<qcap2_demuxer_priv_t*>(pThis)->video_encoders.size());
}

int qcap2_demuxer_get_audio_encoder_count(qcap2_demuxer_t* pThis) {
    if (!pThis) return 0;
    return static_cast<int>(reinterpret_cast<qcap2_demuxer_priv_t*>(pThis)->audio_encoders.size());
}

int qcap2_demuxer_get_program_count(qcap2_demuxer_t* pThis) {
    if (!pThis) return 0;
    return static_cast<int>(reinterpret_cast<qcap2_demuxer_priv_t*>(pThis)->programs.size());
}

qcap2_video_source_t* qcap2_demuxer_get_video_source(qcap2_demuxer_t* pThis, int nIndex) {
    if (!pThis) return nullptr;
    qcap2_demuxer_priv_t* priv = reinterpret_cast<qcap2_demuxer_priv_t*>(pThis);
    if (nIndex >= 0 && nIndex < static_cast<int>(priv->video_sources.size())) {
        return priv->video_sources[nIndex];
    }
    return nullptr;
}

qcap2_audio_source_t* qcap2_demuxer_get_audio_source(qcap2_demuxer_t* pThis, int nIndex) {
    if (!pThis) return nullptr;
    qcap2_demuxer_priv_t* priv = reinterpret_cast<qcap2_demuxer_priv_t*>(pThis);
    if (nIndex >= 0 && nIndex < static_cast<int>(priv->audio_sources.size())) {
        return priv->audio_sources[nIndex];
    }
    return nullptr;
}

qcap2_video_encoder_t* qcap2_demuxer_get_video_encoder(qcap2_demuxer_t* pThis, int nIndex) {
    if (!pThis) return nullptr;
    qcap2_demuxer_priv_t* priv = reinterpret_cast<qcap2_demuxer_priv_t*>(pThis);
    if (nIndex >= 0 && nIndex < static_cast<int>(priv->video_encoders.size())) {
        return priv->video_encoders[nIndex];
    }
    return nullptr;
}

qcap2_audio_encoder_t* qcap2_demuxer_get_audio_encoder(qcap2_demuxer_t* pThis, int nIndex) {
    if (!pThis) return nullptr;
    qcap2_demuxer_priv_t* priv = reinterpret_cast<qcap2_demuxer_priv_t*>(pThis);
    if (nIndex >= 0 && nIndex < static_cast<int>(priv->audio_encoders.size())) {
        return priv->audio_encoders[nIndex];
    }
    return nullptr;
}

qcap2_program_info_t* qcap2_demuxer_get_program_info(qcap2_demuxer_t* pThis, int nIndex) {
    if (!pThis) return nullptr;
    qcap2_demuxer_priv_t* priv = reinterpret_cast<qcap2_demuxer_priv_t*>(pThis);
    if (nIndex >= 0 && nIndex < static_cast<int>(priv->programs.size())) {
        return priv->programs[nIndex];
    }
    return nullptr;
}

QRESULT qcap2_demuxer_update(qcap2_demuxer_t* pThis) {
    (void)pThis; return QCAP_RS_SUCCESSFUL;
}
