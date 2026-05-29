#ifndef __QCAP2_DEVICES_H__
#define __QCAP2_DEVICES_H__

#include "qcap2.types.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

// qcap2_qdev_enum_t
qcap2_qdev_enum_t* qcap2_qdev_enum_new();
void qcap2_qdev_enum_delete(qcap2_qdev_enum_t* pThis);
void qcap2_qdev_enum_set_type(qcap2_qdev_enum_t* pThis, int nType);
void qcap2_qdev_enum_set_event(qcap2_qdev_enum_t* pThis, qcap2_event_t* pEvent);
QRESULT qcap2_qdev_enum_start(qcap2_qdev_enum_t* pThis);
QRESULT qcap2_qdev_enum_stop(qcap2_qdev_enum_t* pThis);
QRESULT qcap2_qdev_enum_pop(qcap2_qdev_enum_t* pThis, qcap2_rcbuffer_t** ppQdevInfo); // qcap2_qdev_info_t

// qcap2_qdev_info_t
qcap2_qdev_info_t* qcap2_qdev_info_lock_from(qcap2_rcbuffer_t* pQdevInfo);
int qcap2_qdev_info_get_type(qcap2_qdev_info_t* pThis);
uint32_t qcap2_qdev_info_get_uid(qcap2_qdev_info_t* pThis);
BOOL qcap2_qdev_info_get_plugged(qcap2_qdev_info_t* pThis);
const char* qcap2_qdev_info_get_path(qcap2_qdev_info_t* pThis);
int qcap2_qdev_info_get_vendor_id(qcap2_qdev_info_t* pThis);
int qcap2_qdev_info_get_device_id(qcap2_qdev_info_t* pThis);

// qcap2_qdev_t
qcap2_qdev_t* qcap2_qdev_new();
void qcap2_qdev_delete(qcap2_qdev_t* pThis);
void qcap2_qdev_set_event(qcap2_qdev_t* pThis, qcap2_event_t* pEvent);
void qcap2_qdev_set_poll_duration(qcap2_qdev_t* pThis, int nPollDuration);
void qcap2_qdev_set_info(qcap2_qdev_t* pThis, qcap2_rcbuffer_t* pQdevInfo); // qcap2_qdev_info_t
QRESULT qcap2_qdev_start(qcap2_qdev_t* pThis);
QRESULT qcap2_qdev_stop(qcap2_qdev_t* pThis);
QRESULT qcap2_qdev_get_video_count(qcap2_qdev_t* pThis, int* pCount);
QRESULT qcap2_qdev_get_audio_count(qcap2_qdev_t* pThis, int* pCount);
QRESULT qcap2_qdev_get_video_encoder_count(qcap2_qdev_t* pThis, int* pCount);
QRESULT qcap2_qdev_get_audio_encoder_count(qcap2_qdev_t* pThis, int* pCount);
QRESULT qcap2_qdev_get_video_input(qcap2_qdev_t* pThis, int nIndex, ULONG* pVideoInput);
QRESULT qcap2_qdev_set_video_input(qcap2_qdev_t* pThis, int nIndex, ULONG nVideoInput);
QRESULT qcap2_qdev_get_audio_input(qcap2_qdev_t* pThis, int nIndex, ULONG* pAudioInput);
QRESULT qcap2_qdev_set_audio_input(qcap2_qdev_t* pThis, int nIndex, ULONG nAudioInput);
QRESULT qcap2_qdev_config_video_source(qcap2_qdev_t* pThis, int nIndex, qcap2_video_source_t* pVideoSource);
QRESULT qcap2_qdev_pop(qcap2_qdev_t* pThis, qcap2_rcbuffer_t** ppMediaInfo); // qcap2_media_info_t

// qcap2_video_source_t
qcap2_video_source_t* qcap2_video_source_new();
void qcap2_video_source_delete(qcap2_video_source_t* pThis);
void qcap2_video_source_set_backend_type(qcap2_video_source_t* pThis, int nBackendType);
void qcap2_video_source_set_frame_count(qcap2_video_source_t* pThis, int nFrameCount);
void qcap2_video_source_set_event(qcap2_video_source_t* pThis, qcap2_event_t* pEvent);
void qcap2_video_source_set_auto_run(qcap2_video_source_t* pThis, bool bAutoRun);
void qcap2_video_source_set_video_format(qcap2_video_source_t* pThis, qcap2_video_format_t* pVideoFormat);
void qcap2_video_source_get_video_format(qcap2_video_source_t* pThis, qcap2_video_format_t* pVideoFormat);
void qcap2_video_source_set_buffers(qcap2_video_source_t* pThis, qcap2_rcbuffer_t** pBuffers);
void qcap2_video_source_set_device_index(qcap2_video_source_t* pThis, int nDeviceIndex);
void qcap2_video_source_set_stream_index(qcap2_video_source_t* pThis, int nStreamIndex);
void qcap2_video_source_set_src_ss_type(qcap2_video_source_t* pThis, int nSrcSSType); // refer to qcap2_stereoscopic_type_t
void qcap2_video_source_set_dst_ss_type(qcap2_video_source_t* pThis, int nDstSSType); // refer to qcap2_stereoscopic_type_t
QRESULT qcap2_video_source_start(qcap2_video_source_t* pThis);
QRESULT qcap2_video_source_stop(qcap2_video_source_t* pThis);
QRESULT qcap2_video_source_run(qcap2_video_source_t* pThis);
QRESULT qcap2_video_source_pop(qcap2_video_source_t* pThis, qcap2_rcbuffer_t** ppRCBuffer);
QRESULT qcap2_video_source_push(qcap2_video_source_t* pThis, qcap2_rcbuffer_t* pRCBuffer);

// qcap2_video_sink_t
qcap2_video_sink_t* qcap2_video_sink_new();
void qcap2_video_sink_delete(qcap2_video_sink_t* pThis);
void qcap2_video_sink_set_backend_type(qcap2_video_sink_t* pThis, int nBackendType);
void qcap2_video_sink_set_video_format(qcap2_video_sink_t* pThis, qcap2_video_format_t* pVideoFormat);
void qcap2_video_sink_set_frame_count(qcap2_video_sink_t* pThis, int nFrameCount);
void qcap2_video_sink_set_multithread(qcap2_video_sink_t* pThis, bool bMultiThread);
void qcap2_video_sink_set_native_handle(qcap2_video_sink_t* pThis, uintptr_t nHandle); // window handle
void qcap2_video_sink_set_low_bandwidth(qcap2_video_sink_t* pThis, bool bLowBandwidth);
void qcap2_video_sink_set_display_system(qcap2_video_sink_t* pThis, int nDisplaySystem);
void qcap2_video_sink_set_graphic_window_system(qcap2_video_sink_t* pThis, int nGraphicWindowSystem);
void qcap2_video_sink_set_gpu_direct(qcap2_video_sink_t* pThis, bool bGPUDirect);
void qcap2_video_sink_set_scale_style(qcap2_video_sink_t* pThis, ULONG nScaleStyle); // refer to QCAP_SCALE_STYLE_*
void qcap2_video_sink_set_device_index(qcap2_video_sink_t* pThis, int nDeviceIndex);
void qcap2_video_sink_set_src_ss_type(qcap2_video_sink_t* pThis, int nSrcSSType); // refer to qcap2_stereoscopic_type_t
void qcap2_video_sink_set_dst_ss_type(qcap2_video_sink_t* pThis, int nDstSSType); // refer to qcap2_stereoscopic_type_t
QRESULT qcap2_video_sink_start(qcap2_video_sink_t* pThis);
QRESULT qcap2_video_sink_stop(qcap2_video_sink_t* pThis);
QRESULT qcap2_video_sink_pop(qcap2_video_source_t* pThis, qcap2_rcbuffer_t** ppRCBuffer);
QRESULT qcap2_video_sink_push(qcap2_video_sink_t* pThis, qcap2_rcbuffer_t* pRCBuffer);

// qcap2_audio_source_t
qcap2_audio_source_t* qcap2_audio_source_new();
void qcap2_audio_source_delete(qcap2_audio_source_t* pThis);
void qcap2_audio_source_set_backend_type(qcap2_audio_source_t* pThis, int nBackendType);
void qcap2_audio_source_set_frame_count(qcap2_audio_source_t* pThis, int nFrameCount);
void qcap2_audio_source_set_event(qcap2_audio_source_t* pThis, qcap2_event_t* pEvent);
void qcap2_audio_source_set_audio_format(qcap2_audio_source_t* pThis, qcap2_audio_format_t* pAudioFormat);
void qcap2_audio_source_get_audio_format(qcap2_audio_source_t* pThis, qcap2_audio_format_t* pAudioFormat);
void qcap2_audio_source_set_buffers(qcap2_audio_source_t* pThis, qcap2_rcbuffer_t** pBuffers);
void qcap2_audio_source_set_period_time(qcap2_audio_source_t* pThis, int nPeriodTime);
void qcap2_audio_source_set_buffer_time(qcap2_audio_source_t* pThis, int nBufferTime);
void qcap2_audio_source_set_ideal_timer(qcap2_audio_source_t* pThis, bool bIdealTimer);
void qcap2_audio_source_set_card(qcap2_audio_source_t* pThis, int nCard);
void qcap2_audio_source_set_device(qcap2_audio_source_t* pThis, int nDevice);
QRESULT qcap2_audio_source_start(qcap2_audio_source_t* pThis);
QRESULT qcap2_audio_source_stop(qcap2_audio_source_t* pThis);
QRESULT qcap2_audio_source_pop(qcap2_audio_source_t* pThis, qcap2_rcbuffer_t** ppRCBuffer);
QRESULT qcap2_audio_source_push(qcap2_audio_source_t* pThis, qcap2_rcbuffer_t* pRCBuffer);

// qcap2_audio_sink_t
qcap2_audio_sink_t* qcap2_audio_sink_new();
void qcap2_audio_sink_delete(qcap2_audio_sink_t* pThis);
void qcap2_audio_sink_set_backend_type(qcap2_audio_sink_t* pThis, int nBackendType);
void qcap2_audio_sink_set_audio_format(qcap2_audio_sink_t* pThis, qcap2_audio_format_t* pAudioFormat);
void qcap2_audio_sink_set_period_time(qcap2_audio_sink_t* pThis, int nPeriodTime);
void qcap2_audio_sink_set_buffer_time(qcap2_audio_sink_t* pThis, int nBufferTime);
void qcap2_audio_sink_set_card(qcap2_audio_sink_t* pThis, int nCard);
void qcap2_audio_sink_set_device(qcap2_audio_sink_t* pThis, int nDevice);
QRESULT qcap2_audio_sink_start(qcap2_audio_sink_t* pThis);
QRESULT qcap2_audio_sink_stop(qcap2_audio_sink_t* pThis);
QRESULT qcap2_audio_sink_pop(qcap2_audio_sink_t* pThis, qcap2_rcbuffer_t** ppRCBuffer);
QRESULT qcap2_audio_sink_push(qcap2_audio_sink_t* pThis, qcap2_rcbuffer_t* pRCBuffer);

// qcap2_demuxer_t
qcap2_demuxer_t* qcap2_demuxer_new();
void qcap2_demuxer_delete(qcap2_demuxer_t* pThis);
void qcap2_demuxer_set_type(qcap2_demuxer_t* pThis, int nType);
void qcap2_demuxer_set_event(qcap2_demuxer_t* pThis, qcap2_event_t* pEvent);
void qcap2_demuxer_set_url(qcap2_demuxer_t* pThis, const char* strURL);
void qcap2_demuxer_set_max_buffer_length(qcap2_demuxer_t* pThis, int nMaxBufferLength); // in milliseconds
void qcap2_demuxer_set_find_stream_info(qcap2_demuxer_t* pThis, bool bFindStreamInfo);
void qcap2_demuxer_set_push_source(qcap2_demuxer_t* pThis, bool bPushSource);
void qcap2_demuxer_set_live_source(qcap2_demuxer_t* pThis, bool bLiveSource);
void qcap2_demuxer_set_format_name(qcap2_demuxer_t* pThis, const char* strFormatName); // refer to `ffprobe -demuxers`
void qcap2_demuxer_set_buffer_size(qcap2_demuxer_t* pThis, int nBufferSize); // in bytes for push-source
void qcap2_demuxer_set_sdp_lines(qcap2_demuxer_t* pThis, const char* strSdpLines);
void qcap2_demuxer_set_tcp(qcap2_demuxer_t* pThis, bool bUseTCP);
void qcap2_demuxer_set_multicast(qcap2_demuxer_t* pThis, bool bMulticast);
QRESULT qcap2_demuxer_start(qcap2_demuxer_t* pThis);
QRESULT qcap2_demuxer_stop(qcap2_demuxer_t* pThis);
QRESULT qcap2_demuxer_push(qcap2_demuxer_t* pThis, qcap2_rcbuffer_t* pRCBuffer); // for push-source
int qcap2_demuxer_get_video_source_count(qcap2_demuxer_t* pThis);
int qcap2_demuxer_get_audio_source_count(qcap2_demuxer_t* pThis);
int qcap2_demuxer_get_video_encoder_count(qcap2_demuxer_t* pThis);
int qcap2_demuxer_get_audio_encoder_count(qcap2_demuxer_t* pThis);
int qcap2_demuxer_get_program_count(qcap2_demuxer_t* pThis);
qcap2_video_source_t* qcap2_demuxer_get_video_source(qcap2_demuxer_t* pThis, int nIndex);
qcap2_audio_source_t* qcap2_demuxer_get_audio_source(qcap2_demuxer_t* pThis, int nIndex);
qcap2_video_encoder_t* qcap2_demuxer_get_video_encoder(qcap2_demuxer_t* pThis, int nIndex);
qcap2_audio_encoder_t* qcap2_demuxer_get_audio_encoder(qcap2_demuxer_t* pThis, int nIndex);
qcap2_program_info_t* qcap2_demuxer_get_program_info(qcap2_demuxer_t* pThis, int nIndex);
QRESULT qcap2_demuxer_play(qcap2_demuxer_t* pThis);
QRESULT qcap2_demuxer_update(qcap2_demuxer_t* pThis); // to sync status (progs, vsrc, asrc, venc, aenc)

// qcap2_muxer_t
qcap2_muxer_t* qcap2_muxer_new();
void qcap2_muxer_delete(qcap2_muxer_t* pThis);
void qcap2_muxer_set_type(qcap2_muxer_t* pThis, int nType);
void qcap2_muxer_set_max_threads(qcap2_muxer_t* pThis, int nMaxThreads);
void qcap2_muxer_set_endpoint(qcap2_muxer_t* pThis, const char* strIP, int nPort);
void qcap2_muxer_set_realm(qcap2_muxer_t* pThis, const char* strRealm);
void qcap2_muxer_set_ssl(qcap2_muxer_t* pThis, bool bSSL);
void qcap2_muxer_set_certificate_chain_file(qcap2_muxer_t* pThis, const char* strCertificateChainFile);
void qcap2_muxer_set_private_key_file(qcap2_muxer_t* pThis, const char* strPrivateKeyFile);
void qcap2_muxer_set_realm(qcap2_muxer_t* pThis, const char* strRealm);
void qcap2_muxer_add_user(qcap2_muxer_t* pThis, const char* strAccount, const char* strPassword);
void qcap2_muxer_add_program_info(qcap2_muxer_t* pThis, qcap2_program_info_t* pProgramInfo);
QRESULT qcap2_muxer_start(qcap2_muxer_t* pThis);
QRESULT qcap2_muxer_stop(qcap2_muxer_t* pThis);
int qcap2_muxer_get_video_sink_count(qcap2_muxer_t* pThis);
int qcap2_muxer_get_audio_sink_count(qcap2_muxer_t* pThis);
int qcap2_muxer_get_video_decoder_count(qcap2_muxer_t* pThis);
int qcap2_muxer_get_audio_decoder_count(qcap2_muxer_t* pThis);
int qcap2_muxer_get_program_count(qcap2_muxer_t* pThis);
qcap2_program_info_t* qcap2_muxer_get_program_info(qcap2_muxer_t* pThis, int nIndex);
qcap2_video_sink_t* qcap2_muxer_get_video_sink(qcap2_muxer_t* pThis, int nIndex);
qcap2_audio_sink_t* qcap2_muxer_get_audio_sink(qcap2_muxer_t* pThis, int nIndex);
qcap2_video_decoder_t* qcap2_muxer_get_video_decoder(qcap2_muxer_t* pThis, int nIndex);
qcap2_audio_decoder_t* qcap2_muxer_get_audio_decoder(qcap2_muxer_t* pThis, int nIndex);
QRESULT qcap2_muxer_play(qcap2_muxer_t* pThis);

// qcap2_dns_source_t
qcap2_dns_source_t* qcap2_dns_source_new();
void qcap2_dns_source_delete(qcap2_dns_source_t* pThis);
void qcap2_dns_source_set_event(qcap2_dns_source_t* pThis, qcap2_event_t* pEvent);
void qcap2_dns_source_set_listen_address(qcap2_dns_source_t* pThis, const char* strListenAddress);
void qcap2_dns_source_set_multicast_address(qcap2_dns_source_t* pThis, const char* strMulticastAddress);
void qcap2_dns_source_set_port(qcap2_dns_source_t* pThis, int nPort);
void qcap2_dns_source_set_polling_time(qcap2_dns_source_t* pThis, int nPollingTime); // in seconds
void qcap2_dns_source_set_host_name(qcap2_dns_source_t* pThis, const char* strHostName);
QRESULT qcap2_dns_source_start(qcap2_dns_source_t* pThis);
QRESULT qcap2_dns_source_stop(qcap2_dns_source_t* pThis);
QRESULT qcap2_dns_source_pop(qcap2_dns_source_t* pThis, qcap2_rcbuffer_t** ppRCBuffer); // refer to qcap2_dns_event_t

// qcap2_clock_source_t
qcap2_clock_source_t* qcap2_clock_source_new();
void qcap2_clock_source_delete(qcap2_clock_source_t* pThis);
void qcap2_clock_source_set_event(qcap2_clock_source_t* pThis, qcap2_event_t* pEvent);
void qcap2_clock_source_set_listen_address(qcap2_clock_source_t* pThis, const char* strListenAddress);
void qcap2_clock_source_set_server_address(qcap2_clock_source_t* pThis, const char* strServerAddress);
void qcap2_clock_source_set_port(qcap2_clock_source_t* pThis, int nPort);
void qcap2_clock_source_set_polling_time(qcap2_clock_source_t* pThis, int nPollingTime); // in seconds
void qcap2_clock_source_set_resync_range(qcap2_clock_source_t* pThis, int nResyncMin, int nResyncMax); // in milliseconds
QRESULT qcap2_clock_source_start(qcap2_clock_source_t* pThis);
QRESULT qcap2_clock_source_stop(qcap2_clock_source_t* pThis);
QRESULT qcap2_clock_source_pop(qcap2_clock_source_t* pThis, qcap2_rcbuffer_t** ppRCBuffer); // refer to qcap2_clock_event_t

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif // __QCAP2_DEVICES_H__
