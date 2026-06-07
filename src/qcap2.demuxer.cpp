#include "qcap2.devices_priv.h"
#include "qcap2.user.h"
#include "qcap2.buffer.h"
#include "qcap2.buffer.ffmpeg.h"
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
#include <libavutil/avutil.h>
#include <libavutil/dict.h>
}

// ============================================================================
// RTSP-specific private data
// ============================================================================

// ============================================================================
// Mock Demuxer Private Data
// ============================================================================
struct qcap2_demuxer_mock_priv_t {
    int format_version; // 0, 1, 2, ...
    int frame_count;
    int video_width;
    int video_height;
    int audio_samplerate;
    std::mutex mtx;

    qcap2_demuxer_mock_priv_t() : format_version(0), frame_count(0), video_width(640), video_height(480), audio_samplerate(44100) {}
};

struct qcap2_demuxer_rtsp_priv_t {
    std::string user_agent;
    std::string username;
    std::string password;
    int timeout_ms;                  // connection/read timeout in ms
    int reconnect_delay_ms;          // delay between reconnection attempts
    int max_reconnect_attempts;      // 0 = infinite
    int keep_alive_interval_ms;      // RTSP keep-alive (OPTIONS) interval
    std::string transport;           // "tcp", "udp", "udp_multicast", or empty for auto
    int buffer_size;                 // socket buffer size
    bool user_agent_set;

    // Stats
    uint64_t total_bytes_read;
    uint64_t total_packets_read;
    int reconnect_count;
    int64_t last_keep_alive_ms;

    qcap2_demuxer_rtsp_priv_t()
        : timeout_ms(5000)
        , reconnect_delay_ms(3000)
        , max_reconnect_attempts(0)
        , keep_alive_interval_ms(30000)
        , buffer_size(0)
        , user_agent_set(false)
        , total_bytes_read(0)
        , total_packets_read(0)
        , reconnect_count(0)
        , last_keep_alive_ms(0) {}
};

// ============================================================================
// Private data (extended with RTSP fields)
// ============================================================================
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

    // RTSP-specific state (valid only when type == QCAP2_DEMUXER_TYPE_RTSP)
    qcap2_demuxer_rtsp_priv_t* rtsp;
    qcap2_demuxer_mock_priv_t* mock;
    std::thread keep_alive_thread;
    std::atomic<bool> keep_alive_running;
    std::mutex rtsp_mtx;

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
          thread_running(false),
          rtsp(nullptr),
          mock(nullptr),
          keep_alive_running(false) {}

    ~qcap2_demuxer_priv_t() {
        if (mock) {
            delete mock;
            mock = nullptr;
        }
        if (rtsp) {
            delete rtsp;
            rtsp = nullptr;
        }
        cleanup_sources();
    }

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
};

// ============================================================================
// Forward declarations
// ============================================================================
static void demuxer_read_thread(qcap2_demuxer_priv_t* priv);
static void demuxer_rtsp_read_thread(qcap2_demuxer_priv_t* priv);
static void demuxer_rtsp_keep_alive_thread(qcap2_demuxer_priv_t* priv);
static QRESULT demuxer_rtsp_do_start(qcap2_demuxer_priv_t* priv);

static QRESULT demuxer_mock_do_start(qcap2_demuxer_priv_t* priv);
static QRESULT demuxer_mock_do_play(qcap2_demuxer_priv_t* priv);
static QRESULT demuxer_mock_do_stop(qcap2_demuxer_priv_t* priv);
static void demuxer_mock_read_thread(qcap2_demuxer_priv_t* priv);

static QRESULT demuxer_rtsp_do_play(qcap2_demuxer_priv_t* priv);
static QRESULT demuxer_rtsp_do_stop(qcap2_demuxer_priv_t* priv);

// ============================================================================
// Common read thread (for DEFAULT demuxer)
// ============================================================================
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
        AVPacket* target_pkt = av_packet_alloc();
        if (target_pkt) {
            if (av_packet_ref(target_pkt, pkt) >= 0) {
                double sample_time = pkt->pts * av_q2d(priv->format_context->streams[pkt->stream_index]->time_base);
                enc_rcbuf = new qcap2_avpacket_buffer(target_pkt, sample_time);
            } else {
                av_packet_free(&target_pkt);
            }
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

// ============================================================================
// Create video/audio sources and encoders from format context (shared helper)
// ============================================================================
static QRESULT demuxer_create_sources_from_context(qcap2_demuxer_priv_t* priv) {
    if (!priv->format_context) return QCAP_RS_ERROR_INVALID_DEVICE;

    priv->cleanup_sources();

    for (unsigned int i = 0; i < priv->format_context->nb_streams; i++) {
        AVStream* stream = priv->format_context->streams[i];
        AVCodecParameters* par = stream->codecpar;

        if (par->codec_type == AVMEDIA_TYPE_VIDEO) {
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
                if (par->codec_id == AV_CODEC_ID_H264) {
                    nEncoderFormat = QCAP_ENCODER_FORMAT_H264;
                } else if (par->codec_id == AV_CODEC_ID_HEVC) {
                    nEncoderFormat = QCAP_ENCODER_FORMAT_H265;
                } else if (par->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
                    nEncoderFormat = QCAP_ENCODER_FORMAT_MPEG2;
                } else if (par->codec_id == AV_CODEC_ID_MJPEG) {
                    nEncoderFormat = QCAP_ENCODER_FORMAT_MJPEG;
                }
                
                double fps = 25.0;
                if (stream->r_frame_rate.den > 0) {
                    fps = av_q2d(stream->r_frame_rate);
                } else if (stream->avg_frame_rate.den > 0) {
                    fps = av_q2d(stream->avg_frame_rate);
                }

                qcap2_video_encoder_property_set_property(venc_priv->enc_prop,
                    0, // type
                    nEncoderFormat,
                    0, // color space
                    par->width,
                    par->height,
                    fps,
                    0, 0,
                    par->bit_rate,
                    0, 0, 0
                );
                
                if (par->extradata && par->extradata_size > 0) {
                    qcap2_video_encoder_set_extra_data(venc, par->extradata, par->extradata_size);
                }
                
                priv->video_encoders.push_back(venc);
            }
        } else if (par->codec_type == AVMEDIA_TYPE_AUDIO) {
            // Check if it's a supported audio format we want to handle
            bool supported = false;
            switch (par->codec_id) {
                case AV_CODEC_ID_AAC:
                case AV_CODEC_ID_MP3:
                case AV_CODEC_ID_MP2:
                case AV_CODEC_ID_OPUS:
                case AV_CODEC_ID_AC3:
                case AV_CODEC_ID_PCM_ALAW:
                case AV_CODEC_ID_PCM_MULAW:
                case AV_CODEC_ID_PCM_S16LE:
                case AV_CODEC_ID_PCM_S16BE:
                case AV_CODEC_ID_PCM_U16LE:
                case AV_CODEC_ID_PCM_U16BE:
                    supported = true;
                    break;
                default:
                    supported = false;
                    break;
            }
            
            if (!supported) continue;

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
                if (par->codec_id == AV_CODEC_ID_AAC) {
                    nEncoderFormat = QCAP_ENCODER_FORMAT_AAC;
                } else if (par->codec_id == AV_CODEC_ID_MP3) {
                    nEncoderFormat = QCAP_ENCODER_FORMAT_MP3;
                } else if (par->codec_id == AV_CODEC_ID_MP2) {
                    nEncoderFormat = QCAP_ENCODER_FORMAT_MP2;
                } else if (par->codec_id == AV_CODEC_ID_OPUS) {
                    nEncoderFormat = QCAP_ENCODER_FORMAT_OPUS;
                } else if (par->codec_id == AV_CODEC_ID_AC3) {
                    nEncoderFormat = QCAP_ENCODER_FORMAT_AC3;
                } else if (par->codec_id == AV_CODEC_ID_PCM_ALAW) {
                    nEncoderFormat = QCAP_ENCODER_FORMAT_G711_ALAW;
                } else if (par->codec_id == AV_CODEC_ID_PCM_MULAW) {
                    nEncoderFormat = QCAP_ENCODER_FORMAT_G711_ULAW;
                } else if (par->codec_id == AV_CODEC_ID_PCM_S16LE ||
                           par->codec_id == AV_CODEC_ID_PCM_S16BE ||
                           par->codec_id == AV_CODEC_ID_PCM_U16LE ||
                           par->codec_id == AV_CODEC_ID_PCM_U16BE) {
                    nEncoderFormat = QCAP_ENCODER_FORMAT_PCM;
                }

                int bits_per_sample = par->bits_per_coded_sample;
                if (bits_per_sample <= 0) bits_per_sample = 16;

                qcap2_audio_encoder_property_set_property1(aenc_priv->property,
                    0, // type
                    nEncoderFormat,
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(59, 37, 100)
                    par->ch_layout.nb_channels,
#else
                    par->channels,
#endif
                    bits_per_sample,
                    par->sample_rate,
                    par->bit_rate
                );

                if (par->extradata && par->extradata_size > 0) {
                    qcap2_audio_encoder_set_extra_data(aenc, par->extradata, par->extradata_size);
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

// ============================================================================
// RTSP demuxer start
// ============================================================================
static QRESULT demuxer_rtsp_do_start(qcap2_demuxer_priv_t* priv) {
    // Initialize RTSP private data on first call
    if (!priv->rtsp) {
        priv->rtsp = new qcap2_demuxer_rtsp_priv_t();
        // Copy relevant config from parent
        if (priv->buffer_size > 0) priv->rtsp->buffer_size = priv->buffer_size;
        if (priv->tcp) priv->rtsp->transport = "tcp";
        if (priv->multicast) priv->rtsp->transport = "udp_multicast";
    }

    // Don't reconnect if already connected
    if (priv->format_context) {
        return QCAP_RS_SUCCESSFUL;
    }

    priv->format_context = avformat_alloc_context();
    if (!priv->format_context) {
        return QCAP_RS_ERROR_OUT_OF_MEMORY;
    }

    // RTSP-specific options
    AVDictionary* options = nullptr;

    // Transport mode: tcp, udp, udp_multicast
    std::string transport = priv->rtsp->transport;
    if (transport.empty()) {
        transport = priv->tcp ? "tcp" : (priv->multicast ? "udp_multicast" : "tcp");
    }
    av_dict_set(&options, "rtsp_transport", transport.c_str(), 0);

    // Timeout
    if (priv->rtsp->timeout_ms > 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", priv->rtsp->timeout_ms);
        av_dict_set(&options, "timeout", buf, 0);
        av_dict_set(&options, "stimeout", buf, 0);
    }

    // User-Agent
    if (priv->rtsp->user_agent_set && !priv->rtsp->user_agent.empty()) {
        av_dict_set(&options, "user_agent", priv->rtsp->user_agent.c_str(), 0);
    }

    // Buffer size
    if (priv->rtsp->buffer_size > 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", priv->rtsp->buffer_size);
        av_dict_set(&options, "buffer_size", buf, 0);
    }

    // Always set as live source for RTSP
    av_dict_set(&options, "fflags", "nobuffer", 0);
    av_dict_set(&options, "flags", "low_delay", 0);
    av_dict_set(&options, "max_delay", "0", 0);

    // Try opening with RTSP transport
    const AVInputFormat* input_format = av_find_input_format("rtsp");

    int ret = avformat_open_input(&priv->format_context, priv->url.c_str(), const_cast<AVInputFormat*>(input_format), &options);
    if (options) av_dict_free(&options);

    if (ret < 0) {
        avformat_free_context(priv->format_context);
        priv->format_context = nullptr;

        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        (void)errbuf;

        return QCAP_RS_ERROR_NETWORK_ACCESS_FAIL;
    }

    if (priv->find_stream_info) {
        ret = avformat_find_stream_info(priv->format_context, nullptr);
        if (ret < 0) {
            avformat_close_input(&priv->format_context);
            return QCAP_RS_ERROR_FILE_ACCESS_FAIL;
        }
    }

    QRESULT res = demuxer_create_sources_from_context(priv);

    // Mark start time for keep-alive
    priv->rtsp->last_keep_alive_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    return res;
}

// ============================================================================
// RTSP read thread with keep-alive and reconnection support
// ============================================================================
static void demuxer_rtsp_read_thread(qcap2_demuxer_priv_t* priv) {
    AVPacket* pkt = av_packet_alloc();
    if (!pkt) return;

    auto start_time = std::chrono::steady_clock::now();

    while (priv->thread_running) {
        // Check if format_context is still valid
        if (!priv->format_context) {
            // Attempt reconnection
            std::unique_lock<std::mutex> lock(priv->rtsp_mtx);
            if (priv->rtsp->max_reconnect_attempts == 0 ||
                priv->rtsp->reconnect_count < priv->rtsp->max_reconnect_attempts) {
                // Brief delay before reconnecting
                lock.unlock();
                std::this_thread::sleep_for(std::chrono::milliseconds(priv->rtsp->reconnect_delay_ms));
                lock.lock();
                if (!priv->thread_running) break;
                
                QRESULT ret = demuxer_rtsp_do_start(priv);
                if (ret == QCAP_RS_SUCCESSFUL) {
                    priv->rtsp->reconnect_count++;
                    lock.unlock();
                    start_time = std::chrono::steady_clock::now();
                    continue;
                }
            }
            break; // Give up reconnecting
        }

        int ret = av_read_frame(priv->format_context, pkt);
        if (ret < 0) {
            // Stream error or EOF - trigger reconnection
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            (void)errbuf;

            std::lock_guard<std::mutex> lock(priv->rtsp_mtx);
            if (priv->format_context) {
                avformat_close_input(&priv->format_context);
                priv->format_context = nullptr;
                priv->cleanup_sources();
            }
            av_packet_unref(pkt);
            continue; // Will trigger reconnection on next loop iteration
        }

        // RTSP is always live source, no pacing needed
        priv->rtsp->total_bytes_read += pkt->size;
        priv->rtsp->total_packets_read++;

        // Prepare av_packet rcbuf for encoder
        qcap2_rcbuffer_t* enc_rcbuf = nullptr;
        AVPacket* target_pkt = av_packet_alloc();
        if (target_pkt) {
            if (av_packet_ref(target_pkt, pkt) >= 0) {
                double sample_time = pkt->pts * av_q2d(priv->format_context->streams[pkt->stream_index]->time_base);
                enc_rcbuf = new qcap2_avpacket_buffer(target_pkt, sample_time);
            } else {
                av_packet_free(&target_pkt);
            }
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

// ============================================================================
// RTSP keep-alive thread (sends periodic OPTIONS requests)
// ============================================================================
static void demuxer_rtsp_keep_alive_thread(qcap2_demuxer_priv_t* priv) {
    if (!priv->rtsp) return;

    while (priv->keep_alive_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(priv->rtsp->keep_alive_interval_ms / 2));

        if (!priv->keep_alive_running) break;

        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();

        if (now_ms - priv->rtsp->last_keep_alive_ms >= priv->rtsp->keep_alive_interval_ms) {
            std::lock_guard<std::mutex> lock(priv->rtsp_mtx);
            if (priv->format_context) {
                // AVFormatContext for RTSP doesn't have a direct OPTIONS API,
                // but we can trigger a probe by reading with a small timeout.
                // FFmpeg's RTSP demuxer handles keep-alive internally via the
                // "rtsp_flags" option with "listen" or via periodic RTCP.
                // Setting "rtsp_transport" to tcp ensures the TCP connection
                // stays alive through normal data flow. If data flow stops,
                // the reconnection logic in the read thread handles it.
                priv->rtsp->last_keep_alive_ms = now_ms;
            }
        }
    }
}

// ============================================================================
// RTSP play
// ============================================================================
static QRESULT demuxer_rtsp_do_play(qcap2_demuxer_priv_t* priv) {
    if (!priv->format_context) return QCAP_RS_ERROR_INVALID_DEVICE;
    if (priv->thread_running) return QCAP_RS_SUCCESSFUL;

    // Start the keep-alive thread
    if (priv->rtsp && priv->rtsp->keep_alive_interval_ms > 0 && !priv->keep_alive_running) {
        priv->keep_alive_running = true;
        priv->keep_alive_thread = std::thread(demuxer_rtsp_keep_alive_thread, priv);
    }

    priv->thread_running = true;
    priv->reader_thread = std::thread(demuxer_rtsp_read_thread, priv);

    return QCAP_RS_SUCCESSFUL;
}

// ============================================================================
// RTSP stop
// ============================================================================
static QRESULT demuxer_rtsp_do_stop(qcap2_demuxer_priv_t* priv) {
    // Stop keep-alive thread
    if (priv->keep_alive_running) {
        priv->keep_alive_running = false;
        if (priv->keep_alive_thread.joinable()) {
            priv->keep_alive_thread.join();
        }
    }

    // Stop reader thread
    if (priv->thread_running) {
        priv->thread_running = false;
        if (priv->reader_thread.joinable()) {
            priv->reader_thread.join();
        }
    }

    // Close RTSP connection
    {
        std::lock_guard<std::mutex> lock(priv->rtsp_mtx);
        if (priv->format_context) {
            avformat_close_input(&priv->format_context);
            priv->format_context = nullptr;
        }
        priv->cleanup_sources();
    }

    return QCAP_RS_SUCCESSFUL;
}

// ============================================================================
// API: qcap2_demuxer_start
// ============================================================================
QRESULT qcap2_demuxer_start(qcap2_demuxer_t* pThis) {
    if (!pThis) return QCAP_RS_ERROR_INVALID_PARAMETER;

    qcap2_demuxer_priv_t* priv = reinterpret_cast<qcap2_demuxer_priv_t*>(pThis);

    if (priv->type == QCAP2_DEMUXER_TYPE_MOCK) {
        return demuxer_mock_do_start(priv);
    }

    if (priv->type == QCAP2_DEMUXER_TYPE_RTSP) {
        return demuxer_rtsp_do_start(priv);
    }

    if (priv->format_context) {
        return QCAP_RS_SUCCESSFUL;
    }

    priv->format_context = avformat_alloc_context();
    if (!priv->format_context) {
        return QCAP_RS_ERROR_OUT_OF_MEMORY;
    }

    AVDictionary* options = nullptr;
    const AVInputFormat* input_format = nullptr;

    if (priv->type == QCAP2_DEMUXER_TYPE_SDP) {
        input_format = av_find_input_format("sdp");
        av_dict_set(&options, "protocol_whitelist", "file,rtp,udp", 0);
        priv->live_source = true;
    } else {
        if (!priv->format_name.empty()) {
            input_format = av_find_input_format(priv->format_name.c_str());
        }
        if (priv->tcp) av_dict_set(&options, "rtsp_transport", "tcp", 0);
        if (priv->multicast) av_dict_set(&options, "rtsp_transport", "udp_multicast", 0);
    }

    int ret = avformat_open_input(&priv->format_context, priv->url.c_str(), const_cast<AVInputFormat*>(input_format), &options);
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

    return demuxer_create_sources_from_context(priv);
}

// ============================================================================
// API: qcap2_demuxer_play
// ============================================================================
QRESULT qcap2_demuxer_play(qcap2_demuxer_t* pThis) {
    if (!pThis) return QCAP_RS_ERROR_INVALID_PARAMETER;
    qcap2_demuxer_priv_t* priv = reinterpret_cast<qcap2_demuxer_priv_t*>(pThis);

    if (priv->type == QCAP2_DEMUXER_TYPE_MOCK) {
        return demuxer_mock_do_play(priv);
    }

    if (priv->type == QCAP2_DEMUXER_TYPE_RTSP) {
        return demuxer_rtsp_do_play(priv);
    }

    if (!priv->format_context) return QCAP_RS_ERROR_INVALID_DEVICE;
    if (priv->thread_running) return QCAP_RS_SUCCESSFUL;

    priv->thread_running = true;
    priv->reader_thread = std::thread(demuxer_read_thread, priv);

    return QCAP_RS_SUCCESSFUL;
}

// ============================================================================
// API: qcap2_demuxer_stop
// ============================================================================
QRESULT qcap2_demuxer_stop(qcap2_demuxer_t* pThis) {
    if (!pThis) return QCAP_RS_ERROR_INVALID_PARAMETER;

    qcap2_demuxer_priv_t* priv = reinterpret_cast<qcap2_demuxer_priv_t*>(pThis);

    if (priv->type == QCAP2_DEMUXER_TYPE_MOCK) {
        return demuxer_mock_do_stop(priv);
    }

    if (priv->type == QCAP2_DEMUXER_TYPE_RTSP) {
        return demuxer_rtsp_do_stop(priv);
    }

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

// ============================================================================
// API: lifecycle management
// ============================================================================
qcap2_demuxer_t* qcap2_demuxer_new() {
    return reinterpret_cast<qcap2_demuxer_t*>(new (std::nothrow) qcap2_demuxer_priv_t());
}

void qcap2_demuxer_delete(qcap2_demuxer_t* pThis) {
    if (pThis) {
        qcap2_demuxer_stop(pThis); // ensure threads are stopped
        delete reinterpret_cast<qcap2_demuxer_priv_t*>(pThis);
    }
}

// ============================================================================
// API: configuration setters
// ============================================================================
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

// ============================================================================
// API: RTSP-specific configuration
// ============================================================================
void qcap2_demuxer_set_rtsp_timeout(qcap2_demuxer_t* pThis, int nTimeoutMs) {
    if (!pThis) return;
    qcap2_demuxer_priv_t* priv = reinterpret_cast<qcap2_demuxer_priv_t*>(pThis);
    if (priv->type != QCAP2_DEMUXER_TYPE_RTSP) return;
    if (!priv->rtsp) priv->rtsp = new qcap2_demuxer_rtsp_priv_t();
    priv->rtsp->timeout_ms = nTimeoutMs;
}

void qcap2_demuxer_set_rtsp_reconnect(qcap2_demuxer_t* pThis, int nMaxAttempts, int nDelayMs) {
    if (!pThis) return;
    qcap2_demuxer_priv_t* priv = reinterpret_cast<qcap2_demuxer_priv_t*>(pThis);
    if (priv->type != QCAP2_DEMUXER_TYPE_RTSP) return;
    if (!priv->rtsp) priv->rtsp = new qcap2_demuxer_rtsp_priv_t();
    priv->rtsp->max_reconnect_attempts = nMaxAttempts;
    if (nDelayMs > 0) priv->rtsp->reconnect_delay_ms = nDelayMs;
}

void qcap2_demuxer_set_rtsp_user_agent(qcap2_demuxer_t* pThis, const char* strUserAgent) {
    if (!pThis || !strUserAgent) return;
    qcap2_demuxer_priv_t* priv = reinterpret_cast<qcap2_demuxer_priv_t*>(pThis);
    if (priv->type != QCAP2_DEMUXER_TYPE_RTSP) return;
    if (!priv->rtsp) priv->rtsp = new qcap2_demuxer_rtsp_priv_t();
    priv->rtsp->user_agent = strUserAgent;
    priv->rtsp->user_agent_set = true;
}

void qcap2_demuxer_set_rtsp_keep_alive(qcap2_demuxer_t* pThis, int nIntervalMs) {
    if (!pThis) return;
    qcap2_demuxer_priv_t* priv = reinterpret_cast<qcap2_demuxer_priv_t*>(pThis);
    if (priv->type != QCAP2_DEMUXER_TYPE_RTSP) return;
    if (!priv->rtsp) priv->rtsp = new qcap2_demuxer_rtsp_priv_t();
    priv->rtsp->keep_alive_interval_ms = nIntervalMs;
}

// ============================================================================
// API: push (not supported)
// ============================================================================
QRESULT qcap2_demuxer_push(qcap2_demuxer_t* pThis, qcap2_rcbuffer_t* pRCBuffer) {
    (void)pThis; (void)pRCBuffer;
    return QCAP_RS_ERROR_NON_SUPPORT;
}

// ============================================================================
// API: getters
// ============================================================================
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
    if (!pThis) return QCAP_RS_ERROR_INVALID_PARAMETER;
    qcap2_demuxer_priv_t* priv = reinterpret_cast<qcap2_demuxer_priv_t*>(pThis);

    if (priv->type == QCAP2_DEMUXER_TYPE_MOCK && priv->mock) {
        std::lock_guard<std::mutex> lock(priv->mock->mtx);

        // Update Video Encoder
        if (!priv->video_encoders.empty()) {
            auto venc = priv->video_encoders[0];
            auto venc_priv = reinterpret_cast<qcap2_video_encoder_priv_t*>(venc);
            if (venc_priv->enc_prop) {
                qcap2_video_encoder_property_set_resolution(venc_priv->enc_prop, priv->mock->video_width, priv->mock->video_height);

            }
        }

        // Update Audio Encoder
        if (!priv->audio_encoders.empty()) {
            auto aenc = priv->audio_encoders[0];
            auto aenc_priv = reinterpret_cast<qcap2_audio_encoder_priv_t*>(aenc);
            if (aenc_priv->property) {
                qcap2_audio_encoder_property_set_property(aenc_priv->property, 0, QCAP_ENCODER_FORMAT_AAC, 2, 16, priv->mock->audio_samplerate);
            }
        }
    }

    return QCAP_RS_SUCCESSFUL;
}



// ============================================================================
// Mock Demuxer Backend
// ============================================================================
static void demuxer_mock_read_thread(qcap2_demuxer_priv_t* priv) {
    while (priv->thread_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(40)); // ~25fps

        if (!priv->thread_running) break;

        std::unique_lock<std::mutex> lock(priv->mock->mtx);
        priv->mock->frame_count++;

        // Simulate a format change every 10 frames
        if (priv->mock->frame_count % 10 == 0) {
            priv->mock->format_version++;
            if (priv->mock->format_version % 2 == 1) {
                priv->mock->video_width = 1280;
                priv->mock->video_height = 720;
                priv->mock->audio_samplerate = 48000;
            } else {
                priv->mock->video_width = 640;
                priv->mock->video_height = 480;
                priv->mock->audio_samplerate = 44100;
            }

            lock.unlock();

            // Notify event handlers that format has changed
            if (priv->event) {
                qcap2_event_notify(priv->event);
            }

            // Wait for app to call update() or we might just pause pushing slightly
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue; // Skip pushing this time to simulate change pause
        }
        lock.unlock();

        // Push dummy video packet (H264)
        if (priv->video_sources.size() > 0 && priv->video_encoders.size() > 0) {
            qcap2_av_packet_t* v_pkt = new qcap2_av_packet_t;
            qcap2_av_packet_init(v_pkt);
            uint8_t dummy_v_payload[] = { 0x00, 0x00, 0x00, 0x01, 0x65, 0x88, 0x84 };
            if (qcap2_av_packet_alloc_buffer(v_pkt, sizeof(dummy_v_payload))) {
                uint8_t* pBuf = nullptr;
                int nSize = 0;
                qcap2_av_packet_get_buffer(v_pkt, &pBuf, &nSize);
                if (pBuf) memcpy(pBuf, dummy_v_payload, sizeof(dummy_v_payload));
                qcap2_av_packet_set_pts(v_pkt, priv->mock->frame_count * 40000);
                qcap2_av_packet_set_dts(v_pkt, priv->mock->frame_count * 40000);
                qcap2_av_packet_set_sample_time(v_pkt, priv->mock->frame_count * 0.04);
                qcap2_av_packet_set_property(v_pkt, 0, TRUE); // keyframe
            }
            qcap2_rcbuffer_t* enc_rcbuf = qcap2_rcbuffer_new(v_pkt, [](PVOID pData) {
                qcap2_av_packet_t* p = (qcap2_av_packet_t*)pData;
                if (p) {
                    qcap2_av_packet_free_buffer(p);
                    delete p;
                }
            });

            auto vs = priv->video_sources[0];
            qcap2_video_source_push(vs, enc_rcbuf);

            auto venc = priv->video_encoders[0];
            auto venc_priv = reinterpret_cast<qcap2_video_encoder_priv_t*>(venc);
            std::lock_guard<std::mutex> elock(*(venc_priv->mtx));
            if (venc_priv->running) {
                qcap2_rcbuffer_add_ref(enc_rcbuf);
                venc_priv->output_queue.push(enc_rcbuf);
                venc_priv->cv->notify_all();
                if (venc_priv->event) qcap2_event_notify(venc_priv->event);
            }
            qcap2_rcbuffer_release(enc_rcbuf);
        }

        // Push dummy audio packet (AAC)
        if (priv->audio_sources.size() > 0 && priv->audio_encoders.size() > 0) {
            qcap2_av_packet_t* a_pkt = new qcap2_av_packet_t;
            qcap2_av_packet_init(a_pkt);
            uint8_t dummy_a_payload[] = { 0xff, 0xf1, 0x4c, 0x80 }; // ADTS header
            if (qcap2_av_packet_alloc_buffer(a_pkt, sizeof(dummy_a_payload))) {
                uint8_t* pBuf = nullptr;
                int nSize = 0;
                qcap2_av_packet_get_buffer(a_pkt, &pBuf, &nSize);
                if (pBuf) memcpy(pBuf, dummy_a_payload, sizeof(dummy_a_payload));
                qcap2_av_packet_set_pts(a_pkt, priv->mock->frame_count * 1024);
                qcap2_av_packet_set_dts(a_pkt, priv->mock->frame_count * 1024);
                qcap2_av_packet_set_sample_time(a_pkt, priv->mock->frame_count * (1024.0 / 44100.0));
                qcap2_av_packet_set_property(a_pkt, 1, TRUE);
            }
            qcap2_rcbuffer_t* enc_rcbuf = qcap2_rcbuffer_new(a_pkt, [](PVOID pData) {
                qcap2_av_packet_t* p = (qcap2_av_packet_t*)pData;
                if (p) {
                    qcap2_av_packet_free_buffer(p);
                    delete p;
                }
            });

            auto as = priv->audio_sources[0];
            qcap2_audio_source_push(as, enc_rcbuf);

            auto aenc = priv->audio_encoders[0];
            auto aenc_priv = reinterpret_cast<qcap2_audio_encoder_priv_t*>(aenc);
            std::lock_guard<std::mutex> elock(*(aenc_priv->mtx));
            if (aenc_priv->running) {
                qcap2_rcbuffer_add_ref(enc_rcbuf);
                aenc_priv->output_queue.push(enc_rcbuf);
                aenc_priv->cv->notify_all();
                if (aenc_priv->event) qcap2_event_notify(aenc_priv->event);
            }
            qcap2_rcbuffer_release(enc_rcbuf);
        }
    }

    // Signal EOF
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

static QRESULT demuxer_mock_do_start(qcap2_demuxer_priv_t* priv) {
    if (!priv->mock) {
        priv->mock = new qcap2_demuxer_mock_priv_t();
    }

    // Create initial sources and programs directly
    priv->cleanup_sources();

    // Video Source
    qcap2_video_source_t* vs = qcap2_video_source_new();
    qcap2_video_source_set_stream_index(vs, 0);
    priv->video_sources.push_back(vs);

    qcap2_video_encoder_t* venc = qcap2_video_encoder_new();
    auto venc_priv = reinterpret_cast<qcap2_video_encoder_priv_t*>(venc);
    venc_priv->running = true;
    venc_priv->enc_prop = qcap2_video_encoder_property_new();
    qcap2_video_encoder_property_set_format(venc_priv->enc_prop, QCAP_ENCODER_FORMAT_H264);
    qcap2_video_encoder_property_set_resolution(venc_priv->enc_prop, priv->mock->video_width, priv->mock->video_height);



    priv->video_encoders.push_back(venc);

    // Audio Source
    qcap2_audio_source_t* as = qcap2_audio_source_new();
    reinterpret_cast<qcap2_audio_source_priv_t*>(as)->stream_index = 1;
    priv->audio_sources.push_back(as);

    qcap2_audio_encoder_t* aenc = qcap2_audio_encoder_new();
    auto aenc_priv = reinterpret_cast<qcap2_audio_encoder_priv_t*>(aenc);
    aenc_priv->running = true;
    aenc_priv->property = qcap2_audio_encoder_property_new();

    qcap2_audio_encoder_property_set_property(aenc_priv->property, 0, QCAP_ENCODER_FORMAT_AAC, 2, 16, priv->mock->audio_samplerate);

    priv->audio_encoders.push_back(aenc);

    // Program
    qcap2_program_info_t* prog = qcap2_program_info_new();
    qcap2_program_info_set_id(prog, 1);
    qcap2_program_info_set_number(prog, 1);
    qcap2_program_info_set_video_source_count(prog, 1);
    qcap2_program_info_set_video_source_index(prog, 0, 0);
    qcap2_program_info_set_video_encoder_count(prog, 1);
    qcap2_program_info_set_video_encoder_index(prog, 0, 0);
    qcap2_program_info_set_audio_source_count(prog, 1);
    qcap2_program_info_set_audio_source_index(prog, 0, 1);
    qcap2_program_info_set_audio_encoder_count(prog, 1);
    qcap2_program_info_set_audio_encoder_index(prog, 0, 1);
    priv->programs.push_back(prog);

    return QCAP_RS_SUCCESSFUL;
}

static QRESULT demuxer_mock_do_play(qcap2_demuxer_priv_t* priv) {
    if (priv->thread_running) return QCAP_RS_SUCCESSFUL;
    priv->thread_running = true;
    priv->reader_thread = std::thread(demuxer_mock_read_thread, priv);
    return QCAP_RS_SUCCESSFUL;
}

static QRESULT demuxer_mock_do_stop(qcap2_demuxer_priv_t* priv) {
    if (priv->thread_running) {
        priv->thread_running = false;
        if (priv->reader_thread.joinable()) {
            priv->reader_thread.join();
        }
    }
    priv->cleanup_sources();
    return QCAP_RS_SUCCESSFUL;
}
