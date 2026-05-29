#include "qcap2.buffer.h"
#include "qcap2.user.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <atomic>
#include <new>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _qcap2_rcbuffer_priv_t {
    PVOID pData;
    ULONG nDataSize;
    qcap2_on_free_resource_t pOnFreeResource;
    std::atomic<int32_t> use_count;
    std::atomic<int32_t> res_count;
    std::atomic<bool> resource_freed;
} qcap2_rcbuffer_priv_t;

// A simple internal definition to overlay on the opaque struct arrays.
typedef struct _qcap2_av_frame_priv_t {
    ULONG nColorSpaceType;
    ULONG nWidth;
    ULONG nHeight;

    ULONG nChannels;
    ULONG nSampleFmt;
    ULONG nSampleFrequency;
    ULONG nFrameSize;

    int nFieldType;
    double dSampleTime;
    int64_t nPTS;
    int64_t nPktPos;

    int64_t nVideoBits;
    int64_t nAudioBits;

    uint8_t* pBuffer[4];
    int pStride[4];
    bool bOwnsBuffer;
} qcap2_av_frame_priv_t;

typedef struct _qcap2_av_packet_priv_t {
    int nStreamIndex;
    BOOL bIsKeyFrame;
    double dSampleTime;
    int64_t nPTS;
    int64_t nDTS;

    uint8_t* pBuffer;
    int nSize;
    bool bOwnsBuffer;
} qcap2_av_packet_priv_t;

static int32_t qcap2_atomic_inc_if_positive(std::atomic<int32_t>& value) {
    int32_t n = value.load(std::memory_order_acquire);
    while (n > 0) {
        if (value.compare_exchange_weak(n, n + 1, std::memory_order_acq_rel, std::memory_order_acquire)) {
            return n + 1;
        }
    }
    return 0;
}

static int32_t qcap2_atomic_dec_if_positive(std::atomic<int32_t>& value) {
    int32_t n = value.load(std::memory_order_acquire);
    while (n > 0) {
        if (value.compare_exchange_weak(n, n - 1, std::memory_order_acq_rel, std::memory_order_acquire)) {
            return n - 1;
        }
    }
    return -1;
}

static void qcap2_rcbuffer_maybe_delete(qcap2_rcbuffer_priv_t* p) {
    if (p &&
        p->use_count.load(std::memory_order_acquire) == 0 &&
        p->res_count.load(std::memory_order_acquire) == 0) {
        delete p;
    }
}

static void qcap2_rcbuffer_release_resource(qcap2_rcbuffer_priv_t* p) {
    if (!p) return;

    int32_t nResCount = qcap2_atomic_dec_if_positive(p->res_count);
    if (nResCount == 0) {
        bool bExpected = false;
        if (p->resource_freed.compare_exchange_strong(bExpected, true, std::memory_order_acq_rel, std::memory_order_acquire)) {
            if (p->pOnFreeResource) {
                p->pOnFreeResource(p->pData);
            }
        }
    }

    qcap2_rcbuffer_maybe_delete(p);
}

// --- qcap2_rcbuffer_t ---

qcap2_rcbuffer_t* qcap2_rcbuffer_new(PVOID pData, qcap2_on_free_resource_t pOnFreeResource) {
    qcap2_rcbuffer_priv_t* p = new (std::nothrow) qcap2_rcbuffer_priv_t();
    if (p) {
        p->pData = pData;
        p->nDataSize = 0;
        p->pOnFreeResource = pOnFreeResource;
        p->use_count.store(1, std::memory_order_release);
        p->res_count.store(1, std::memory_order_release);
        p->resource_freed.store(false, std::memory_order_release);
    }
    return (qcap2_rcbuffer_t*)p;
}

void qcap2_rcbuffer_delete(qcap2_rcbuffer_t* pRCBuffer) {
    qcap2_rcbuffer_release(pRCBuffer);
}

void qcap2_rcbuffer_to_buffer(qcap2_rcbuffer_t* pRCBuffer, BYTE** ppBuffer, ULONG* pBufferSize) {
    if (ppBuffer) *ppBuffer = NULL;
    if (pBufferSize) *pBufferSize = 0;

    if (pRCBuffer) {
        qcap2_rcbuffer_priv_t* p = (qcap2_rcbuffer_priv_t*)pRCBuffer;
        if (ppBuffer) *ppBuffer = (BYTE*)p->pData;
        if (pBufferSize) *pBufferSize = p->nDataSize;
    }
}

qcap2_rcbuffer_t* qcap2_rcbuffer_cast(BYTE * pBuffer, ULONG nBufferLen) {
    qcap2_rcbuffer_priv_t* p = (qcap2_rcbuffer_priv_t*)qcap2_rcbuffer_new(pBuffer, NULL);
    if (p) {
        p->nDataSize = nBufferLen;
    }
    return (qcap2_rcbuffer_t*)p;
}

void qcap2_rcbuffer_add_ref(qcap2_rcbuffer_t* pRCBuffer) {
    if (pRCBuffer) {
        qcap2_rcbuffer_priv_t* p = (qcap2_rcbuffer_priv_t*)pRCBuffer;
        qcap2_atomic_inc_if_positive(p->use_count);
    }
}

void qcap2_rcbuffer_release(qcap2_rcbuffer_t* pRCBuffer) {
    if (pRCBuffer) {
        qcap2_rcbuffer_priv_t* p = (qcap2_rcbuffer_priv_t*)pRCBuffer;
        int32_t nUseCount = qcap2_atomic_dec_if_positive(p->use_count);
        if (nUseCount == 0) {
            qcap2_rcbuffer_release_resource(p);
        }
    }
}

PVOID qcap2_rcbuffer_lock_data(qcap2_rcbuffer_t* pRCBuffer) {
    if (pRCBuffer) {
        qcap2_rcbuffer_priv_t* p = (qcap2_rcbuffer_priv_t*)pRCBuffer;
        if (!p->resource_freed.load(std::memory_order_acquire) &&
            qcap2_atomic_inc_if_positive(p->res_count) > 0) {
            return p->pData;
        }
    }
    return NULL;
}

void qcap2_rcbuffer_unlock_data(qcap2_rcbuffer_t* pRCBuffer) {
    if (pRCBuffer) {
        qcap2_rcbuffer_release_resource((qcap2_rcbuffer_priv_t*)pRCBuffer);
    }
}

PVOID qcap2_rcbuffer_get_data(qcap2_rcbuffer_t* pRCBuffer) {
    if (pRCBuffer) {
        return ((qcap2_rcbuffer_priv_t*)pRCBuffer)->pData;
    }
    return NULL;
}

int32_t qcap2_rcbuffer_use_count(qcap2_rcbuffer_t* pRCBuffer) {
    if (pRCBuffer) {
        return ((qcap2_rcbuffer_priv_t*)pRCBuffer)->use_count.load(std::memory_order_acquire);
    }
    return 0;
}

int32_t qcap2_rcbuffer_res_count(qcap2_rcbuffer_t* pRCBuffer) {
    if (pRCBuffer) {
        return ((qcap2_rcbuffer_priv_t*)pRCBuffer)->res_count.load(std::memory_order_acquire);
    }
    return 0;
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

#ifdef __cplusplus
}
#endif
