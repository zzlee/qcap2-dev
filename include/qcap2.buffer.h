#ifndef __QCAP2_BUFFER_H__
#define __QCAP2_BUFFER_H__

#include "qcap2.types.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

// qcap2_rcbuffer_t
qcap2_rcbuffer_t* qcap2_rcbuffer_new(PVOID pData, qcap2_on_free_resource_t pOnFreeResource);
void qcap2_rcbuffer_delete(qcap2_rcbuffer_t* pRCBuffer);
void qcap2_rcbuffer_to_buffer(qcap2_rcbuffer_t* pRCBuffer, BYTE** ppBuffer, ULONG* pBufferSize);
qcap2_rcbuffer_t* qcap2_rcbuffer_cast(BYTE * pBuffer, ULONG nBufferLen);
void qcap2_rcbuffer_add_ref(qcap2_rcbuffer_t* pRCBuffer);
void qcap2_rcbuffer_release(qcap2_rcbuffer_t* pRCBuffer);
PVOID qcap2_rcbuffer_lock_data(qcap2_rcbuffer_t* pRCBuffer);
void qcap2_rcbuffer_unlock_data(qcap2_rcbuffer_t* pRCBuffer);
PVOID qcap2_rcbuffer_get_data(qcap2_rcbuffer_t* pRCBuffer); // risky accessor
int32_t qcap2_rcbuffer_use_count(qcap2_rcbuffer_t* pRCBuffer);
int32_t qcap2_rcbuffer_res_count(qcap2_rcbuffer_t* pRCBuffer);

// --- Extended Buffer Extensibility APIs ---
typedef enum {
    QCAP2_BUFFER_TYPE_SYSTEM = 0,
    QCAP2_BUFFER_TYPE_DMABUF,
    QCAP2_BUFFER_TYPE_V4L2,
    QCAP2_BUFFER_TYPE_CUDA,
    QCAP2_BUFFER_TYPE_NVBUF,
    QCAP2_BUFFER_TYPE_AVFRAME,
    QCAP2_BUFFER_TYPE_AVPACKET,
    QCAP2_BUFFER_TYPE_CUSTOM
} qcap2_buffer_type_t;

qcap2_buffer_type_t qcap2_rcbuffer_get_type(qcap2_rcbuffer_t* pRCBuffer);
PVOID qcap2_rcbuffer_get_native_handle(qcap2_rcbuffer_t* pRCBuffer);

QRESULT qcap2_rcbuffer_get_pts(qcap2_rcbuffer_t* pRCBuffer, int64_t* pts);
QRESULT qcap2_rcbuffer_set_pts(qcap2_rcbuffer_t* pRCBuffer, int64_t pts);
QRESULT qcap2_rcbuffer_get_dts(qcap2_rcbuffer_t* pRCBuffer, int64_t* dts);
QRESULT qcap2_rcbuffer_set_dts(qcap2_rcbuffer_t* pRCBuffer, int64_t dts);
QRESULT qcap2_rcbuffer_get_stream_index(qcap2_rcbuffer_t* pRCBuffer, int* idx);
QRESULT qcap2_rcbuffer_set_stream_index(qcap2_rcbuffer_t* pRCBuffer, int idx);
QRESULT qcap2_rcbuffer_is_keyframe(qcap2_rcbuffer_t* pRCBuffer, BOOL* key);
QRESULT qcap2_rcbuffer_set_keyframe(qcap2_rcbuffer_t* pRCBuffer, BOOL key);

QRESULT qcap2_rcbuffer_get_data_ptr(qcap2_rcbuffer_t* pRCBuffer, uint8_t** data, int* size);
QRESULT qcap2_rcbuffer_get_video_property(qcap2_rcbuffer_t* pRCBuffer, ULONG* colorspace, ULONG* width, ULONG* height);
QRESULT qcap2_rcbuffer_get_plane(qcap2_rcbuffer_t* pRCBuffer, int plane, uint8_t** data, int* stride);

QRESULT qcap2_rcbuffer_map_system_memory(qcap2_rcbuffer_t* pRCBuffer, PVOID* ppDataOut);
QRESULT qcap2_rcbuffer_unmap_system_memory(qcap2_rcbuffer_t* pRCBuffer);


// qcap2_av_frame_t
void qcap2_av_frame_init(qcap2_av_frame_t* pFrame);
void qcap2_av_frame_set_video_property(qcap2_av_frame_t* pFrame, ULONG nColorSpaceType, ULONG nWidth, ULONG nHeight);
void qcap2_av_frame_get_video_property(qcap2_av_frame_t* pFrame, ULONG* pColorSpaceType, ULONG* pWidth, ULONG* pHeight);
void qcap2_av_frame_set_audio_property(qcap2_av_frame_t* pFrame, ULONG nChannels, ULONG nSampleFmt, ULONG nSampleFrequency, ULONG nFrameSize);
void qcap2_av_frame_get_audio_property(qcap2_av_frame_t* pFrame, ULONG* pChannels, ULONG* pSampleFmt, ULONG* pSampleFrequency, ULONG* pFrameSize);
void qcap2_av_frame_set_field_type(qcap2_av_frame_t* pFrame, int nFieldType); // refer to qcap2_field_type_t
void qcap2_av_frame_get_field_type(qcap2_av_frame_t* pFrame, int* pFieldType); // refer to qcap2_field_type_t
void qcap2_av_frame_set_sample_time(qcap2_av_frame_t* pFrame, double dSampleTime);
void qcap2_av_frame_get_sample_time(qcap2_av_frame_t* pFrame, double* pSampleTime);
void qcap2_av_frame_set_pts(qcap2_av_frame_t* pFrame, int64_t nPTS);
void qcap2_av_frame_get_pts(qcap2_av_frame_t* pFrame, int64_t* pPTS);
void qcap2_av_frame_set_pkt_pos(qcap2_av_frame_t* pFrame, int64_t nPktPos);
void qcap2_av_frame_get_pkt_pos(qcap2_av_frame_t* pFrame, int64_t* pPktPos);
void qcap2_av_frame_get_video_bits(qcap2_av_frame_t* pFrame, int64_t* pBits);
void qcap2_av_frame_get_audio_bits(qcap2_av_frame_t* pFrame, int64_t* pBits);
void qcap2_av_frame_set_buffer(qcap2_av_frame_t* pFrame, uint8_t* pBuffer, int nStride);
void qcap2_av_frame_get_buffer(qcap2_av_frame_t* pFrame, uint8_t** ppBuffer, int* pStride);
void qcap2_av_frame_set_buffer1(qcap2_av_frame_t* pFrame, uint8_t* pBuffer[4], int pStride[4]);
void qcap2_av_frame_get_buffer1(qcap2_av_frame_t* pFrame, uint8_t* pBuffer[4], int pStride[4]);
bool qcap2_av_frame_alloc_buffer(qcap2_av_frame_t* pFrame, int align, int valign);
void qcap2_av_frame_free_buffer(qcap2_av_frame_t* pFrame);
QRESULT qcap2_av_frame_copy(qcap2_av_frame_t* pSrcFrame, qcap2_av_frame_t* pDstFrame);
QRESULT qcap2_av_frame_color_range_expand(qcap2_av_frame_t* pSrcFrame, qcap2_av_frame_t* pDstFrame); // limited-range -> full-range
QRESULT qcap2_av_frame_store_picture(qcap2_av_frame_t* pFrame, const char* strFilePath);
QRESULT qcap2_av_frame_store_picture2(qcap2_av_frame_t* pFrame, const char* strFilePath, int nQuality);

// qcap2_av_packet_t
void qcap2_av_packet_init(qcap2_av_packet_t* pPacket);
void qcap2_av_packet_set_property(qcap2_av_packet_t* pPacket, int nStreamIndex, BOOL bIsKeyFrame);
void qcap2_av_packet_get_property(qcap2_av_packet_t* pPacket, int* pStreamIndex, BOOL* pIsKeyFrame);
void qcap2_av_packet_set_sample_time(qcap2_av_packet_t* pPacket, double dSampleTime);
void qcap2_av_packet_get_sample_time(qcap2_av_packet_t* pPacket, double* pSampleTime);
void qcap2_av_packet_set_pts(qcap2_av_packet_t* pPacket, int64_t nPTS);
void qcap2_av_packet_get_pts(qcap2_av_packet_t* pPacket, int64_t* pPTS);
void qcap2_av_packet_set_dts(qcap2_av_packet_t* pPacket, int64_t nDTS);
void qcap2_av_packet_get_dts(qcap2_av_packet_t* pPacket, int64_t* pDTS);
void qcap2_av_packet_set_buffer(qcap2_av_packet_t* pPacket, uint8_t* pBuffer, int nSize);
void qcap2_av_packet_get_buffer(qcap2_av_packet_t* pPacket, uint8_t** ppBuffer, int* pSize);
bool qcap2_av_packet_alloc_buffer(qcap2_av_packet_t* pPacket, int nSize);
void qcap2_av_packet_free_buffer(qcap2_av_packet_t* pPacket);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif // __QCAP2_BUFFER_H__
