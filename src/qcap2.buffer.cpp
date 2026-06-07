#include "qcap2.buffer.h"
#include "qcap2.user.h"
#include "qcap2.dmabuf.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <atomic>
#include <new>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>

#include "qcap2.buffer_priv.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- qcap2_rcbuffer_t C-to-C++ Forwarding layer ---

qcap2_rcbuffer_t* qcap2_rcbuffer_new(PVOID pData, qcap2_on_free_resource_t pOnFreeResource) {
    return new (std::nothrow) qcap2_system_buffer(pData, pOnFreeResource);
}

void qcap2_rcbuffer_delete(qcap2_rcbuffer_t* pRCBuffer) {
    if (pRCBuffer) pRCBuffer->release();
}

void qcap2_rcbuffer_to_buffer(qcap2_rcbuffer_t* pRCBuffer, BYTE** ppBuffer, ULONG* pBufferSize) {
    if (ppBuffer) *ppBuffer = NULL;
    if (pBufferSize) *pBufferSize = 0;

    if (pRCBuffer) {
        if (ppBuffer) *ppBuffer = (BYTE*)pRCBuffer->get_data();
        if (pBufferSize) {
            if (pRCBuffer->get_type() == QCAP2_BUFFER_TYPE_SYSTEM) {
                *pBufferSize = static_cast<qcap2_system_buffer*>(pRCBuffer)->get_data_size();
            } else {
                uint8_t* ptr = nullptr;
                int size = 0;
                if (pRCBuffer->get_data_ptr(&ptr, &size) == QCAP_RS_SUCCESSFUL) {
                    *pBufferSize = size;
                }
            }
        }
    }
}

qcap2_rcbuffer_t* qcap2_rcbuffer_cast(BYTE * pBuffer, ULONG nBufferLen) {
    auto* buf = new (std::nothrow) qcap2_system_buffer(pBuffer, NULL);
    if (buf) {
        buf->set_data_size(nBufferLen);
    }
    return buf;
}

void qcap2_rcbuffer_add_ref(qcap2_rcbuffer_t* pRCBuffer) {
    if (pRCBuffer) pRCBuffer->add_ref();
}

void qcap2_rcbuffer_release(qcap2_rcbuffer_t* pRCBuffer) {
    if (pRCBuffer) pRCBuffer->release();
}

PVOID qcap2_rcbuffer_lock_data(qcap2_rcbuffer_t* pRCBuffer) {
    return pRCBuffer ? pRCBuffer->lock_data() : NULL;
}

void qcap2_rcbuffer_unlock_data(qcap2_rcbuffer_t* pRCBuffer) {
    if (pRCBuffer) pRCBuffer->unlock_data();
}

PVOID qcap2_rcbuffer_get_data(qcap2_rcbuffer_t* pRCBuffer) {
    return pRCBuffer ? pRCBuffer->get_data() : NULL;
}

int32_t qcap2_rcbuffer_use_count(qcap2_rcbuffer_t* pRCBuffer) {
    return pRCBuffer ? pRCBuffer->use_count() : 0;
}

int32_t qcap2_rcbuffer_res_count(qcap2_rcbuffer_t* pRCBuffer) {
    return pRCBuffer ? pRCBuffer->res_count() : 0;
}

qcap2_buffer_type_t qcap2_rcbuffer_get_type(qcap2_rcbuffer_t* pRCBuffer) {
    return pRCBuffer ? pRCBuffer->get_type() : QCAP2_BUFFER_TYPE_SYSTEM;
}

PVOID qcap2_rcbuffer_get_native_handle(qcap2_rcbuffer_t* pRCBuffer) {
    return pRCBuffer ? pRCBuffer->get_native_handle() : NULL;
}

QRESULT qcap2_rcbuffer_get_pts(qcap2_rcbuffer_t* pRCBuffer, int64_t* pts) {
    if (!pRCBuffer) return QCAP_RS_ERROR_INVALID_PARAMETER;
    return pRCBuffer->get_pts(pts);
}

QRESULT qcap2_rcbuffer_set_pts(qcap2_rcbuffer_t* pRCBuffer, int64_t pts) {
    if (!pRCBuffer) return QCAP_RS_ERROR_INVALID_PARAMETER;
    return pRCBuffer->set_pts(pts);
}

QRESULT qcap2_rcbuffer_get_dts(qcap2_rcbuffer_t* pRCBuffer, int64_t* dts) {
    if (!pRCBuffer) return QCAP_RS_ERROR_INVALID_PARAMETER;
    return pRCBuffer->get_dts(dts);
}

QRESULT qcap2_rcbuffer_set_dts(qcap2_rcbuffer_t* pRCBuffer, int64_t dts) {
    if (!pRCBuffer) return QCAP_RS_ERROR_INVALID_PARAMETER;
    return pRCBuffer->set_dts(dts);
}

QRESULT qcap2_rcbuffer_get_stream_index(qcap2_rcbuffer_t* pRCBuffer, int* idx) {
    if (!pRCBuffer) return QCAP_RS_ERROR_INVALID_PARAMETER;
    return pRCBuffer->get_stream_index(idx);
}

QRESULT qcap2_rcbuffer_set_stream_index(qcap2_rcbuffer_t* pRCBuffer, int idx) {
    if (!pRCBuffer) return QCAP_RS_ERROR_INVALID_PARAMETER;
    return pRCBuffer->set_stream_index(idx);
}

QRESULT qcap2_rcbuffer_is_keyframe(qcap2_rcbuffer_t* pRCBuffer, BOOL* key) {
    if (!pRCBuffer) return QCAP_RS_ERROR_INVALID_PARAMETER;
    return pRCBuffer->is_keyframe(key);
}

QRESULT qcap2_rcbuffer_set_keyframe(qcap2_rcbuffer_t* pRCBuffer, BOOL key) {
    if (!pRCBuffer) return QCAP_RS_ERROR_INVALID_PARAMETER;
    return pRCBuffer->set_keyframe(key);
}

QRESULT qcap2_rcbuffer_get_data_ptr(qcap2_rcbuffer_t* pRCBuffer, uint8_t** data, int* size) {
    if (!pRCBuffer) return QCAP_RS_ERROR_INVALID_PARAMETER;
    return pRCBuffer->get_data_ptr(data, size);
}

QRESULT qcap2_rcbuffer_get_video_property(qcap2_rcbuffer_t* pRCBuffer, ULONG* colorspace, ULONG* width, ULONG* height) {
    if (!pRCBuffer) return QCAP_RS_ERROR_INVALID_PARAMETER;
    return pRCBuffer->get_video_property(colorspace, width, height);
}

QRESULT qcap2_rcbuffer_get_plane(qcap2_rcbuffer_t* pRCBuffer, int plane, uint8_t** data, int* stride) {
    if (!pRCBuffer) return QCAP_RS_ERROR_INVALID_PARAMETER;
    return pRCBuffer->get_plane(plane, data, stride);
}

QRESULT qcap2_rcbuffer_map_system_memory(qcap2_rcbuffer_t* pRCBuffer, PVOID* ppDataOut) {
    if (!pRCBuffer) return QCAP_RS_ERROR_INVALID_PARAMETER;
    return pRCBuffer->map_system_memory(ppDataOut);
}

QRESULT qcap2_rcbuffer_unmap_system_memory(qcap2_rcbuffer_t* pRCBuffer) {
    if (!pRCBuffer) return QCAP_RS_ERROR_INVALID_PARAMETER;
    return pRCBuffer->unmap_system_memory();
}

// --- qcap2_av_frame_t ---

void qcap2_av_frame_init(qcap2_av_frame_t* pFrame) {
    if (pFrame) {
        memset(pFrame, 0, sizeof(qcap2_av_frame_t));
    }
}

void qcap2_av_frame_set_video_property(qcap2_av_frame_t* pFrame, ULONG nColorSpaceType, ULONG nWidth, ULONG nHeight) {
    if (pFrame) {
        qcap2_av_frame_priv_t* p = (qcap2_av_frame_priv_t*)pFrame;
        p->nColorSpaceType = nColorSpaceType;
        p->nWidth = nWidth;
        p->nHeight = nHeight;
    }
}

void qcap2_av_frame_get_video_property(qcap2_av_frame_t* pFrame, ULONG* pColorSpaceType, ULONG* pWidth, ULONG* pHeight) {
    if (pFrame) {
        qcap2_av_frame_priv_t* p = (qcap2_av_frame_priv_t*)pFrame;
        if (pColorSpaceType) *pColorSpaceType = p->nColorSpaceType;
        if (pWidth) *pWidth = p->nWidth;
        if (pHeight) *pHeight = p->nHeight;
    }
}

void qcap2_av_frame_set_audio_property(qcap2_av_frame_t* pFrame, ULONG nChannels, ULONG nSampleFmt, ULONG nSampleFrequency, ULONG nFrameSize) {
    if (pFrame) {
        qcap2_av_frame_priv_t* p = (qcap2_av_frame_priv_t*)pFrame;
        p->nChannels = nChannels;
        p->nSampleFmt = nSampleFmt;
        p->nSampleFrequency = nSampleFrequency;
        p->nFrameSize = nFrameSize;
    }
}

void qcap2_av_frame_get_audio_property(qcap2_av_frame_t* pFrame, ULONG* pChannels, ULONG* pSampleFmt, ULONG* pSampleFrequency, ULONG* pFrameSize) {
    if (pFrame) {
        qcap2_av_frame_priv_t* p = (qcap2_av_frame_priv_t*)pFrame;
        if (pChannels) *pChannels = p->nChannels;
        if (pSampleFmt) *pSampleFmt = p->nSampleFmt;
        if (pSampleFrequency) *pSampleFrequency = p->nSampleFrequency;
        if (pFrameSize) *pFrameSize = p->nFrameSize;
    }
}

void qcap2_av_frame_set_field_type(qcap2_av_frame_t* pFrame, int nFieldType) {
    if (pFrame) {
        qcap2_av_frame_priv_t* p = (qcap2_av_frame_priv_t*)pFrame;
        p->nFieldType = nFieldType;
    }
}

void qcap2_av_frame_get_field_type(qcap2_av_frame_t* pFrame, int* pFieldType) {
    if (pFrame) {
        qcap2_av_frame_priv_t* p = (qcap2_av_frame_priv_t*)pFrame;
        if (pFieldType) *pFieldType = p->nFieldType;
    }
}

void qcap2_av_frame_set_sample_time(qcap2_av_frame_t* pFrame, double dSampleTime) {
    if (pFrame) {
        qcap2_av_frame_priv_t* p = (qcap2_av_frame_priv_t*)pFrame;
        p->dSampleTime = dSampleTime;
    }
}

void qcap2_av_frame_get_sample_time(qcap2_av_frame_t* pFrame, double* pSampleTime) {
    if (pFrame) {
        qcap2_av_frame_priv_t* p = (qcap2_av_frame_priv_t*)pFrame;
        if (pSampleTime) *pSampleTime = p->dSampleTime;
    }
}

void qcap2_av_frame_set_pts(qcap2_av_frame_t* pFrame, int64_t nPTS) {
    if (pFrame) {
        qcap2_av_frame_priv_t* p = (qcap2_av_frame_priv_t*)pFrame;
        p->nPTS = nPTS;
    }
}

void qcap2_av_frame_get_pts(qcap2_av_frame_t* pFrame, int64_t* pPTS) {
    if (pFrame) {
        qcap2_av_frame_priv_t* p = (qcap2_av_frame_priv_t*)pFrame;
        if (pPTS) *pPTS = p->nPTS;
    }
}

void qcap2_av_frame_set_pkt_pos(qcap2_av_frame_t* pFrame, int64_t nPktPos) {
    if (pFrame) {
        qcap2_av_frame_priv_t* p = (qcap2_av_frame_priv_t*)pFrame;
        p->nPktPos = nPktPos;
    }
}

void qcap2_av_frame_get_pkt_pos(qcap2_av_frame_t* pFrame, int64_t* pPktPos) {
    if (pFrame) {
        qcap2_av_frame_priv_t* p = (qcap2_av_frame_priv_t*)pFrame;
        if (pPktPos) *pPktPos = p->nPktPos;
    }
}

void qcap2_av_frame_get_video_bits(qcap2_av_frame_t* pFrame, int64_t* pBits) {
    if (pFrame) {
        qcap2_av_frame_priv_t* p = (qcap2_av_frame_priv_t*)pFrame;
        if (pBits) *pBits = p->nVideoBits;
    }
}

void qcap2_av_frame_get_audio_bits(qcap2_av_frame_t* pFrame, int64_t* pBits) {
    if (pFrame) {
        qcap2_av_frame_priv_t* p = (qcap2_av_frame_priv_t*)pFrame;
        if (pBits) *pBits = p->nAudioBits;
    }
}

void qcap2_av_frame_set_buffer(qcap2_av_frame_t* pFrame, uint8_t* pBuffer, int nStride) {
    if (pFrame) {
        qcap2_av_frame_priv_t* p = (qcap2_av_frame_priv_t*)pFrame;
        p->pBuffer[0] = pBuffer;
        p->pStride[0] = nStride;
    }
}

void qcap2_av_frame_get_buffer(qcap2_av_frame_t* pFrame, uint8_t** ppBuffer, int* pStride) {
    if (pFrame) {
        qcap2_av_frame_priv_t* p = (qcap2_av_frame_priv_t*)pFrame;
        if (ppBuffer) *ppBuffer = p->pBuffer[0];
        if (pStride) *pStride = p->pStride[0];
    }
}

void qcap2_av_frame_set_buffer1(qcap2_av_frame_t* pFrame, uint8_t* pBuffer[4], int pStride[4]) {
    if (pFrame) {
        qcap2_av_frame_priv_t* p = (qcap2_av_frame_priv_t*)pFrame;
        for (int i = 0; i < 4; i++) {
            p->pBuffer[i] = pBuffer[i];
            p->pStride[i] = pStride[i];
        }
    }
}

void qcap2_av_frame_get_buffer1(qcap2_av_frame_t* pFrame, uint8_t* pBuffer[4], int pStride[4]) {
    if (pFrame) {
        qcap2_av_frame_priv_t* p = (qcap2_av_frame_priv_t*)pFrame;
        for (int i = 0; i < 4; i++) {
            if (pBuffer) pBuffer[i] = p->pBuffer[i];
            if (pStride) pStride[i] = p->pStride[i];
        }
    }
}

static int qcap2_align_value(int value, int alignment) {
    if (value <= 0) return 0;
    if (alignment <= 1) return value;
    return ((value + alignment - 1) / alignment) * alignment;
}

static bool qcap2_av_frame_set_owned_layout(qcap2_av_frame_priv_t* p, size_t nTotalSize, int nPlanes, const size_t nPlaneOffset[4], const int nStride[4]) {
    if (!p || nTotalSize == 0 || nPlanes <= 0 || nPlanes > 4) return false;

    uint8_t* pMemory = (uint8_t*)malloc(nTotalSize);
    if (!pMemory) return false;

    memset(p->pBuffer, 0, sizeof(p->pBuffer));
    memset(p->pStride, 0, sizeof(p->pStride));
    for (int i = 0; i < nPlanes; ++i) {
        p->pBuffer[i] = pMemory + nPlaneOffset[i];
        p->pStride[i] = nStride[i];
    }
    p->bOwnsBuffer = true;
    p->nVideoBits = (int64_t)nTotalSize * 8;
    return true;
}

bool qcap2_av_frame_alloc_buffer(qcap2_av_frame_t* pFrame, int align, int valign) {
    if (!pFrame) return false;
    qcap2_av_frame_priv_t* p = (qcap2_av_frame_priv_t*)pFrame;

    qcap2_av_frame_free_buffer(pFrame);

    int nWidth = (int)p->nWidth;
    int nHeight = (int)p->nHeight;
    if (nWidth <= 0 || nHeight <= 0) return false;

    int nAlignedHeight = qcap2_align_value(nHeight, valign);
    int nChromaWidth = (nWidth + 1) / 2;
    int nChromaHeight = (nHeight + 1) / 2;
    int nAlignedChromaHeight = qcap2_align_value(nChromaHeight, valign);

    size_t nOffset[4] = { 0, 0, 0, 0 };
    int nStride[4] = { 0, 0, 0, 0 };
    size_t nSize[4] = { 0, 0, 0, 0 };

    switch (p->nColorSpaceType) {
    case QCAP_COLORSPACE_TYPE_RGB24:
    case QCAP_COLORSPACE_TYPE_BGR24:
        nStride[0] = qcap2_align_value(nWidth * 3, align);
        nSize[0] = (size_t)nStride[0] * nAlignedHeight;
        return qcap2_av_frame_set_owned_layout(p, nSize[0], 1, nOffset, nStride);

    case QCAP_COLORSPACE_TYPE_ARGB32:
    case QCAP_COLORSPACE_TYPE_ABGR32:
        nStride[0] = qcap2_align_value(nWidth * 4, align);
        nSize[0] = (size_t)nStride[0] * nAlignedHeight;
        return qcap2_av_frame_set_owned_layout(p, nSize[0], 1, nOffset, nStride);

    case QCAP_COLORSPACE_TYPE_Y416:
        nStride[0] = qcap2_align_value(nWidth * 8, align);
        nSize[0] = (size_t)nStride[0] * nAlignedHeight;
        return qcap2_av_frame_set_owned_layout(p, nSize[0], 1, nOffset, nStride);

    case QCAP_COLORSPACE_TYPE_YUY2:
    case QCAP_COLORSPACE_TYPE_UYVY:
        nStride[0] = qcap2_align_value(nWidth * 2, align);
        nSize[0] = (size_t)nStride[0] * nAlignedHeight;
        return qcap2_av_frame_set_owned_layout(p, nSize[0], 1, nOffset, nStride);

    case QCAP_COLORSPACE_TYPE_Y800:
        nStride[0] = qcap2_align_value(nWidth, align);
        nSize[0] = (size_t)nStride[0] * nAlignedHeight;
        return qcap2_av_frame_set_owned_layout(p, nSize[0], 1, nOffset, nStride);

    case QCAP_COLORSPACE_TYPE_YV12:
    case QCAP_COLORSPACE_TYPE_I420:
        nStride[0] = qcap2_align_value(nWidth, align);
        nStride[1] = qcap2_align_value(nChromaWidth, align);
        nStride[2] = nStride[1];
        nSize[0] = (size_t)nStride[0] * nAlignedHeight;
        nSize[1] = (size_t)nStride[1] * nAlignedChromaHeight;
        nSize[2] = nSize[1];
        nOffset[1] = nSize[0];
        nOffset[2] = nSize[0] + nSize[1];
        return qcap2_av_frame_set_owned_layout(p, nSize[0] + nSize[1] + nSize[2], 3, nOffset, nStride);

    case QCAP_COLORSPACE_TYPE_YV24:
        nStride[0] = qcap2_align_value(nWidth, align);
        nStride[1] = nStride[0];
        nStride[2] = nStride[0];
        nSize[0] = (size_t)nStride[0] * nAlignedHeight;
        nSize[1] = nSize[0];
        nSize[2] = nSize[0];
        nOffset[1] = nSize[0];
        nOffset[2] = nSize[0] + nSize[1];
        return qcap2_av_frame_set_owned_layout(p, nSize[0] + nSize[1] + nSize[2], 3, nOffset, nStride);

    case QCAP_COLORSPACE_TYPE_NV12:
        nStride[0] = qcap2_align_value(nWidth, align);
        nStride[1] = nStride[0];
        nSize[0] = (size_t)nStride[0] * nAlignedHeight;
        nSize[1] = (size_t)nStride[1] * nAlignedChromaHeight;
        nOffset[1] = nSize[0];
        return qcap2_av_frame_set_owned_layout(p, nSize[0] + nSize[1], 2, nOffset, nStride);

    case QCAP_COLORSPACE_TYPE_P010:
        nStride[0] = qcap2_align_value(nWidth * 2, align);
        nStride[1] = nStride[0];
        nSize[0] = (size_t)nStride[0] * nAlignedHeight;
        nSize[1] = (size_t)nStride[1] * nAlignedChromaHeight;
        nOffset[1] = nSize[0];
        return qcap2_av_frame_set_owned_layout(p, nSize[0] + nSize[1], 2, nOffset, nStride);

    case QCAP_COLORSPACE_TYPE_P210:
        nStride[0] = qcap2_align_value(nWidth * 2, align);
        nStride[1] = nStride[0];
        nSize[0] = (size_t)nStride[0] * nAlignedHeight;
        nSize[1] = (size_t)nStride[1] * nAlignedHeight;
        nOffset[1] = nSize[0];
        return qcap2_av_frame_set_owned_layout(p, nSize[0] + nSize[1], 2, nOffset, nStride);

    default:
        return false;
    }
}

void qcap2_av_frame_free_buffer(qcap2_av_frame_t* pFrame) {
    if (!pFrame) return;
    qcap2_av_frame_priv_t* p = (qcap2_av_frame_priv_t*)pFrame;
    qcap2_av_frame_free_mapped_dmabuf(pFrame);
    if (p->bOwnsBuffer && p->pBuffer[0]) {
        free(p->pBuffer[0]);
    }
    memset(p->pBuffer, 0, sizeof(p->pBuffer));
    memset(p->pStride, 0, sizeof(p->pStride));
    p->bOwnsBuffer = false;
    p->nVideoBits = 0;
}

QRESULT qcap2_av_frame_copy(qcap2_av_frame_t* pSrcFrame, qcap2_av_frame_t* pDstFrame) {
    if (!pSrcFrame || !pDstFrame) return QCAP_RS_ERROR_GENERAL;
    memcpy(pDstFrame, pSrcFrame, sizeof(qcap2_av_frame_t));

    // Deep copy buffer
    qcap2_av_frame_priv_t* pSrc = (qcap2_av_frame_priv_t*)pSrcFrame;
    qcap2_av_frame_priv_t* pDst = (qcap2_av_frame_priv_t*)pDstFrame;

    if (pSrc->pBuffer[0] && pSrc->pStride[0] > 0 && pSrc->nHeight > 0) {
        int size = pSrc->nHeight * pSrc->pStride[0];
        pDst->pBuffer[0] = (uint8_t*)malloc(size);
        if (pDst->pBuffer[0]) {
            memcpy(pDst->pBuffer[0], pSrc->pBuffer[0], size);
            pDst->bOwnsBuffer = true;
        } else {
            pDst->bOwnsBuffer = false;
        }
    }

    return QCAP_RS_SUCCESSFUL;
}

QRESULT qcap2_av_frame_color_range_expand(qcap2_av_frame_t* pSrcFrame, qcap2_av_frame_t* pDstFrame) {
    if (!pSrcFrame || !pDstFrame) return QCAP_RS_ERROR_GENERAL;
    // Just copy for now, as we don't have a real image processing library attached
    return qcap2_av_frame_copy(pSrcFrame, pDstFrame);
}

QRESULT qcap2_av_frame_store_picture(qcap2_av_frame_t* pFrame, const char* strFilePath) {
    if (!pFrame || !strFilePath) return QCAP_RS_ERROR_GENERAL;
    FILE* f = fopen(strFilePath, "wb");
    if (!f) return QCAP_RS_ERROR_GENERAL;

    qcap2_av_frame_priv_t* p = (qcap2_av_frame_priv_t*)pFrame;
    if (p->pBuffer[0] && p->nHeight > 0 && p->pStride[0] > 0) {
        fwrite(p->pBuffer[0], 1, p->nHeight * p->pStride[0], f);
    }

    fclose(f);
    return QCAP_RS_SUCCESSFUL;
}

QRESULT qcap2_av_frame_store_picture2(qcap2_av_frame_t* pFrame, const char* strFilePath, int nQuality) {
    return qcap2_av_frame_store_picture(pFrame, strFilePath);
}


// --- qcap2_av_packet_t ---

void qcap2_av_packet_init(qcap2_av_packet_t* pPacket) {
    if (pPacket) {
        memset(pPacket, 0, sizeof(qcap2_av_packet_t));
    }
}

void qcap2_av_packet_set_property(qcap2_av_packet_t* pPacket, int nStreamIndex, BOOL bIsKeyFrame) {
    if (pPacket) {
        qcap2_av_packet_priv_t* p = (qcap2_av_packet_priv_t*)pPacket;
        p->nStreamIndex = nStreamIndex;
        p->bIsKeyFrame = bIsKeyFrame;
    }
}

void qcap2_av_packet_get_property(qcap2_av_packet_t* pPacket, int* pStreamIndex, BOOL* pIsKeyFrame) {
    if (pPacket) {
        qcap2_av_packet_priv_t* p = (qcap2_av_packet_priv_t*)pPacket;
        if (pStreamIndex) *pStreamIndex = p->nStreamIndex;
        if (pIsKeyFrame) *pIsKeyFrame = p->bIsKeyFrame;
    }
}

void qcap2_av_packet_set_sample_time(qcap2_av_packet_t* pPacket, double dSampleTime) {
    if (pPacket) {
        qcap2_av_packet_priv_t* p = (qcap2_av_packet_priv_t*)pPacket;
        p->dSampleTime = dSampleTime;
    }
}

void qcap2_av_packet_get_sample_time(qcap2_av_packet_t* pPacket, double* pSampleTime) {
    if (pPacket) {
        qcap2_av_packet_priv_t* p = (qcap2_av_packet_priv_t*)pPacket;
        if (pSampleTime) *pSampleTime = p->dSampleTime;
    }
}

void qcap2_av_packet_set_pts(qcap2_av_packet_t* pPacket, int64_t nPTS) {
    if (pPacket) {
        qcap2_av_packet_priv_t* p = (qcap2_av_packet_priv_t*)pPacket;
        p->nPTS = nPTS;
    }
}

void qcap2_av_packet_get_pts(qcap2_av_packet_t* pPacket, int64_t* pPTS) {
    if (pPacket) {
        qcap2_av_packet_priv_t* p = (qcap2_av_packet_priv_t*)pPacket;
        if (pPTS) *pPTS = p->nPTS;
    }
}

void qcap2_av_packet_set_dts(qcap2_av_packet_t* pPacket, int64_t nDTS) {
    if (pPacket) {
        qcap2_av_packet_priv_t* p = (qcap2_av_packet_priv_t*)pPacket;
        p->nDTS = nDTS;
    }
}

void qcap2_av_packet_get_dts(qcap2_av_packet_t* pPacket, int64_t* pDTS) {
    if (pPacket) {
        qcap2_av_packet_priv_t* p = (qcap2_av_packet_priv_t*)pPacket;
        if (pDTS) *pDTS = p->nDTS;
    }
}

void qcap2_av_packet_set_buffer(qcap2_av_packet_t* pPacket, uint8_t* pBuffer, int nSize) {
    if (pPacket) {
        qcap2_av_packet_priv_t* p = (qcap2_av_packet_priv_t*)pPacket;
        p->pBuffer = pBuffer;
        p->nSize = nSize;
    }
}

void qcap2_av_packet_get_buffer(qcap2_av_packet_t* pPacket, uint8_t** ppBuffer, int* pSize) {
    if (pPacket) {
        qcap2_av_packet_priv_t* p = (qcap2_av_packet_priv_t*)pPacket;
        if (ppBuffer) *ppBuffer = p->pBuffer;
        if (pSize) *pSize = p->nSize;
    }
}

bool qcap2_av_packet_alloc_buffer(qcap2_av_packet_t* pPacket, int nSize) {
    if (!pPacket) return false;
    qcap2_av_packet_priv_t* p = (qcap2_av_packet_priv_t*)pPacket;
    if (p->bOwnsBuffer && p->pBuffer) {
        free(p->pBuffer);
    }
    p->pBuffer = (uint8_t*)malloc(nSize);
    p->nSize = nSize;
    p->bOwnsBuffer = true;
    return p->pBuffer != NULL;
}

void qcap2_av_packet_free_buffer(qcap2_av_packet_t* pPacket) {
    if (!pPacket) return;
    qcap2_av_packet_priv_t* p = (qcap2_av_packet_priv_t*)pPacket;
    if (p->bOwnsBuffer && p->pBuffer) {
        free(p->pBuffer);
        p->pBuffer = NULL;
    }
    p->bOwnsBuffer = false;
    p->nSize = 0;
}


// --- qcap2.user.h rc-buffer helpers ---

typedef struct _qcap2_rcbuffer_av_frame_owner_t {
    PVOID pOwner;
    qcap2_av_frame_t av_frame;
} qcap2_rcbuffer_av_frame_owner_t;

typedef struct _qcap2_rcbuffer_av_packet_owner_t {
    PVOID pOwner;
    qcap2_av_packet_t av_packet;
} qcap2_rcbuffer_av_packet_owner_t;

static void qcap2_rcbuffer_free_av_frame(PVOID pData) {
    if (!pData) return;
    qcap2_rcbuffer_av_frame_owner_t* pOwner = qcap2_container_of(pData, qcap2_rcbuffer_av_frame_owner_t, av_frame);
    qcap2_av_frame_free_buffer(&pOwner->av_frame);
    delete pOwner;
}

static void qcap2_rcbuffer_free_av_packet(PVOID pData) {
    if (!pData) return;
    qcap2_rcbuffer_av_packet_owner_t* pOwner = qcap2_container_of(pData, qcap2_rcbuffer_av_packet_owner_t, av_packet);
    qcap2_av_packet_free_buffer(&pOwner->av_packet);
    delete pOwner;
}

qcap2_rcbuffer_t* qcap2_rcbuffer_new_av_frame() {
    qcap2_rcbuffer_av_frame_owner_t* pOwner = new (std::nothrow) qcap2_rcbuffer_av_frame_owner_t();
    if (!pOwner) return NULL;

    pOwner->pOwner = pOwner;
    qcap2_av_frame_init(&pOwner->av_frame);

    qcap2_rcbuffer_t* pRCBuffer = qcap2_rcbuffer_new(&pOwner->av_frame, qcap2_rcbuffer_free_av_frame);
    if (!pRCBuffer) {
        delete pOwner;
    }
    return pRCBuffer;
}

qcap2_rcbuffer_t* qcap2_rcbuffer_new_av_packet() {
    qcap2_rcbuffer_av_packet_owner_t* pOwner = new (std::nothrow) qcap2_rcbuffer_av_packet_owner_t();
    if (!pOwner) return NULL;

    pOwner->pOwner = pOwner;
    qcap2_av_packet_init(&pOwner->av_packet);

    qcap2_rcbuffer_t* pRCBuffer = qcap2_rcbuffer_new(&pOwner->av_packet, qcap2_rcbuffer_free_av_packet);
    if (!pRCBuffer) {
        delete pOwner;
    }
    return pRCBuffer;
}

QRESULT qcap2_av_frame_set_dmabuf(qcap2_av_frame_t* pFrame, qcap2_dmabuf_t* pDMABuf) {
    if (!pFrame) return QCAP_RS_ERROR_INVALID_PARAMETER;
    qcap2_av_frame_priv_t* p = (qcap2_av_frame_priv_t*)pFrame;

    if (p->pDMABuf) {
        qcap2_av_frame_free_mapped_dmabuf(pFrame);
    }

    p->pDMABuf = pDMABuf;
    p->bOwnsDMABuf = false;
    return QCAP_RS_SUCCESSFUL;
}

QRESULT qcap2_av_frame_get_dmabuf(qcap2_av_frame_t* pFrame, qcap2_dmabuf_t** ppDMABuf) {
    if (!pFrame || !ppDMABuf) return QCAP_RS_ERROR_INVALID_PARAMETER;
    qcap2_av_frame_priv_t* p = (qcap2_av_frame_priv_t*)pFrame;
    *ppDMABuf = p->pDMABuf;
    return QCAP_RS_SUCCESSFUL;
}

QRESULT qcap2_av_frame_alloc_dmabuf(qcap2_av_frame_t* pFrame, int nSize, int nProt) {
    (void)nProt;
    if (!pFrame || nSize <= 0) return QCAP_RS_ERROR_INVALID_PARAMETER;
    qcap2_av_frame_priv_t* p = (qcap2_av_frame_priv_t*)pFrame;

    if (p->pDMABuf) {
        qcap2_av_frame_free_dmabuf(pFrame);
    }

    qcap2_dmabuf_t* pDMABuf = (qcap2_dmabuf_t*)calloc(1, sizeof(qcap2_dmabuf_t));
    if (!pDMABuf) return QCAP_RS_ERROR_OUT_OF_MEMORY;

    char pathTemplate[] = "/tmp/qcap2-dmabuf-XXXXXX";
    int fd = mkstemp(pathTemplate);
    if (fd < 0) {
        free(pDMABuf);
        return QCAP_RS_ERROR_OUT_OF_RESOURCE;
    }
    unlink(pathTemplate);

    if (ftruncate(fd, nSize) < 0) {
        close(fd);
        free(pDMABuf);
        return QCAP_RS_ERROR_GENERAL;
    }

    pDMABuf->fd = fd;
    pDMABuf->dmabuf_size = nSize;
    pDMABuf->pVirAddr = nullptr;
    pDMABuf->nPhyAddr = 0;
    pDMABuf->nSize = nSize;

    p->pDMABuf = pDMABuf;
    p->bOwnsDMABuf = true;
    return QCAP_RS_SUCCESSFUL;
}

QRESULT qcap2_av_frame_free_dmabuf(qcap2_av_frame_t* pFrame) {
    if (!pFrame) return QCAP_RS_ERROR_INVALID_PARAMETER;
    qcap2_av_frame_priv_t* p = (qcap2_av_frame_priv_t*)pFrame;
    if (!p->pDMABuf) return QCAP_RS_SUCCESSFUL;

    if (p->pDMABuf->pVirAddr) {
        qcap2_av_frame_unmap_dmabuf(pFrame);
    }

    if (p->pDMABuf->fd >= 0) {
        close(p->pDMABuf->fd);
        p->pDMABuf->fd = -1;
    }

    if (p->bOwnsDMABuf) {
        free(p->pDMABuf);
    }

    p->pDMABuf = nullptr;
    p->bOwnsDMABuf = false;
    return QCAP_RS_SUCCESSFUL;
}

QRESULT qcap2_av_frame_map_dmabuf(qcap2_av_frame_t* pFrame, int nProt) {
    if (!pFrame) return QCAP_RS_ERROR_INVALID_PARAMETER;
    qcap2_av_frame_priv_t* p = (qcap2_av_frame_priv_t*)pFrame;
    if (!p->pDMABuf || p->pDMABuf->fd < 0) return QCAP_RS_ERROR_GENERAL;

    if (p->pDMABuf->pVirAddr) return QCAP_RS_SUCCESSFUL;

    void* addr = mmap(nullptr, p->pDMABuf->dmabuf_size, nProt, MAP_SHARED, p->pDMABuf->fd, 0);
    if (addr == MAP_FAILED) {
        return QCAP_RS_ERROR_GENERAL;
    }

    p->pDMABuf->pVirAddr = addr;
    return QCAP_RS_SUCCESSFUL;
}

QRESULT qcap2_av_frame_unmap_dmabuf(qcap2_av_frame_t* pFrame) {
    if (!pFrame) return QCAP_RS_ERROR_INVALID_PARAMETER;
    qcap2_av_frame_priv_t* p = (qcap2_av_frame_priv_t*)pFrame;
    if (!p->pDMABuf) return QCAP_RS_SUCCESSFUL;
    if (!p->pDMABuf->pVirAddr) return QCAP_RS_SUCCESSFUL;

    if (munmap(p->pDMABuf->pVirAddr, p->pDMABuf->dmabuf_size) < 0) {
        return QCAP_RS_ERROR_GENERAL;
    }

    p->pDMABuf->pVirAddr = nullptr;
    return QCAP_RS_SUCCESSFUL;
}

QRESULT qcap2_av_frame_alloc_mapped_dmabuf(qcap2_av_frame_t* pFrame, int nSize, int nProt) {
    QRESULT res = qcap2_av_frame_alloc_dmabuf(pFrame, nSize, nProt);
    if (res != QCAP_RS_SUCCESSFUL) return res;

    res = qcap2_av_frame_map_dmabuf(pFrame, nProt);
    if (res != QCAP_RS_SUCCESSFUL) {
        qcap2_av_frame_free_dmabuf(pFrame);
        return res;
    }

    return QCAP_RS_SUCCESSFUL;
}

QRESULT qcap2_av_frame_free_mapped_dmabuf(qcap2_av_frame_t* pFrame) {
    return qcap2_av_frame_free_dmabuf(pFrame);
}

#ifdef __cplusplus
}
#endif
