#ifndef __QCAP2_PROCESSING_H__
#define __QCAP2_PROCESSING_H__

#include "qcap2.types.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

// qcap2_video_scaler_t
qcap2_video_scaler_t* qcap2_video_scaler_new();
void qcap2_video_scaler_delete(qcap2_video_scaler_t* pThis);
void qcap2_video_scaler_set_video_format(qcap2_video_scaler_t* pThis, qcap2_video_format_t* pVideoFormat);
void qcap2_video_scaler_get_video_format(qcap2_video_scaler_t* pThis, qcap2_video_format_t* pVideoFormat);
void qcap2_video_scaler_set_backend_type(qcap2_video_scaler_t* pThis, int nBackendType);
void qcap2_video_scaler_set_frame_count(qcap2_video_scaler_t* pThis, int nFrameCount);
void qcap2_video_scaler_set_frame_align(qcap2_video_scaler_t* pThis, int nFrameAlign);
void qcap2_video_scaler_set_frame_valign(qcap2_video_scaler_t* pThis, int nFrameVAlign);
void qcap2_video_scaler_set_multithread(qcap2_video_scaler_t* pThis, bool bMultiThread);
void qcap2_video_scaler_set_auto_run(qcap2_video_scaler_t* pThis, bool bAutoRun);
void qcap2_video_scaler_set_event(qcap2_video_scaler_t* pThis, qcap2_event_t* pEvent);
void qcap2_video_scaler_set_buffers(qcap2_video_scaler_t* pThis, qcap2_rcbuffer_t** pBuffers);
void qcap2_video_scaler_set_src_color_space(qcap2_video_scaler_t* pThis, int nSrcColorSpace); // refer to qcap2_color_space_t
void qcap2_video_scaler_set_dst_color_range(qcap2_video_scaler_t* pThis, int nDstColorRange); // refer to qcap2_color_range_t
void qcap2_video_scaler_set_src_ss_type(qcap2_video_scaler_t* pThis, int nSrcSSType); // refer to qcap2_stereoscopic_type_t
void qcap2_video_scaler_set_dst_ss_type(qcap2_video_scaler_t* pThis, int nDstSSType); // refer to qcap2_stereoscopic_type_t
void qcap2_video_scaler_set_crop(qcap2_video_scaler_t* pThis, int x, int y, int w, int h); // to crop on source video frame
void qcap2_video_scaler_set_src_buffer_hint(qcap2_video_scaler_t* pThis, int nSrcBufferHint); // refer to qcap2_buffer_hint_t
void qcap2_video_scaler_set_dst_buffer_hint(qcap2_video_scaler_t* pThis, int nDstBufferHint); // refer to qcap2_buffer_hint_t
void qcap2_video_scaler_set_filter_graph(qcap2_video_scaler_t* pThis, const char* strFilterGraph); // refer to https://ffmpeg.org/ffmpeg-filters.html
QRESULT qcap2_video_scaler_start(qcap2_video_scaler_t* pThis);
QRESULT qcap2_video_scaler_stop(qcap2_video_scaler_t* pThis);
QRESULT qcap2_video_scaler_run(qcap2_video_scaler_t* pThis);
QRESULT qcap2_video_scaler_push(qcap2_video_scaler_t* pThis, qcap2_rcbuffer_t* pRCBuffer);
QRESULT qcap2_video_scaler_pop(qcap2_video_scaler_t* pThis, qcap2_rcbuffer_t** ppRCBuffer);
QRESULT qcap2_video_scaler_pop_input(qcap2_video_scaler_t* pThis, qcap2_rcbuffer_t** ppRCBuffer);
QRESULT qcap2_video_scaler_push_output(qcap2_video_scaler_t* pThis, qcap2_rcbuffer_t* pRCBuffer);

// qcap2_audio_resampler_t
qcap2_audio_resampler_t* qcap2_audio_resampler_new();
void qcap2_audio_resampler_delete(qcap2_audio_resampler_t* pThis);
void qcap2_audio_resampler_set_audio_property(qcap2_audio_resampler_t* pThis, ULONG nChannels, ULONG nSampleFmt, ULONG nSampleFrequency, ULONG nFrameSize);
void qcap2_audio_resampler_set_frame_count(qcap2_audio_resampler_t* pThis, int nFrameCount);
void qcap2_audio_resampler_set_frame_align(qcap2_audio_resampler_t* pThis, int nFrameAlign);
void qcap2_audio_resampler_set_multithread(qcap2_audio_resampler_t* pThis, bool bMultiThread);
void qcap2_audio_resampler_set_event(qcap2_audio_resampler_t* pThis, qcap2_event_t* pEvent);
QRESULT qcap2_audio_resampler_start(qcap2_audio_resampler_t* pThis);
QRESULT qcap2_audio_resampler_stop(qcap2_audio_resampler_t* pThis);
QRESULT qcap2_audio_resampler_push(qcap2_audio_resampler_t* pThis, qcap2_rcbuffer_t* pRCBuffer);
QRESULT qcap2_audio_resampler_pop(qcap2_audio_resampler_t* pThis, qcap2_rcbuffer_t** ppRCBuffer);
QRESULT qcap2_audio_resampler_pop_input(qcap2_audio_resampler_t* pThis, qcap2_rcbuffer_t** ppRCBuffer);
QRESULT qcap2_audio_resampler_push_output(qcap2_audio_resampler_t* pThis, qcap2_rcbuffer_t* pRCBuffer);

// qcap2_video_encoder_t
qcap2_video_encoder_t* qcap2_video_encoder_new();
void qcap2_video_encoder_delete(qcap2_video_encoder_t* pThis);
void qcap2_video_encoder_set_video_property(qcap2_video_encoder_t* pThis, qcap2_video_encoder_property_t* pVideoEncoderProperty);
void qcap2_video_encoder_get_video_property(qcap2_video_encoder_t* pThis, qcap2_video_encoder_property_t* pVideoEncoderProperty);
void qcap2_video_encoder_set_dynamic_video_property(qcap2_video_encoder_t* pThis, qcap2_video_encoder_dynamic_property_t* pVideoEncoderDynamicProperty);
void qcap2_video_encoder_get_dynamic_video_property(qcap2_video_encoder_t* pThis, qcap2_video_encoder_dynamic_property_t* pVideoEncoderDynamicProperty);
void qcap2_video_encoder_get_extra_data(qcap2_video_encoder_t* pThis, uint8_t** ppExtraData, int* pExtraDataSize);
void qcap2_video_encoder_set_extra_data(qcap2_video_encoder_t* pThis, uint8_t* pExtraData, int nExtraDataSize);
void qcap2_video_encoder_set_frame_count(qcap2_video_encoder_t* pThis, int nFrameCount);
void qcap2_video_encoder_set_frame_align(qcap2_video_encoder_t* pThis, int nFrameAlign);
void qcap2_video_encoder_set_frame_valign(qcap2_video_encoder_t* pThis, int nFrameVAlign);
void qcap2_video_encoder_set_packet_count(qcap2_video_encoder_t* pThis, int nPacketCount);
void qcap2_video_encoder_set_max_packet_size(qcap2_video_encoder_t* pThis, int nMaxPacketSize);
void qcap2_video_encoder_set_multithread(qcap2_video_encoder_t* pThis, bool bMultiThread);
void qcap2_video_encoder_set_event(qcap2_video_encoder_t* pThis, qcap2_event_t* pEvent);
void qcap2_video_encoder_set_num_cores(qcap2_video_encoder_t* pThis, int nNumCores);
void qcap2_video_encoder_set_native_buffer(qcap2_video_encoder_t* pThis, bool bNativeBuffer);
void qcap2_video_encoder_request_idr(qcap2_video_encoder_t* pThis);
QRESULT qcap2_video_encoder_start(qcap2_video_encoder_t* pThis);
QRESULT qcap2_video_encoder_stop(qcap2_video_encoder_t* pThis);
QRESULT qcap2_video_encoder_push(qcap2_video_encoder_t* pThis, qcap2_rcbuffer_t* pRCBuffer);
QRESULT qcap2_video_encoder_pop(qcap2_video_encoder_t* pThis, qcap2_rcbuffer_t** ppRCBuffer);
QRESULT qcap2_video_encoder_pop_input(qcap2_video_encoder_t* pThis, qcap2_rcbuffer_t** ppRCBuffer);
QRESULT qcap2_video_encoder_push_output(qcap2_video_encoder_t* pThis, qcap2_rcbuffer_t* pRCBuffer);

// qcap2_video_decoder_t
qcap2_video_decoder_t* qcap2_video_decoder_new();
void qcap2_video_decoder_delete(qcap2_video_decoder_t* pThis);
void qcap2_video_decoder_set_video_property(qcap2_video_decoder_t* pThis, qcap2_video_encoder_property_t* pVideoEncoderProperty);
void qcap2_video_decoder_get_video_property(qcap2_video_decoder_t* pThis, qcap2_video_encoder_property_t* pVideoEncoderProperty);
void qcap2_video_decoder_get_extra_data(qcap2_video_decoder_t* pThis, uint8_t** ppExtraData, int* pExtraDataSize);
void qcap2_video_decoder_set_extra_data(qcap2_video_decoder_t* pThis, uint8_t* pExtraData, int nExtraDataSize);
void qcap2_video_decoder_set_frame_count(qcap2_video_decoder_t* pThis, int nFrameCount);
void qcap2_video_decoder_set_frame_align(qcap2_video_decoder_t* pThis, int nFrameAlign);
void qcap2_video_decoder_set_frame_valign(qcap2_video_decoder_t* pThis, int nFrameVAlign);
void qcap2_video_decoder_set_packet_count(qcap2_video_decoder_t* pThis, int nPacketCount);
void qcap2_video_decoder_set_max_packet_size(qcap2_video_decoder_t* pThis, int nMaxPacketSize);
void qcap2_video_decoder_set_multithread(qcap2_video_decoder_t* pThis, bool bMultiThread);
void qcap2_video_decoder_set_event(qcap2_video_decoder_t* pThis, qcap2_event_t* pEvent);
void qcap2_video_decoder_set_buffers(qcap2_video_decoder_t* pThis, qcap2_rcbuffer_t** pBuffers);
void qcap2_video_decoder_set_payload_type(qcap2_video_decoder_t* pThis, int nPayloadType);
QRESULT qcap2_video_decoder_start(qcap2_video_decoder_t* pThis);
QRESULT qcap2_video_decoder_stop(qcap2_video_decoder_t* pThis);
QRESULT qcap2_video_decoder_push(qcap2_video_decoder_t* pThis, qcap2_rcbuffer_t* pRCBuffer);
QRESULT qcap2_video_decoder_pop(qcap2_video_decoder_t* pThis, qcap2_rcbuffer_t** ppRCBuffer);
QRESULT qcap2_video_decoder_pop_input(qcap2_video_decoder_t* pThis, qcap2_rcbuffer_t** ppRCBuffer);
QRESULT qcap2_video_decoder_push_output(qcap2_video_decoder_t* pThis, qcap2_rcbuffer_t* pRCBuffer);

// qcap2_audio_encoder_t
qcap2_audio_encoder_t* qcap2_audio_encoder_new();
void qcap2_audio_encoder_delete(qcap2_audio_encoder_t* pThis);
void qcap2_audio_encoder_set_audio_property(qcap2_audio_encoder_t* pThis, qcap2_audio_encoder_property_t* pAudioEncoderProperty);
void qcap2_audio_encoder_get_audio_property(qcap2_audio_encoder_t* pThis, qcap2_audio_encoder_property_t* pAudioEncoderProperty);
void qcap2_audio_encoder_get_extra_data(qcap2_audio_encoder_t* pThis, uint8_t** ppExtraData, int* pExtraDataSize);
void qcap2_audio_encoder_set_extra_data(qcap2_audio_encoder_t* pThis, uint8_t* pExtraData, int nExtraDataSize);
void qcap2_audio_encoder_set_frame_count(qcap2_audio_encoder_t* pThis, int nFrameCount);
void qcap2_audio_encoder_set_packet_count(qcap2_audio_encoder_t* pThis, int nPacketCount);
void qcap2_audio_encoder_set_multithread(qcap2_audio_encoder_t* pThis, bool bMultiThread);
void qcap2_audio_encoder_set_event(qcap2_audio_encoder_t* pThis, qcap2_event_t* pEvent);
QRESULT qcap2_audio_encoder_start(qcap2_audio_encoder_t* pThis);
QRESULT qcap2_audio_encoder_stop(qcap2_audio_encoder_t* pThis);
QRESULT qcap2_audio_encoder_push(qcap2_audio_encoder_t* pThis, qcap2_rcbuffer_t* pRCBuffer);
QRESULT qcap2_audio_encoder_pop(qcap2_audio_encoder_t* pThis, qcap2_rcbuffer_t** ppRCBuffer);
QRESULT qcap2_audio_encoder_pop_input(qcap2_audio_encoder_t* pThis, qcap2_rcbuffer_t** ppRCBuffer);
QRESULT qcap2_audio_encoder_push_output(qcap2_audio_encoder_t* pThis, qcap2_rcbuffer_t* pRCBuffer);

// qcap2_audio_decoder_t
qcap2_audio_decoder_t* qcap2_audio_decoder_new();
void qcap2_audio_decoder_delete(qcap2_audio_decoder_t* pThis);
void qcap2_audio_decoder_set_audio_property(qcap2_audio_decoder_t* pThis, qcap2_audio_encoder_property_t* pAudioEncoderProperty);
void qcap2_audio_decoder_get_audio_property(qcap2_audio_decoder_t* pThis, qcap2_audio_encoder_property_t* pAudioEncoderProperty);
void qcap2_audio_decoder_get_extra_data(qcap2_audio_decoder_t* pThis, uint8_t** ppExtraData, int* pExtraDataSize);
void qcap2_audio_decoder_set_extra_data(qcap2_audio_decoder_t* pThis, uint8_t* pExtraData, int nExtraDataSize);
void qcap2_audio_decoder_set_frame_count(qcap2_audio_decoder_t* pThis, int nFrameCount);
void qcap2_audio_decoder_set_packet_count(qcap2_audio_decoder_t* pThis, int nPacketCount);
void qcap2_audio_decoder_set_multithread(qcap2_audio_decoder_t* pThis, bool bMultiThread);
void qcap2_audio_decoder_set_event(qcap2_audio_decoder_t* pThis, qcap2_event_t* pEvent);
void qcap2_audio_decoder_set_buffers(qcap2_audio_decoder_t* pThis, qcap2_rcbuffer_t** pBuffers);
void qcap2_audio_decoder_set_payload_type(qcap2_audio_decoder_t* pThis, int nPayloadType);
QRESULT qcap2_audio_decoder_start(qcap2_audio_decoder_t* pThis);
QRESULT qcap2_audio_decoder_stop(qcap2_audio_decoder_t* pThis);
QRESULT qcap2_audio_decoder_push(qcap2_audio_decoder_t* pThis, qcap2_rcbuffer_t* pRCBuffer);
QRESULT qcap2_audio_decoder_pop(qcap2_audio_decoder_t* pThis, qcap2_rcbuffer_t** ppRCBuffer);
QRESULT qcap2_audio_decoder_pop_input(qcap2_audio_decoder_t* pThis, qcap2_rcbuffer_t** ppRCBuffer);
QRESULT qcap2_audio_decoder_push_output(qcap2_audio_decoder_t* pThis, qcap2_rcbuffer_t* pRCBuffer);

// qcap2_frame_pool_t
qcap2_frame_pool_t* qcap2_frame_pool_new();
void qcap2_frame_pool_delete(qcap2_frame_pool_t* pThis);
void qcap2_frame_pool_set_backend_type(qcap2_frame_pool_t* pThis, int nBackendType);
void qcap2_frame_pool_set_frame_count(qcap2_frame_pool_t* pThis, int nFrameCount);
void qcap2_frame_pool_set_video_frame_align(qcap2_frame_pool_t* pThis, int nFrameAlign, int nFrameVAlign);
void qcap2_frame_pool_set_audio_frame_align(qcap2_frame_pool_t* pThis, int nFrameAlign);
void qcap2_frame_pool_set_video_property(qcap2_frame_pool_t* pThis, ULONG nColorSpaceType, ULONG nFrameWidth, ULONG nFrameHeight);
void qcap2_frame_pool_set_video_property1(qcap2_frame_pool_t* pThis, ULONG nWidthBorder, ULONG nHeightBorder, BOOL bMapped);
void qcap2_frame_pool_set_audio_property(qcap2_frame_pool_t* pThis, ULONG nChannels, ULONG nSampleFmt, ULONG nSampleFrequency, ULONG nAudioFrameSize);
QRESULT qcap2_frame_pool_start(qcap2_frame_pool_t* pThis);
QRESULT qcap2_frame_pool_stop(qcap2_frame_pool_t* pThis);
QRESULT qcap2_frame_pool_get_buffer(qcap2_frame_pool_t* pThis, qcap2_rcbuffer_t** ppBuffer);

// qcap2_packet_pool_t
qcap2_packet_pool_t* qcap2_packet_pool_new();
void qcap2_packet_pool_delete(qcap2_packet_pool_t* pThis);
void qcap2_packet_pool_set_packet_count(qcap2_packet_pool_t* pThis, int nPacketCount);
QRESULT qcap2_packet_pool_start(qcap2_packet_pool_t* pThis);
QRESULT qcap2_packet_pool_stop(qcap2_packet_pool_t* pThis);
QRESULT qcap2_packet_pool_get_buffer(qcap2_packet_pool_t* pThis, int nPacketSize, qcap2_rcbuffer_t** ppBuffer);

// qcap2_video_matte_t
qcap2_video_matte_t* qcap2_video_matte_new();
void qcap2_video_matte_delete(qcap2_video_matte_t* pThis);
void qcap2_video_matte_set_video_format(qcap2_video_matte_t* pThis, qcap2_video_format_t* pVideoFormat);
void qcap2_video_matte_get_video_format(qcap2_video_matte_t* pThis, qcap2_video_format_t* pVideoFormat);
void qcap2_video_matte_set_backend_type(qcap2_video_matte_t* pThis, int nBackendType);
void qcap2_video_matte_set_frame_count(qcap2_video_matte_t* pThis, int nFrameCount);
void qcap2_video_matte_set_buffers(qcap2_video_matte_t* pThis, qcap2_rcbuffer_t** pBuffers);
void qcap2_video_matte_set_alpha_buffers(qcap2_video_matte_t* pThis, qcap2_rcbuffer_t** pAlphaBuffers);
void qcap2_video_matte_set_params(qcap2_video_matte_t* pThis, float pParams[16]);
QRESULT qcap2_video_matte_start(qcap2_video_matte_t* pThis);
QRESULT qcap2_video_matte_stop(qcap2_video_matte_t* pThis);
QRESULT qcap2_video_matte_push(qcap2_video_matte_t* pThis, qcap2_rcbuffer_t* pRCBuffer);
QRESULT qcap2_video_matte_pop(qcap2_video_matte_t* pThis, qcap2_rcbuffer_t** ppRCBuffer, qcap2_rcbuffer_t** ppRCBuffer_Alpha);

// qcap2_video_blender_t
qcap2_video_blender_t* qcap2_video_blender_new();
void qcap2_video_blender_delete(qcap2_video_blender_t* pThis);
void qcap2_video_blender_set_video_format(qcap2_video_blender_t* pThis, qcap2_video_format_t* pVideoFormat);
void qcap2_video_blender_get_video_format(qcap2_video_blender_t* pThis, qcap2_video_format_t* pVideoFormat);
void qcap2_video_blender_set_backend_type(qcap2_video_blender_t* pThis, int nBackendType);
void qcap2_video_blender_set_frame_count(qcap2_video_blender_t* pThis, int nFrameCount);
void qcap2_video_blender_set_buffers(qcap2_video_blender_t* pThis, qcap2_rcbuffer_t** pBuffers);
QRESULT qcap2_video_blender_start(qcap2_video_blender_t* pThis);
QRESULT qcap2_video_blender_stop(qcap2_video_blender_t* pThis);
QRESULT qcap2_video_blender_push(qcap2_video_blender_t* pThis, qcap2_rcbuffer_t* pRCBuffer, qcap2_rcbuffer_t* pRCBuffer_cover, qcap2_rcbuffer_t* pRCBuffer_alpha);
QRESULT qcap2_video_blender_pop(qcap2_video_blender_t* pThis, qcap2_rcbuffer_t** ppRCBuffer);

// qcap2_bitstream_filter_t
qcap2_bitstream_filter_t* qcap2_bitstream_filter_new();
void qcap2_bitstream_filter_delete(qcap2_bitstream_filter_t* pThis);
void qcap2_bitstream_filter_set_backend_type(qcap2_bitstream_filter_t* pThis, int nBackendType);
void qcap2_bitstream_filter_set_video_encoder_format(qcap2_bitstream_filter_t* pThis, ULONG nEncoderFormat);
void qcap2_bitstream_filter_get_video_encoder_format(qcap2_bitstream_filter_t* pThis, ULONG* pEncoderFormat);
void qcap2_bitstream_filter_set_audio_encoder_format(qcap2_bitstream_filter_t* pThis, ULONG nEncoderFormat);
void qcap2_bitstream_filter_get_audio_encoder_format(qcap2_bitstream_filter_t* pThis, ULONG* pEncoderFormat);
void qcap2_bitstream_filter_set_packet_count(qcap2_bitstream_filter_t* pThis, int nPacketCount);
void qcap2_bitstream_filter_set_event(qcap2_bitstream_filter_t* pThis, qcap2_event_t* pEvent);
QRESULT qcap2_bitstream_filter_start(qcap2_bitstream_filter_t* pThis);
QRESULT qcap2_bitstream_filter_stop(qcap2_bitstream_filter_t* pThis);
QRESULT qcap2_bitstream_filter_push(qcap2_bitstream_filter_t* pThis, qcap2_rcbuffer_t* pRCBuffer, int n, ...);
QRESULT qcap2_bitstream_filter_pop(qcap2_bitstream_filter_t* pThis, qcap2_rcbuffer_t** ppRCBuffer, int n, ...);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif // __QCAP2_PROCESSING_H__
