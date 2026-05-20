#include "qcap2.devices_priv.h"
#include "qcap2.user.h"
#include "qcap2.buffer.h"
#include <vector>
#include <thread>
#include <atomic>
#include <string>
#include <new>
#include <cstdlib>

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

    std::thread reader_thread;
    std::atomic<bool> thread_running;

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
    }

    ~qcap2_demuxer_priv_t() {
        cleanup_sources();
    }
};

static void demuxer_read_thread(qcap2_demuxer_priv_t* priv) {
    AVPacket* pkt = av_packet_alloc();
    if (!pkt) return;

    while (priv->thread_running) {
        int ret = av_read_frame(priv->format_context, pkt);
        if (ret < 0) {
            // EOF or error
            break;
        }

        // We will just allocate a buffer with the packet size and copy data since we don't have new_av_packet
        qcap2_rcbuffer_t* rcbuf = qcap2_rcbuffer_new(malloc(pkt->size), free);
        if (rcbuf) {
            void* data = qcap2_rcbuffer_lock_data(rcbuf);
            if (data) {
                memcpy(data, pkt->data, pkt->size);
                qcap2_rcbuffer_unlock_data(rcbuf);
            }
            bool handled = false;

            // Try video sources
            for (auto vs : priv->video_sources) {
                auto vs_priv = reinterpret_cast<qcap2_video_source_priv_t*>(vs);
                if (vs_priv->stream_index == pkt->stream_index) {
                    qcap2_rcbuffer_queue_push(vs_priv->queue, rcbuf);
                    handled = true;
                    break;
                }
            }

            // Try audio sources
            if (!handled) {
                for (auto as : priv->audio_sources) {
                    auto as_priv = reinterpret_cast<qcap2_audio_source_priv_t*>(as);
                    if (as_priv->stream_index == pkt->stream_index) {
                        qcap2_rcbuffer_queue_push(as_priv->queue, rcbuf);
                        handled = true;
                        break;
                    }
                }
            }

            if (!handled) {
                // Not mapped, release buffer
                qcap2_rcbuffer_release(rcbuf);
            }
        }

        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);
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
        } else if (priv->format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            qcap2_audio_source_t* as = qcap2_audio_source_new();
            if (as) {
                reinterpret_cast<qcap2_audio_source_priv_t*>(as)->stream_index = i;
                priv->audio_sources.push_back(as);
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
    (void)pThis; return 0;
}

int qcap2_demuxer_get_audio_encoder_count(qcap2_demuxer_t* pThis) {
    (void)pThis; return 0;
}

int qcap2_demuxer_get_program_count(qcap2_demuxer_t* pThis) {
    (void)pThis; return 0;
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
    (void)pThis; (void)nIndex; return nullptr;
}

qcap2_audio_encoder_t* qcap2_demuxer_get_audio_encoder(qcap2_demuxer_t* pThis, int nIndex) {
    (void)pThis; (void)nIndex; return nullptr;
}

qcap2_program_info_t* qcap2_demuxer_get_program_info(qcap2_demuxer_t* pThis, int nIndex) {
    (void)pThis; (void)nIndex; return nullptr;
}

QRESULT qcap2_demuxer_update(qcap2_demuxer_t* pThis) {
    (void)pThis; return QCAP_RS_SUCCESSFUL;
}
