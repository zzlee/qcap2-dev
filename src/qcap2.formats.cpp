#include "qcap2.formats.h"
#include "qcap2.buffer.h"
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _qcap2_input_format_priv_t {
    int dummy;
} qcap2_input_format_priv_t;

typedef struct _qcap2_video_format_priv_t {
    ULONG nColorSpaceType;
    ULONG nVideoWidth;
    ULONG nVideoHeight;
    BOOL bVideoIsInterleaved;
    double dVideoFrameRate;
    ULONG nInput;
} qcap2_video_format_priv_t;

typedef struct _qcap2_audio_format_priv_t {
    ULONG nAudioChannels;
    ULONG nAudioBitsPerSample;
    ULONG nAudioSampleFrequency;
    ULONG nInput;
} qcap2_audio_format_priv_t;

typedef struct _qcap2_video_encoder_property_priv_t {
    UINT nGpuNum;
    ULONG nEncoderType;
    ULONG nEncoderFormat;
    ULONG nColorSpaceType;
    ULONG nWidth;
    ULONG nHeight;
    double dFrameRate;
    ULONG nRecordProfile;
    ULONG nRecordLevel;
    ULONG nRecordEntropy;
    ULONG nRecordComplexity;
    ULONG nRecordMode;
    ULONG nQuality;
    ULONG nBitRate;
    ULONG nGOP;
    ULONG nBFrames;
    BOOL bIsInterleaved;
    ULONG nSlices;
    ULONG nLayers;
    ULONG nSceneCut;
    BOOL bMultiThread;
    BOOL bMBBRC;
    BOOL bExtBRC;
    ULONG nMinQP;
    ULONG nMaxQP;
    ULONG nVBVMaxRate;
    ULONG nVBVBufSize;
    ULONG nAspectRatioX;
    ULONG nAspectRatioY;
    ULONG nCBRVariation;
    ULONG nColorRange;
    double dSourceFrameRate;
    ULONG nRotation;
    BOOL bIdealTimestamp;
    BOOL bEncoderOSD;
    BOOL bEnableFF;
    BOOL bEnableLowDelay;
    BOOL bEnableRTMPCompatible;
    ULONG nTimeScaleFactor;
    BOOL bEnableHighPerf;
} qcap2_video_encoder_property_priv_t;

typedef struct _qcap2_audio_encoder_property_priv_t {
    ULONG nEncoderType;
    ULONG nEncoderFormat;
    ULONG nChannels;
    ULONG nBitsPerSample;
    ULONG nSampleFrequency;
    ULONG nBitRate;
    BOOL bIdealTimestamp;
} qcap2_audio_encoder_property_priv_t;

typedef struct _qcap2_video_encoder_dynamic_property_priv_t {
    ULONG nRecordMode;
    ULONG nQuality;
    ULONG nBitRate;
    ULONG nGOP;
} qcap2_video_encoder_dynamic_property_priv_t;

typedef struct _qcap2_media_info_priv_t {
    qcap2_video_format_t* pVideoFormat[16];
    qcap2_audio_format_t* pAudioFormat[16];
    qcap2_video_encoder_property_t* pVideoEncoder[16];
    qcap2_audio_encoder_property_t* pAudioEncoder[16];
    int nVideoCount;
    int nAudioCount;
    int nVideoEncoderCount;
    int nAudioEncoderCount;
} qcap2_media_info_priv_t;

typedef struct _qcap2_program_info_priv_t {
    int nId;
    int nNumber;
    int nVideoSourceCount;
    int nVideoSourceIndex[16];
    int nAudioSourceCount;
    int nAudioSourceIndex[16];
    int nVideoEncoderCount;
    int nVideoEncoderIndex[16];
    int nAudioEncoderCount;
    int nAudioEncoderIndex[16];
    int nVideoSinkCount;
    int nVideoSinkIndex[16];
    int nAudioSinkCount;
    int nAudioSinkIndex[16];
    int nVideoDecoderCount;
    int nVideoDecoderIndex[16];
    int nAudioDecoderCount;
    int nAudioDecoderIndex[16];
    char metadataKey[256];
    char metadataValue[256];
} qcap2_program_info_priv_t;

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif

// qcap2_input_format_t
qcap2_input_format_t* qcap2_input_format_new() {
    qcap2_input_format_priv_t* p = (qcap2_input_format_priv_t*)malloc(sizeof(qcap2_input_format_priv_t));
    if (p) memset(p, 0, sizeof(qcap2_input_format_priv_t));
    return (qcap2_input_format_t*)p;
}

void qcap2_input_format_delete(qcap2_input_format_t* pThis) {
    if (pThis) free(pThis);
}

// qcap2_video_format_t
qcap2_video_format_t* qcap2_video_format_new() {
    qcap2_video_format_priv_t* p = (qcap2_video_format_priv_t*)malloc(sizeof(qcap2_video_format_priv_t));
    if (p) memset(p, 0, sizeof(qcap2_video_format_priv_t));
    return (qcap2_video_format_t*)p;
}

void qcap2_video_format_delete(qcap2_video_format_t* pThis) {
    if (pThis) free(pThis);
}

// qcap2_audio_format_t
qcap2_audio_format_t* qcap2_audio_format_new() {
    qcap2_audio_format_priv_t* p = (qcap2_audio_format_priv_t*)malloc(sizeof(qcap2_audio_format_priv_t));
    if (p) memset(p, 0, sizeof(qcap2_audio_format_priv_t));
    return (qcap2_audio_format_t*)p;
}

void qcap2_audio_format_delete(qcap2_audio_format_t* pThis) {
    if (pThis) free(pThis);
}

// qcap2_video_encoder_property_t
qcap2_video_encoder_property_t* qcap2_video_encoder_property_new() {
    qcap2_video_encoder_property_priv_t* p = (qcap2_video_encoder_property_priv_t*)malloc(sizeof(qcap2_video_encoder_property_priv_t));
    if (p) memset(p, 0, sizeof(qcap2_video_encoder_property_priv_t));
    return (qcap2_video_encoder_property_t*)p;
}

void qcap2_video_encoder_property_delete(qcap2_video_encoder_property_t* pThis) {
    if (pThis) free(pThis);
}

// qcap2_audio_encoder_property_t
qcap2_audio_encoder_property_t* qcap2_audio_encoder_property_new() {
    qcap2_audio_encoder_property_priv_t* p = (qcap2_audio_encoder_property_priv_t*)malloc(sizeof(qcap2_audio_encoder_property_priv_t));
    if (p) memset(p, 0, sizeof(qcap2_audio_encoder_property_priv_t));
    return (qcap2_audio_encoder_property_t*)p;
}

void qcap2_audio_encoder_property_delete(qcap2_audio_encoder_property_t* pThis) {
    if (pThis) free(pThis);
}

// qcap2_video_encoder_dynamic_property_t
qcap2_video_encoder_dynamic_property_t* qcap2_video_encoder_dynamic_property_new() {
    qcap2_video_encoder_dynamic_property_priv_t* p = (qcap2_video_encoder_dynamic_property_priv_t*)malloc(sizeof(qcap2_video_encoder_dynamic_property_priv_t));
    if (p) memset(p, 0, sizeof(qcap2_video_encoder_dynamic_property_priv_t));
    return (qcap2_video_encoder_dynamic_property_t*)p;
}

void qcap2_video_encoder_dynamic_property_delete(qcap2_video_encoder_dynamic_property_t* pThis) {
    if (pThis) free(pThis);
}

// qcap2_program_info_t
qcap2_program_info_t* qcap2_program_info_new() {
    qcap2_program_info_priv_t* p = (qcap2_program_info_priv_t*)malloc(sizeof(qcap2_program_info_priv_t));
    if (p) memset(p, 0, sizeof(qcap2_program_info_priv_t));
    return (qcap2_program_info_t*)p;
}

void qcap2_program_info_delete(qcap2_program_info_t* pThis) {
    if (pThis) free(pThis);
}

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif

// qcap2_video_format_t properties
void qcap2_video_format_get_property(qcap2_video_format_t* pThis, ULONG * pColorSpaceType, ULONG * pVideoWidth, ULONG * pVideoHeight, BOOL * pVideoIsInterleaved, double * pVideoFrameRate) {
    if (!pThis) return;
    qcap2_video_format_priv_t* p = (qcap2_video_format_priv_t*)pThis;
    if (pColorSpaceType) *pColorSpaceType = p->nColorSpaceType;
    if (pVideoWidth) *pVideoWidth = p->nVideoWidth;
    if (pVideoHeight) *pVideoHeight = p->nVideoHeight;
    if (pVideoIsInterleaved) *pVideoIsInterleaved = p->bVideoIsInterleaved;
    if (pVideoFrameRate) *pVideoFrameRate = p->dVideoFrameRate;
}

void qcap2_video_format_set_property(qcap2_video_format_t* pThis, ULONG nColorSpaceType, ULONG nVideoWidth, ULONG nVideoHeight, BOOL bVideoIsInterleaved, double dVideoFrameRate) {
    if (!pThis) return;
    qcap2_video_format_priv_t* p = (qcap2_video_format_priv_t*)pThis;
    p->nColorSpaceType = nColorSpaceType;
    p->nVideoWidth = nVideoWidth;
    p->nVideoHeight = nVideoHeight;
    p->bVideoIsInterleaved = bVideoIsInterleaved;
    p->dVideoFrameRate = dVideoFrameRate;
}

void qcap2_video_format_get_input(qcap2_video_format_t* pThis, ULONG * pInput) {
    if (!pThis) return;
    qcap2_video_format_priv_t* p = (qcap2_video_format_priv_t*)pThis;
    if (pInput) *pInput = p->nInput;
}

void qcap2_video_format_set_input(qcap2_video_format_t* pThis, ULONG nInput) {
    if (!pThis) return;
    qcap2_video_format_priv_t* p = (qcap2_video_format_priv_t*)pThis;
    p->nInput = nInput;
}

// qcap2_audio_format_t properties
void qcap2_audio_format_get_property(qcap2_audio_format_t* pThis, ULONG * pAudioChannels, ULONG * pAudioBitsPerSample, ULONG * pAudioSampleFrequency) {
    if (!pThis) return;
    qcap2_audio_format_priv_t* p = (qcap2_audio_format_priv_t*)pThis;
    if (pAudioChannels) *pAudioChannels = p->nAudioChannels;
    if (pAudioBitsPerSample) *pAudioBitsPerSample = p->nAudioBitsPerSample;
    if (pAudioSampleFrequency) *pAudioSampleFrequency = p->nAudioSampleFrequency;
}

void qcap2_audio_format_set_property(qcap2_audio_format_t* pThis, ULONG nAudioChannels, ULONG nAudioBitsPerSample, ULONG nAudioSampleFrequency) {
    if (!pThis) return;
    qcap2_audio_format_priv_t* p = (qcap2_audio_format_priv_t*)pThis;
    p->nAudioChannels = nAudioChannels;
    p->nAudioBitsPerSample = nAudioBitsPerSample;
    p->nAudioSampleFrequency = nAudioSampleFrequency;
}

void qcap2_audio_format_get_input(qcap2_audio_format_t* pThis, ULONG * pInput) {
    if (!pThis) return;
    qcap2_audio_format_priv_t* p = (qcap2_audio_format_priv_t*)pThis;
    if (pInput) *pInput = p->nInput;
}

void qcap2_audio_format_set_input(qcap2_audio_format_t* pThis, ULONG nInput) {
    if (!pThis) return;
    qcap2_audio_format_priv_t* p = (qcap2_audio_format_priv_t*)pThis;
    p->nInput = nInput;
}

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif

// qcap2_video_encoder_property_t properties
void qcap2_video_encoder_property_get_property(qcap2_video_encoder_property_t* pThis, ULONG * pEncoderType, ULONG * pEncoderFormat, ULONG * pColorSpaceType, ULONG * pWidth, ULONG * pHeight, double * pFrameRate, ULONG * pRecordMode, ULONG * pQuality, ULONG * pBitRate, ULONG * pGOP, ULONG * pAspectRatioX, ULONG * pAspectRatioY) {
    if (!pThis) return;
    qcap2_video_encoder_property_priv_t* p = (qcap2_video_encoder_property_priv_t*)pThis;
    if (pEncoderType) *pEncoderType = p->nEncoderType;
    if (pEncoderFormat) *pEncoderFormat = p->nEncoderFormat;
    if (pColorSpaceType) *pColorSpaceType = p->nColorSpaceType;
    if (pWidth) *pWidth = p->nWidth;
    if (pHeight) *pHeight = p->nHeight;
    if (pFrameRate) *pFrameRate = p->dFrameRate;
    if (pRecordMode) *pRecordMode = p->nRecordMode;
    if (pQuality) *pQuality = p->nQuality;
    if (pBitRate) *pBitRate = p->nBitRate;
    if (pGOP) *pGOP = p->nGOP;
    if (pAspectRatioX) *pAspectRatioX = p->nAspectRatioX;
    if (pAspectRatioY) *pAspectRatioY = p->nAspectRatioY;
}

void qcap2_video_encoder_property_set_property(qcap2_video_encoder_property_t* pThis, ULONG nEncoderType, ULONG nEncoderFormat, ULONG nColorSpaceType, ULONG nWidth, ULONG nHeight, double dFrameRate, ULONG nRecordMode, ULONG nQuality, ULONG nBitRate, ULONG nGOP, ULONG nAspectRatioX, ULONG nAspectRatioY) {
    if (!pThis) return;
    qcap2_video_encoder_property_priv_t* p = (qcap2_video_encoder_property_priv_t*)pThis;
    p->nEncoderType = nEncoderType;
    p->nEncoderFormat = nEncoderFormat;
    p->nColorSpaceType = nColorSpaceType;
    p->nWidth = nWidth;
    p->nHeight = nHeight;
    p->dFrameRate = dFrameRate;
    p->nRecordMode = nRecordMode;
    p->nQuality = nQuality;
    p->nBitRate = nBitRate;
    p->nGOP = nGOP;
    p->nAspectRatioX = nAspectRatioX;
    p->nAspectRatioY = nAspectRatioY;
}

void qcap2_video_encoder_property_get_property1(qcap2_video_encoder_property_t* pThis, UINT* pGpuNum, ULONG * pEncoderType, ULONG * pEncoderFormat, ULONG * pColorSpaceType, ULONG * pWidth, ULONG * pHeight, double * pFrameRate, ULONG * pRecordProfile, ULONG * pRecordLevel, ULONG * pRecordEntropy, ULONG * pRecordComplexity, ULONG * pRecordMode, ULONG * pQuality, ULONG * pBitRate, ULONG * pGOP, ULONG * pBFrames, BOOL * pIsInterleaved, ULONG * pSlices, ULONG * pLayers, ULONG * pSceneCut, BOOL * pMultiThread, BOOL * pMBBRC, BOOL * pExtBRC, ULONG * pMinQP, ULONG * pMaxQP, ULONG * pVBVMaxRate, ULONG * pVBVBufSize, ULONG * pAspectRatioX, ULONG * pAspectRatioY) {
    if (!pThis) return;
    qcap2_video_encoder_property_priv_t* p = (qcap2_video_encoder_property_priv_t*)pThis;
    if (pGpuNum) *pGpuNum = p->nGpuNum;
    if (pEncoderType) *pEncoderType = p->nEncoderType;
    if (pEncoderFormat) *pEncoderFormat = p->nEncoderFormat;
    if (pColorSpaceType) *pColorSpaceType = p->nColorSpaceType;
    if (pWidth) *pWidth = p->nWidth;
    if (pHeight) *pHeight = p->nHeight;
    if (pFrameRate) *pFrameRate = p->dFrameRate;
    if (pRecordProfile) *pRecordProfile = p->nRecordProfile;
    if (pRecordLevel) *pRecordLevel = p->nRecordLevel;
    if (pRecordEntropy) *pRecordEntropy = p->nRecordEntropy;
    if (pRecordComplexity) *pRecordComplexity = p->nRecordComplexity;
    if (pRecordMode) *pRecordMode = p->nRecordMode;
    if (pQuality) *pQuality = p->nQuality;
    if (pBitRate) *pBitRate = p->nBitRate;
    if (pGOP) *pGOP = p->nGOP;
    if (pBFrames) *pBFrames = p->nBFrames;
    if (pIsInterleaved) *pIsInterleaved = p->bIsInterleaved;
    if (pSlices) *pSlices = p->nSlices;
    if (pLayers) *pLayers = p->nLayers;
    if (pSceneCut) *pSceneCut = p->nSceneCut;
    if (pMultiThread) *pMultiThread = p->bMultiThread;
    if (pMBBRC) *pMBBRC = p->bMBBRC;
    if (pExtBRC) *pExtBRC = p->bExtBRC;
    if (pMinQP) *pMinQP = p->nMinQP;
    if (pMaxQP) *pMaxQP = p->nMaxQP;
    if (pVBVMaxRate) *pVBVMaxRate = p->nVBVMaxRate;
    if (pVBVBufSize) *pVBVBufSize = p->nVBVBufSize;
    if (pAspectRatioX) *pAspectRatioX = p->nAspectRatioX;
    if (pAspectRatioY) *pAspectRatioY = p->nAspectRatioY;
}

void qcap2_video_encoder_property_set_property1(qcap2_video_encoder_property_t* pThis, UINT nGpuNum, ULONG nEncoderType, ULONG nEncoderFormat, ULONG nColorSpaceType, ULONG nWidth, ULONG nHeight, double dFrameRate, ULONG nRecordProfile, ULONG nRecordLevel, ULONG nRecordEntropy, ULONG nRecordComplexity, ULONG nRecordMode, ULONG nQuality, ULONG nBitRate, ULONG nGOP, ULONG nBFrames, BOOL bIsInterleaved, ULONG nSlices, ULONG nLayers, ULONG nSceneCut, BOOL bMultiThread, BOOL bMBBRC, BOOL bExtBRC, ULONG nMinQP, ULONG nMaxQP, ULONG nVBVMaxRate, ULONG nVBVBufSize, ULONG nAspectRatioX, ULONG nAspectRatioY) {
    if (!pThis) return;
    qcap2_video_encoder_property_priv_t* p = (qcap2_video_encoder_property_priv_t*)pThis;
    p->nGpuNum = nGpuNum;
    p->nEncoderType = nEncoderType;
    p->nEncoderFormat = nEncoderFormat;
    p->nColorSpaceType = nColorSpaceType;
    p->nWidth = nWidth;
    p->nHeight = nHeight;
    p->dFrameRate = dFrameRate;
    p->nRecordProfile = nRecordProfile;
    p->nRecordLevel = nRecordLevel;
    p->nRecordEntropy = nRecordEntropy;
    p->nRecordComplexity = nRecordComplexity;
    p->nRecordMode = nRecordMode;
    p->nQuality = nQuality;
    p->nBitRate = nBitRate;
    p->nGOP = nGOP;
    p->nBFrames = nBFrames;
    p->bIsInterleaved = bIsInterleaved;
    p->nSlices = nSlices;
    p->nLayers = nLayers;
    p->nSceneCut = nSceneCut;
    p->bMultiThread = bMultiThread;
    p->bMBBRC = bMBBRC;
    p->bExtBRC = bExtBRC;
    p->nMinQP = nMinQP;
    p->nMaxQP = nMaxQP;
    p->nVBVMaxRate = nVBVMaxRate;
    p->nVBVBufSize = nVBVBufSize;
    p->nAspectRatioX = nAspectRatioX;
    p->nAspectRatioY = nAspectRatioY;
}

void qcap2_video_encoder_property_get_type(qcap2_video_encoder_property_t* pThis, ULONG * pEncoderType) {
    if (!pThis) return;
    qcap2_video_encoder_property_priv_t* p = (qcap2_video_encoder_property_priv_t*)pThis;
    if (pEncoderType) *pEncoderType = p->nEncoderType;
}

void qcap2_video_encoder_property_set_type(qcap2_video_encoder_property_t* pThis, ULONG nEncoderType) {
    if (!pThis) return;
    qcap2_video_encoder_property_priv_t* p = (qcap2_video_encoder_property_priv_t*)pThis;
    p->nEncoderType = nEncoderType;
}

void qcap2_video_encoder_property_get_format(qcap2_video_encoder_property_t* pThis, ULONG * pEncoderFormat) {
    if (!pThis) return;
    qcap2_video_encoder_property_priv_t* p = (qcap2_video_encoder_property_priv_t*)pThis;
    if (pEncoderFormat) *pEncoderFormat = p->nEncoderFormat;
}

void qcap2_video_encoder_property_set_format(qcap2_video_encoder_property_t* pThis, ULONG nEncoderFormat) {
    if (!pThis) return;
    qcap2_video_encoder_property_priv_t* p = (qcap2_video_encoder_property_priv_t*)pThis;
    p->nEncoderFormat = nEncoderFormat;
}

void qcap2_video_encoder_property_get_resolution(qcap2_video_encoder_property_t* pThis, ULONG * pWidth, ULONG * pHeight) {
    if (!pThis) return;
    qcap2_video_encoder_property_priv_t* p = (qcap2_video_encoder_property_priv_t*)pThis;
    if (pWidth) *pWidth = p->nWidth;
    if (pHeight) *pHeight = p->nHeight;
}

void qcap2_video_encoder_property_set_resolution(qcap2_video_encoder_property_t* pThis, ULONG nWidth, ULONG nHeight) {
    if (!pThis) return;
    qcap2_video_encoder_property_priv_t* p = (qcap2_video_encoder_property_priv_t*)pThis;
    p->nWidth = nWidth;
    p->nHeight = nHeight;
}

void qcap2_video_encoder_property_get_bitrate(qcap2_video_encoder_property_t* pThis, ULONG * pBitRate) {
    if (!pThis) return;
    qcap2_video_encoder_property_priv_t* p = (qcap2_video_encoder_property_priv_t*)pThis;
    if (pBitRate) *pBitRate = p->nBitRate;
}

void qcap2_video_encoder_property_set_bitrate(qcap2_video_encoder_property_t* pThis, ULONG nBitRate) {
    if (!pThis) return;
    qcap2_video_encoder_property_priv_t* p = (qcap2_video_encoder_property_priv_t*)pThis;
    p->nBitRate = nBitRate;
}

void qcap2_video_encoder_property_set_cbr_variation(qcap2_video_encoder_property_t* pThis, ULONG nCBRVariation) {
    if (!pThis) return;
    qcap2_video_encoder_property_priv_t* p = (qcap2_video_encoder_property_priv_t*)pThis;
    p->nCBRVariation = nCBRVariation;
}

void qcap2_video_encoder_property_get_cbr_variation(qcap2_video_encoder_property_t* pThis, ULONG* pCBRVariation) {
    if (!pThis) return;
    qcap2_video_encoder_property_priv_t* p = (qcap2_video_encoder_property_priv_t*)pThis;
    if (pCBRVariation) *pCBRVariation = p->nCBRVariation;
}

void qcap2_video_encoder_property_set_color_range(qcap2_video_encoder_property_t* pThis, ULONG nColorRange) {
    if (!pThis) return;
    qcap2_video_encoder_property_priv_t* p = (qcap2_video_encoder_property_priv_t*)pThis;
    p->nColorRange = nColorRange;
}

void qcap2_video_encoder_property_get_color_range(qcap2_video_encoder_property_t* pThis, ULONG* pColorRange) {
    if (!pThis) return;
    qcap2_video_encoder_property_priv_t* p = (qcap2_video_encoder_property_priv_t*)pThis;
    if (pColorRange) *pColorRange = p->nColorRange;
}

void qcap2_video_encoder_property_set_source_frame_rate(qcap2_video_encoder_property_t* pThis, double dSourceFrameRate) {
    if (!pThis) return;
    qcap2_video_encoder_property_priv_t* p = (qcap2_video_encoder_property_priv_t*)pThis;
    p->dSourceFrameRate = dSourceFrameRate;
}

void qcap2_video_encoder_property_get_source_frame_rate(qcap2_video_encoder_property_t* pThis, double* pSourceFrameRate) {
    if (!pThis) return;
    qcap2_video_encoder_property_priv_t* p = (qcap2_video_encoder_property_priv_t*)pThis;
    if (pSourceFrameRate) *pSourceFrameRate = p->dSourceFrameRate;
}

void qcap2_video_encoder_property_set_rotation(qcap2_video_encoder_property_t* pThis, ULONG nRotation) {
    if (!pThis) return;
    qcap2_video_encoder_property_priv_t* p = (qcap2_video_encoder_property_priv_t*)pThis;
    p->nRotation = nRotation;
}

void qcap2_video_encoder_property_get_rotation(qcap2_video_encoder_property_t* pThis, ULONG* pRotation) {
    if (!pThis) return;
    qcap2_video_encoder_property_priv_t* p = (qcap2_video_encoder_property_priv_t*)pThis;
    if (pRotation) *pRotation = p->nRotation;
}

void qcap2_video_encoder_property_set_ideal_timestamp(qcap2_video_encoder_property_t* pThis, BOOL bIdealTimestamp) {
    if (!pThis) return;
    qcap2_video_encoder_property_priv_t* p = (qcap2_video_encoder_property_priv_t*)pThis;
    p->bIdealTimestamp = bIdealTimestamp;
}

void qcap2_video_encoder_property_get_ideal_timestamp(qcap2_video_encoder_property_t* pThis, BOOL* pIdealTimestamp) {
    if (!pThis) return;
    qcap2_video_encoder_property_priv_t* p = (qcap2_video_encoder_property_priv_t*)pThis;
    if (pIdealTimestamp) *pIdealTimestamp = p->bIdealTimestamp;
}

void qcap2_video_encoder_property_set_encoder_osd(qcap2_video_encoder_property_t* pThis, BOOL bEncoderOSD) {
    if (!pThis) return;
    qcap2_video_encoder_property_priv_t* p = (qcap2_video_encoder_property_priv_t*)pThis;
    p->bEncoderOSD = bEncoderOSD;
}

void qcap2_video_encoder_property_get_encoder_osd(qcap2_video_encoder_property_t* pThis, BOOL* pEncoderOSD) {
    if (!pThis) return;
    qcap2_video_encoder_property_priv_t* p = (qcap2_video_encoder_property_priv_t*)pThis;
    if (pEncoderOSD) *pEncoderOSD = p->bEncoderOSD;
}

void qcap2_video_encoder_property_set_fixed_function(qcap2_video_encoder_property_t* pThis, BOOL bEnableFF) {
    if (!pThis) return;
    qcap2_video_encoder_property_priv_t* p = (qcap2_video_encoder_property_priv_t*)pThis;
    p->bEnableFF = bEnableFF;
}

void qcap2_video_encoder_property_get_fixed_function(qcap2_video_encoder_property_t* pThis, BOOL* pEnableFF) {
    if (!pThis) return;
    qcap2_video_encoder_property_priv_t* p = (qcap2_video_encoder_property_priv_t*)pThis;
    if (pEnableFF) *pEnableFF = p->bEnableFF;
}

void qcap2_video_encoder_property_set_low_delay(qcap2_video_encoder_property_t* pThis, BOOL bEnableLowDelay) {
    if (!pThis) return;
    qcap2_video_encoder_property_priv_t* p = (qcap2_video_encoder_property_priv_t*)pThis;
    p->bEnableLowDelay = bEnableLowDelay;
}

void qcap2_video_encoder_property_get_low_delay(qcap2_video_encoder_property_t* pThis, BOOL* pEnableLowDelay) {
    if (!pThis) return;
    qcap2_video_encoder_property_priv_t* p = (qcap2_video_encoder_property_priv_t*)pThis;
    if (pEnableLowDelay) *pEnableLowDelay = p->bEnableLowDelay;
}

void qcap2_video_encoder_property_set_rtmp_compat(qcap2_video_encoder_property_t* pThis, BOOL bEnableRTMPCompatible) {
    if (!pThis) return;
    qcap2_video_encoder_property_priv_t* p = (qcap2_video_encoder_property_priv_t*)pThis;
    p->bEnableRTMPCompatible = bEnableRTMPCompatible;
}

void qcap2_video_encoder_property_get_rtmp_compat(qcap2_video_encoder_property_t* pThis, BOOL* pEnableRTMPCompatible) {
    if (!pThis) return;
    qcap2_video_encoder_property_priv_t* p = (qcap2_video_encoder_property_priv_t*)pThis;
    if (pEnableRTMPCompatible) *pEnableRTMPCompatible = p->bEnableRTMPCompatible;
}

void qcap2_video_encoder_property_set_time_scale_factor(qcap2_video_encoder_property_t* pThis, ULONG nTimeScaleFactor) {
    if (!pThis) return;
    qcap2_video_encoder_property_priv_t* p = (qcap2_video_encoder_property_priv_t*)pThis;
    p->nTimeScaleFactor = nTimeScaleFactor;
}

void qcap2_video_encoder_property_get_time_scale_factor(qcap2_video_encoder_property_t* pThis, ULONG* pTimeScaleFactor) {
    if (!pThis) return;
    qcap2_video_encoder_property_priv_t* p = (qcap2_video_encoder_property_priv_t*)pThis;
    if (pTimeScaleFactor) *pTimeScaleFactor = p->nTimeScaleFactor;
}

void qcap2_video_encoder_property_set_high_perf(qcap2_video_encoder_property_t* pThis, BOOL bEnableHighPerf) {
    if (!pThis) return;
    qcap2_video_encoder_property_priv_t* p = (qcap2_video_encoder_property_priv_t*)pThis;
    p->bEnableHighPerf = bEnableHighPerf;
}

void qcap2_video_encoder_property_get_high_perf(qcap2_video_encoder_property_t* pThis, BOOL* pEnableHighPerf) {
    if (!pThis) return;
    qcap2_video_encoder_property_priv_t* p = (qcap2_video_encoder_property_priv_t*)pThis;
    if (pEnableHighPerf) *pEnableHighPerf = p->bEnableHighPerf;
}


// qcap2_audio_encoder_property_t properties
void qcap2_audio_encoder_property_get_property(qcap2_audio_encoder_property_t* pThis, ULONG * pEncoderType, ULONG * pEncoderFormat, ULONG * pChannels, ULONG * pBitsPerSample, ULONG * pSampleFrequency) {
    if (!pThis) return;
    qcap2_audio_encoder_property_priv_t* p = (qcap2_audio_encoder_property_priv_t*)pThis;
    if (pEncoderType) *pEncoderType = p->nEncoderType;
    if (pEncoderFormat) *pEncoderFormat = p->nEncoderFormat;
    if (pChannels) *pChannels = p->nChannels;
    if (pBitsPerSample) *pBitsPerSample = p->nBitsPerSample;
    if (pSampleFrequency) *pSampleFrequency = p->nSampleFrequency;
}

void qcap2_audio_encoder_property_set_property(qcap2_audio_encoder_property_t* pThis, ULONG nEncoderType, ULONG nEncoderFormat, ULONG nChannels, ULONG nBitsPerSample, ULONG nSampleFrequency) {
    if (!pThis) return;
    qcap2_audio_encoder_property_priv_t* p = (qcap2_audio_encoder_property_priv_t*)pThis;
    p->nEncoderType = nEncoderType;
    p->nEncoderFormat = nEncoderFormat;
    p->nChannels = nChannels;
    p->nBitsPerSample = nBitsPerSample;
    p->nSampleFrequency = nSampleFrequency;
}

void qcap2_audio_encoder_property_get_property1(qcap2_audio_encoder_property_t* pThis, ULONG * pEncoderType, ULONG * pEncoderFormat, ULONG * pChannels, ULONG * pBitsPerSample, ULONG * pSampleFrequency, ULONG * pBitRate) {
    if (!pThis) return;
    qcap2_audio_encoder_property_priv_t* p = (qcap2_audio_encoder_property_priv_t*)pThis;
    if (pEncoderType) *pEncoderType = p->nEncoderType;
    if (pEncoderFormat) *pEncoderFormat = p->nEncoderFormat;
    if (pChannels) *pChannels = p->nChannels;
    if (pBitsPerSample) *pBitsPerSample = p->nBitsPerSample;
    if (pSampleFrequency) *pSampleFrequency = p->nSampleFrequency;
    if (pBitRate) *pBitRate = p->nBitRate;
}

void qcap2_audio_encoder_property_set_property1(qcap2_audio_encoder_property_t* pThis, ULONG nEncoderType, ULONG nEncoderFormat, ULONG nChannels, ULONG nBitsPerSample, ULONG nSampleFrequency, ULONG nBitRate) {
    if (!pThis) return;
    qcap2_audio_encoder_property_priv_t* p = (qcap2_audio_encoder_property_priv_t*)pThis;
    p->nEncoderType = nEncoderType;
    p->nEncoderFormat = nEncoderFormat;
    p->nChannels = nChannels;
    p->nBitsPerSample = nBitsPerSample;
    p->nSampleFrequency = nSampleFrequency;
    p->nBitRate = nBitRate;
}

void qcap2_audio_encoder_property_set_ideal_timestamp(qcap2_audio_encoder_property_t* pThis, BOOL bIdealTimestamp) {
    if (!pThis) return;
    qcap2_audio_encoder_property_priv_t* p = (qcap2_audio_encoder_property_priv_t*)pThis;
    p->bIdealTimestamp = bIdealTimestamp;
}

void qcap2_audio_encoder_property_get_ideal_timestamp(qcap2_audio_encoder_property_t* pThis, BOOL* pIdealTimestamp) {
    if (!pThis) return;
    qcap2_audio_encoder_property_priv_t* p = (qcap2_audio_encoder_property_priv_t*)pThis;
    if (pIdealTimestamp) *pIdealTimestamp = p->bIdealTimestamp;
}

// qcap2_video_encoder_dynamic_property_t properties
void qcap2_video_encoder_dynamic_get_property(qcap2_video_encoder_dynamic_property_t* pThis, ULONG * pRecordMode, ULONG * pQuality, ULONG * pBitRate, ULONG * pGOP) {
    if (!pThis) return;
    qcap2_video_encoder_dynamic_property_priv_t* p = (qcap2_video_encoder_dynamic_property_priv_t*)pThis;
    if (pRecordMode) *pRecordMode = p->nRecordMode;
    if (pQuality) *pQuality = p->nQuality;
    if (pBitRate) *pBitRate = p->nBitRate;
    if (pGOP) *pGOP = p->nGOP;
}

void qcap2_video_encoder_dynamic_set_property(qcap2_video_encoder_dynamic_property_t* pThis, ULONG nRecordMode, ULONG nQuality, ULONG nBitRate, ULONG nGOP) {
    if (!pThis) return;
    qcap2_video_encoder_dynamic_property_priv_t* p = (qcap2_video_encoder_dynamic_property_priv_t*)pThis;
    p->nRecordMode = nRecordMode;
    p->nQuality = nQuality;
    p->nBitRate = nBitRate;
    p->nGOP = nGOP;
}


// qcap2_media_info_t properties
qcap2_media_info_t* qcap2_media_info_lock_from(qcap2_rcbuffer_t* pMediaInfo) {
    if (!pMediaInfo) return NULL;
    return (qcap2_media_info_t*)qcap2_rcbuffer_lock_data(pMediaInfo);
}

int qcap2_media_info_get_video_count(qcap2_media_info_t* pThis) {
    if (!pThis) return 0;
    qcap2_media_info_priv_t* p = (qcap2_media_info_priv_t*)pThis;
    return p->nVideoCount;
}

int qcap2_media_info_get_audio_count(qcap2_media_info_t* pThis) {
    if (!pThis) return 0;
    qcap2_media_info_priv_t* p = (qcap2_media_info_priv_t*)pThis;
    return p->nAudioCount;
}

int qcap2_media_info_get_video_encoder_count(qcap2_media_info_t* pThis) {
    if (!pThis) return 0;
    qcap2_media_info_priv_t* p = (qcap2_media_info_priv_t*)pThis;
    return p->nVideoEncoderCount;
}

int qcap2_media_info_get_audio_encoder_count(qcap2_media_info_t* pThis) {
    if (!pThis) return 0;
    qcap2_media_info_priv_t* p = (qcap2_media_info_priv_t*)pThis;
    return p->nAudioEncoderCount;
}

qcap2_video_format_t* qcap2_media_info_get_video_format(qcap2_media_info_t* pThis, int nIndex) {
    if (!pThis || nIndex < 0 || nIndex >= 16) return NULL;
    qcap2_media_info_priv_t* p = (qcap2_media_info_priv_t*)pThis;
    return p->pVideoFormat[nIndex];
}

qcap2_audio_format_t* qcap2_media_info_get_audio_format(qcap2_media_info_t* pThis, int nIndex) {
    if (!pThis || nIndex < 0 || nIndex >= 16) return NULL;
    qcap2_media_info_priv_t* p = (qcap2_media_info_priv_t*)pThis;
    return p->pAudioFormat[nIndex];
}

qcap2_video_encoder_property_t* qcap2_media_info_get_video_encoder_property(qcap2_media_info_t* pThis, int nIndex) {
    if (!pThis || nIndex < 0 || nIndex >= 16) return NULL;
    qcap2_media_info_priv_t* p = (qcap2_media_info_priv_t*)pThis;
    return p->pVideoEncoder[nIndex];
}

qcap2_audio_encoder_property_t* qcap2_media_info_get_audio_encoder_property(qcap2_media_info_t* pThis, int nIndex) {
    if (!pThis || nIndex < 0 || nIndex >= 16) return NULL;
    qcap2_media_info_priv_t* p = (qcap2_media_info_priv_t*)pThis;
    return p->pAudioEncoder[nIndex];
}

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif

// qcap2_program_info_t properties
int qcap2_program_info_get_id(qcap2_program_info_t* pThis) {
    if (!pThis) return 0;
    qcap2_program_info_priv_t* p = (qcap2_program_info_priv_t*)pThis;
    return p->nId;
}

void qcap2_program_info_set_id(qcap2_program_info_t* pThis, int nId) {
    if (!pThis) return;
    qcap2_program_info_priv_t* p = (qcap2_program_info_priv_t*)pThis;
    p->nId = nId;
}

int qcap2_program_info_get_number(qcap2_program_info_t* pThis) {
    if (!pThis) return 0;
    qcap2_program_info_priv_t* p = (qcap2_program_info_priv_t*)pThis;
    return p->nNumber;
}

void qcap2_program_info_set_number(qcap2_program_info_t* pThis, int nNumber) {
    if (!pThis) return;
    qcap2_program_info_priv_t* p = (qcap2_program_info_priv_t*)pThis;
    p->nNumber = nNumber;
}

int qcap2_program_info_get_video_source_count(qcap2_program_info_t* pThis) {
    if (!pThis) return 0;
    qcap2_program_info_priv_t* p = (qcap2_program_info_priv_t*)pThis;
    return p->nVideoSourceCount;
}

void qcap2_program_info_set_video_source_count(qcap2_program_info_t* pThis, int nCount) {
    if (!pThis) return;
    qcap2_program_info_priv_t* p = (qcap2_program_info_priv_t*)pThis;
    p->nVideoSourceCount = nCount;
}

int qcap2_program_info_get_video_source_index(qcap2_program_info_t* pThis, int nIndex) {
    if (!pThis || nIndex < 0 || nIndex >= 16) return -1;
    qcap2_program_info_priv_t* p = (qcap2_program_info_priv_t*)pThis;
    return p->nVideoSourceIndex[nIndex];
}

void qcap2_program_info_set_video_source_index(qcap2_program_info_t* pThis, int nIndex, int nValue) {
    if (!pThis || nIndex < 0 || nIndex >= 16) return;
    qcap2_program_info_priv_t* p = (qcap2_program_info_priv_t*)pThis;
    p->nVideoSourceIndex[nIndex] = nValue;
}

int qcap2_program_info_get_audio_source_count(qcap2_program_info_t* pThis) {
    if (!pThis) return 0;
    qcap2_program_info_priv_t* p = (qcap2_program_info_priv_t*)pThis;
    return p->nAudioSourceCount;
}

void qcap2_program_info_set_audio_source_count(qcap2_program_info_t* pThis, int nCount) {
    if (!pThis) return;
    qcap2_program_info_priv_t* p = (qcap2_program_info_priv_t*)pThis;
    p->nAudioSourceCount = nCount;
}

int qcap2_program_info_get_audio_source_index(qcap2_program_info_t* pThis, int nIndex) {
    if (!pThis || nIndex < 0 || nIndex >= 16) return -1;
    qcap2_program_info_priv_t* p = (qcap2_program_info_priv_t*)pThis;
    return p->nAudioSourceIndex[nIndex];
}

void qcap2_program_info_set_audio_source_index(qcap2_program_info_t* pThis, int nIndex, int nValue) {
    if (!pThis || nIndex < 0 || nIndex >= 16) return;
    qcap2_program_info_priv_t* p = (qcap2_program_info_priv_t*)pThis;
    p->nAudioSourceIndex[nIndex] = nValue;
}

int qcap2_program_info_get_video_encoder_count(qcap2_program_info_t* pThis) {
    if (!pThis) return 0;
    qcap2_program_info_priv_t* p = (qcap2_program_info_priv_t*)pThis;
    return p->nVideoEncoderCount;
}

void qcap2_program_info_set_video_encoder_count(qcap2_program_info_t* pThis, int nCount) {
    if (!pThis) return;
    qcap2_program_info_priv_t* p = (qcap2_program_info_priv_t*)pThis;
    p->nVideoEncoderCount = nCount;
}

int qcap2_program_info_get_video_encoder_index(qcap2_program_info_t* pThis, int nIndex) {
    if (!pThis || nIndex < 0 || nIndex >= 16) return -1;
    qcap2_program_info_priv_t* p = (qcap2_program_info_priv_t*)pThis;
    return p->nVideoEncoderIndex[nIndex];
}

void qcap2_program_info_set_video_encoder_index(qcap2_program_info_t* pThis, int nIndex, int nValue) {
    if (!pThis || nIndex < 0 || nIndex >= 16) return;
    qcap2_program_info_priv_t* p = (qcap2_program_info_priv_t*)pThis;
    p->nVideoEncoderIndex[nIndex] = nValue;
}

int qcap2_program_info_get_audio_encoder_count(qcap2_program_info_t* pThis) {
    if (!pThis) return 0;
    qcap2_program_info_priv_t* p = (qcap2_program_info_priv_t*)pThis;
    return p->nAudioEncoderCount;
}

void qcap2_program_info_set_audio_encoder_count(qcap2_program_info_t* pThis, int nCount) {
    if (!pThis) return;
    qcap2_program_info_priv_t* p = (qcap2_program_info_priv_t*)pThis;
    p->nAudioEncoderCount = nCount;
}

int qcap2_program_info_get_audio_encoder_index(qcap2_program_info_t* pThis, int nIndex) {
    if (!pThis || nIndex < 0 || nIndex >= 16) return -1;
    qcap2_program_info_priv_t* p = (qcap2_program_info_priv_t*)pThis;
    return p->nAudioEncoderIndex[nIndex];
}

void qcap2_program_info_set_audio_encoder_index(qcap2_program_info_t* pThis, int nIndex, int nValue) {
    if (!pThis || nIndex < 0 || nIndex >= 16) return;
    qcap2_program_info_priv_t* p = (qcap2_program_info_priv_t*)pThis;
    p->nAudioEncoderIndex[nIndex] = nValue;
}

int qcap2_program_info_get_video_sink_count(qcap2_program_info_t* pThis) {
    if (!pThis) return 0;
    qcap2_program_info_priv_t* p = (qcap2_program_info_priv_t*)pThis;
    return p->nVideoSinkCount;
}

void qcap2_program_info_set_video_sink_count(qcap2_program_info_t* pThis, int nCount) {
    if (!pThis) return;
    qcap2_program_info_priv_t* p = (qcap2_program_info_priv_t*)pThis;
    p->nVideoSinkCount = nCount;
}

int qcap2_program_info_get_video_sink_index(qcap2_program_info_t* pThis, int nIndex) {
    if (!pThis || nIndex < 0 || nIndex >= 16) return -1;
    qcap2_program_info_priv_t* p = (qcap2_program_info_priv_t*)pThis;
    return p->nVideoSinkIndex[nIndex];
}

void qcap2_program_info_set_video_sink_index(qcap2_program_info_t* pThis, int nIndex, int nValue) {
    if (!pThis || nIndex < 0 || nIndex >= 16) return;
    qcap2_program_info_priv_t* p = (qcap2_program_info_priv_t*)pThis;
    p->nVideoSinkIndex[nIndex] = nValue;
}

int qcap2_program_info_get_audio_sink_count(qcap2_program_info_t* pThis) {
    if (!pThis) return 0;
    qcap2_program_info_priv_t* p = (qcap2_program_info_priv_t*)pThis;
    return p->nAudioSinkCount;
}

void qcap2_program_info_set_audio_sink_count(qcap2_program_info_t* pThis, int nCount) {
    if (!pThis) return;
    qcap2_program_info_priv_t* p = (qcap2_program_info_priv_t*)pThis;
    p->nAudioSinkCount = nCount;
}

int qcap2_program_info_get_audio_sink_index(qcap2_program_info_t* pThis, int nIndex) {
    if (!pThis || nIndex < 0 || nIndex >= 16) return -1;
    qcap2_program_info_priv_t* p = (qcap2_program_info_priv_t*)pThis;
    return p->nAudioSinkIndex[nIndex];
}

void qcap2_program_info_set_audio_sink_index(qcap2_program_info_t* pThis, int nIndex, int nValue) {
    if (!pThis || nIndex < 0 || nIndex >= 16) return;
    qcap2_program_info_priv_t* p = (qcap2_program_info_priv_t*)pThis;
    p->nAudioSinkIndex[nIndex] = nValue;
}

int qcap2_program_info_get_video_decoder_count(qcap2_program_info_t* pThis) {
    if (!pThis) return 0;
    qcap2_program_info_priv_t* p = (qcap2_program_info_priv_t*)pThis;
    return p->nVideoDecoderCount;
}

void qcap2_program_info_set_video_decoder_count(qcap2_program_info_t* pThis, int nCount) {
    if (!pThis) return;
    qcap2_program_info_priv_t* p = (qcap2_program_info_priv_t*)pThis;
    p->nVideoDecoderCount = nCount;
}

int qcap2_program_info_get_video_decoder_index(qcap2_program_info_t* pThis, int nIndex) {
    if (!pThis || nIndex < 0 || nIndex >= 16) return -1;
    qcap2_program_info_priv_t* p = (qcap2_program_info_priv_t*)pThis;
    return p->nVideoDecoderIndex[nIndex];
}

void qcap2_program_info_set_video_decoder_index(qcap2_program_info_t* pThis, int nIndex, int nValue) {
    if (!pThis || nIndex < 0 || nIndex >= 16) return;
    qcap2_program_info_priv_t* p = (qcap2_program_info_priv_t*)pThis;
    p->nVideoDecoderIndex[nIndex] = nValue;
}

int qcap2_program_info_get_audio_decoder_count(qcap2_program_info_t* pThis) {
    if (!pThis) return 0;
    qcap2_program_info_priv_t* p = (qcap2_program_info_priv_t*)pThis;
    return p->nAudioDecoderCount;
}

void qcap2_program_info_set_audio_decoder_count(qcap2_program_info_t* pThis, int nCount) {
    if (!pThis) return;
    qcap2_program_info_priv_t* p = (qcap2_program_info_priv_t*)pThis;
    p->nAudioDecoderCount = nCount;
}

int qcap2_program_info_get_audio_decoder_index(qcap2_program_info_t* pThis, int nIndex) {
    if (!pThis || nIndex < 0 || nIndex >= 16) return -1;
    qcap2_program_info_priv_t* p = (qcap2_program_info_priv_t*)pThis;
    return p->nAudioDecoderIndex[nIndex];
}

void qcap2_program_info_set_audio_decoder_index(qcap2_program_info_t* pThis, int nIndex, int nValue) {
    if (!pThis || nIndex < 0 || nIndex >= 16) return;
    qcap2_program_info_priv_t* p = (qcap2_program_info_priv_t*)pThis;
    p->nAudioDecoderIndex[nIndex] = nValue;
}

const char* qcap2_program_info_get_metadata(qcap2_program_info_t* pThis, const char* strKey) {
    if (!pThis || !strKey) return NULL;
    qcap2_program_info_priv_t* p = (qcap2_program_info_priv_t*)pThis;
    if (strncmp(p->metadataKey, strKey, sizeof(p->metadataKey)) == 0) {
        return p->metadataValue;
    }
    return NULL;
}

void qcap2_program_info_set_metadata(qcap2_program_info_t* pThis, const char* strKey, const char* strValue) {
    if (!pThis || !strKey || !strValue) return;
    qcap2_program_info_priv_t* p = (qcap2_program_info_priv_t*)pThis;
    strncpy(p->metadataKey, strKey, sizeof(p->metadataKey)-1);
    p->metadataKey[sizeof(p->metadataKey)-1] = '\0';
    strncpy(p->metadataValue, strValue, sizeof(p->metadataValue)-1);
    p->metadataValue[sizeof(p->metadataValue)-1] = '\0';
}

#ifdef __cplusplus
}
#endif
