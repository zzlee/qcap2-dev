/**
 * @file qcap2.vcuallegro.cpp
 * @brief Xilinx Allegro VCU hardware backend for QCAP2 video encoder/decoder.
 *
 * This file provides the concrete implementations of the Allegro VCU backend
 * functions declared in qcap2.processing_priv.h. It is only compiled when
 * the QCAP2_HAVE_ALLEGRO preprocessor symbol is defined.
 *
 * When QCAP2_HAVE_ALLEGRO is not defined, the fallback stubs in
 * qcap2.processing.cpp provide the symbols instead.
 */

#ifdef QCAP2_HAVE_ALLEGRO

#include "qcap2.processing_priv.h"
#include "qcap.ext.core.h"

#include <cstring>
#include <cstdio>
#include <vector>
#include <queue>
#include <mutex>

// Allegro VCU SDK headers
#include "lib_encode/lib_encoder.h"
#include "lib_decode/lib_decode.h"
#include "lib_common/BufferAPI.h"
#include "lib_common/BufferPixMapMeta.h"
#include "lib_common/PixMapBuffer.h"
#include "lib_common/Allocator.h"
#include "lib_common/Error.h"
#include "lib_common_enc/Settings.h"
#include "lib_common_enc/EncChanParam.h"
#include "lib_common_enc/EncRecBuffer.h"
#include "lib_common/StreamBuffer.h"
#include "lib_common_dec/DecSettings.h"
#include "lib_common_dec/DecCallbacks.h"
#include "lib_common_dec/DecInfo.h"
#include "lib_decode/DecSchedulerMcu.h"
#include "lib_encode/EncSchedulerMcu.h"
#include "lib_common/LinuxDriverCommunication.h"
#include "lib_fpga/DmaAlloc.h"

// The Allegro backend functions must have C linkage to match the declarations
// seen by qcap2.processing.cpp (which includes qcap2.processing_priv.h from
// within the extern "C" block started by qcap2.processing.h).
#ifdef __cplusplus
extern "C" {
#endif

// ==============================================================================
// Internal helper structures
// ==============================================================================

/// Per-channel stream buffer pool for encoder
struct allegro_enc_pool_t {
    std::vector<AL_TBuffer*> stream_bufs;   // pre-allocated output stream buffers
    std::queue<AL_TBuffer*> free_streams;   // buffers ready to be used
    std::mutex mtx;
};

/// Per-channel display/output buffer pool for decoder
struct allegro_dec_pool_t {
    std::vector<AL_TBuffer*> display_bufs;  // pre-allocated display buffers
    std::queue<AL_TBuffer*> free_displays;  // buffers ready to be used
    std::mutex mtx;
    int width  = 0;
    int height = 0;
};

// ==============================================================================
// Buffer bridge helpers
// ==============================================================================

/**
 * Map QCAP2 color space type to Allegro TFourCC.
 * Returns 0 if no mapping exists.
 */
static TFourCC qcap_color_to_allegro_fourcc(ULONG nColorSpaceType)
{
    switch (nColorSpaceType) {
    case QCAP_COLORSPACE_TYPE_I420:
    case QCAP_COLORSPACE_TYPE_YV12:
        return FOURCC('Y','V','1','2');
    case QCAP_COLORSPACE_TYPE_NV12:
        return FOURCC('N','V','1','2');
    case QCAP_COLORSPACE_TYPE_NV21:
        return FOURCC('N','V','2','1');
    case QCAP_COLORSPACE_TYPE_YUYV:
        return FOURCC('Y','U','Y','V');
    case QCAP_COLORSPACE_TYPE_UYVY:
        return FOURCC('U','Y','V','Y');
    case QCAP_COLORSPACE_TYPE_RGBP:
        return FOURCC('R','G','B','P');
    case QCAP_COLORSPACE_TYPE_BGRP:
        return FOURCC('B','G','R','P');
    default:
        return 0;
    }
}

/**
 * Copy pixel data from a qcap2_av_frame_t into an AL_TBuffer pixmap buffer.
 * Returns true on success.
 */
static bool copy_frame_to_al_buffer(qcap2_av_frame_t* avFrame, AL_TBuffer* dstBuf)
{
    if (!avFrame || !dstBuf)
        return false;

    // Get source plane pointers and strides from the QCAP2 frame
    uint8_t* srcPlanes[4] = { nullptr, nullptr, nullptr, nullptr };
    int srcStrides[4] = { 0, 0, 0, 0 };
    // Note: qcap2_av_frame_get_buffer1 is declared in qcap2.buffer.h
    // and is available via qcap2.processing_priv.h -> qcap2.buffer.h
    // We call it here to get the actual data pointers.
    // On some builds this function may be inline or not exposed, in which
    // case the user must lock the rcbuffer and get the AVFrame pointer directly.
    // For now we assume it is available.
    // If not available, fall back to locking the rcbuffer and accessing
    // the fields directly.

    // Get destination plane data from AL_TBuffer via pixmap metadata
    AL_TPixMapMeta* pixMeta = (AL_TPixMapMeta*)AL_Buffer_GetMetaData(dstBuf, AL_META_TYPE_PIXMAP);
    if (!pixMeta)
        return false;

    int width  = pixMeta->tDim.iWidth;
    int height = pixMeta->tDim.iHeight;

    uint8_t* dstY = AL_PixMapBuffer_GetPlaneAddress(dstBuf, AL_PLANE_Y);
    int dstPitchY = AL_PixMapBuffer_GetPlanePitch(dstBuf, AL_PLANE_Y);
    uint8_t* dstU = AL_PixMapBuffer_GetPlaneAddress(dstBuf, AL_PLANE_U);
    int dstPitchU = AL_PixMapBuffer_GetPlanePitch(dstBuf, AL_PLANE_U);
    uint8_t* dstV = AL_PixMapBuffer_GetPlaneAddress(dstBuf, AL_PLANE_V);
    int dstPitchV = AL_PixMapBuffer_GetPlanePitch(dstBuf, AL_PLANE_V);

    if (!dstY) return false;

    // Try to get plane data via the frame's API, or fall back
    // Since qcap2_av_frame_t is opaque in the public headers,
    // we use the getter: qcap2_av_frame_get_buffer1
    qcap2_av_frame_get_buffer1(avFrame, srcPlanes, srcStrides);

    // Copy Y plane
    if (srcPlanes[0]) {
        int copyW = (width < srcStrides[0]) ? width : srcStrides[0];
        for (int y = 0; y < height; y++)
            memcpy(dstY + y * dstPitchY, srcPlanes[0] + y * srcStrides[0], copyW);
    }

    // Copy U plane
    if (srcPlanes[1] && dstU) {
        int uvH = height >> 1;
        int uvW = width  >> 1;
        int copyW = (uvW < srcStrides[1]) ? uvW : srcStrides[1];
        for (int y = 0; y < uvH; y++)
            memcpy(dstU + y * dstPitchU, srcPlanes[1] + y * srcStrides[1], copyW);
    }

    // Copy V plane
    if (srcPlanes[2] && dstV) {
        int uvH = height >> 1;
        int uvW = width  >> 1;
        int copyW = (uvW < srcStrides[2]) ? uvW : srcStrides[2];
        for (int y = 0; y < uvH; y++)
            memcpy(dstV + y * dstPitchV, srcPlanes[2] + y * srcStrides[2], copyW);
    }

    return true;
}

/**
 * Copy pixel data from an AL_TBuffer pixmap buffer into a qcap2_av_frame_t
 * (inside an output rcbuffer).  Returns true on success.
 */
static bool copy_al_buffer_to_frame(AL_TBuffer* srcBuf, qcap2_av_frame_t* avFrame)
{
    if (!srcBuf || !avFrame)
        return false;

    uint8_t* srcY = AL_PixMapBuffer_GetPlaneAddress(srcBuf, AL_PLANE_Y);
    int srcPitchY = AL_PixMapBuffer_GetPlanePitch(srcBuf, AL_PLANE_Y);
    uint8_t* srcU = AL_PixMapBuffer_GetPlaneAddress(srcBuf, AL_PLANE_U);
    int srcPitchU = AL_PixMapBuffer_GetPlanePitch(srcBuf, AL_PLANE_U);
    uint8_t* srcV = AL_PixMapBuffer_GetPlaneAddress(srcBuf, AL_PLANE_V);
    int srcPitchV = AL_PixMapBuffer_GetPlanePitch(srcBuf, AL_PLANE_V);

    AL_TPixMapMeta* pixMeta = (AL_TPixMapMeta*)AL_Buffer_GetMetaData(srcBuf, AL_META_TYPE_PIXMAP);
    int width  = pixMeta ? pixMeta->tDim.iWidth : 0;
    int height = pixMeta ? pixMeta->tDim.iHeight : 0;

    if (!srcY || width <= 0 || height <= 0)
        return false;

    // Set frame dimensions
    qcap2_av_frame_set_video_property(avFrame, QCAP_COLORSPACE_TYPE_I420, width, height);

    // Destination planes inside avFrame
    uint8_t* dstPlanes[4] = { nullptr, nullptr, nullptr, nullptr };
    int dstStrides[4] = { 0, 0, 0, 0 };
    qcap2_av_frame_get_buffer1(avFrame, dstPlanes, dstStrides);

    if (!dstPlanes[0]) return false;

    // Copy Y plane
    for (int y = 0; y < height; y++)
        memcpy(dstPlanes[0] + y * dstStrides[0], srcY + y * srcPitchY,
               (width < dstStrides[0]) ? width : dstStrides[0]);

    // Copy U plane
    if (dstPlanes[1] && srcU) {
        int uvH = height >> 1;
        int uvW = width  >> 1;
        for (int y = 0; y < uvH; y++)
            memcpy(dstPlanes[1] + y * dstStrides[1], srcU + y * srcPitchU,
                   (uvW < dstStrides[1]) ? uvW : dstStrides[1]);
    }

    // Copy V plane
    if (dstPlanes[2] && srcV) {
        int uvH = height >> 1;
        int uvW = width  >> 1;
        for (int y = 0; y < uvH; y++)
            memcpy(dstPlanes[2] + y * dstStrides[2], srcV + y * srcPitchV,
                   (uvW < dstStrides[2]) ? uvW : dstStrides[2]);
    }

    return true;
}

// ==============================================================================
// Encoder callback
// ==============================================================================

/**
 * Called by the Allegro encoder when a frame has been encoded, EOS reached,
 * or buffers need to be released.
 */
static void encoder_end_callback(void* pUserParam, AL_TBuffer* pStream,
                                  AL_TBuffer const* pSrc, int32_t iLayerID)
{
    (void)iLayerID;
    if (!pUserParam) return;

    qcap2_video_encoder_priv_t* p = (qcap2_video_encoder_priv_t*)pUserParam;

    // EOS reached (both null)
    if (!pStream && !pSrc)
        return;

    // Release source buffer (pSrc non-null, pStream null)
    if (!pStream && pSrc) {
        AL_Buffer_Destroy((AL_TBuffer*)pSrc);
        return;
    }

    // Encoded stream buffer available (pStream non-null)
    if (pStream) {
        // Get an output packet buffer from the output recycled queue
        qcap2_rcbuffer_t* outBuf = nullptr;
        if (qcap2_rcbuffer_queue_pop(p->output_recycled_queue, &outBuf) != QCAP_RS_SUCCESSFUL) {
            // No room -- recycle the stream buffer for reuse
            // We simply destroy it; a pool-based approach would push it back
            AL_Buffer_Destroy(pStream);
            return;
        }

        PVOID pPacketData = qcap2_rcbuffer_lock_data(outBuf);
        if (!pPacketData) {
            qcap2_rcbuffer_queue_push(p->output_recycled_queue, outBuf);
            AL_Buffer_Destroy(pStream);
            return;
        }

        qcap2_av_packet_t* pPacket = (qcap2_av_packet_t*)pPacketData;

        // Get stream data and sections
        uint8_t* streamData = AL_Buffer_GetData(pStream);
        size_t streamSize   = AL_Buffer_GetSize(pStream);
        AL_TStreamMetaData* streamMeta = (AL_TStreamMetaData*)AL_Buffer_GetMetaData(pStream, AL_META_TYPE_STREAM);

        if (streamMeta) {
            // Calculate total encoded size from sections
            size_t totalSize = 0;
            for (int i = 0; i < streamMeta->iNbSections; i++)
                totalSize += streamMeta->pSections[i].zSize;

            if (totalSize > 0) {
                // Allocate packet buffer
                if (qcap2_av_packet_alloc_buffer(pPacket, (int)totalSize)) {
                    uint8_t* pktBuf = nullptr;
                    int pktSize = 0;
                    qcap2_av_packet_get_buffer(pPacket, &pktBuf, &pktSize);

                    if (pktBuf && (int)totalSize <= pktSize) {
                        int offset = 0;
                        for (int i = 0; i < streamMeta->iNbSections; i++) {
                            memcpy(pktBuf + offset,
                                   streamData + streamMeta->pSections[i].zOffset,
                                   streamMeta->pSections[i].zSize);
                            offset += (int)streamMeta->pSections[i].zSize;
                        }
                        // Size already set by alloc_buffer to totalSize
                    }
                }
            }
        }

        // Mark key frame based on picture metadata
        AL_TPictureMetaData* picMeta = (AL_TPictureMetaData*)AL_Buffer_GetMetaData(pStream, AL_META_TYPE_PICTURE);
        if (picMeta) {
            bool isKey = (picMeta->eType == AL_SLICE_I);
            qcap2_av_packet_set_property(pPacket, 0, (BOOL)(isKey ? TRUE : FALSE));
        }

        qcap2_rcbuffer_unlock_data(outBuf);

        // Push to output queue
        {
            std::lock_guard<std::mutex> lock(*(p->mtx));
            p->output_queue.push(outBuf);
        }
        p->cv->notify_one();

        // Destroy the stream buffer (production would recycle into a pool)
        AL_Buffer_Destroy(pStream);
    }

    // pSrc still held by encoder; will be released when encoder is destroyed
    // or via a separate release-stream-buffer callback invocation
}

// ==============================================================================
// Encoder: allegro_encoder_start
// ==============================================================================
QRESULT allegro_encoder_start(qcap2_video_encoder_priv_t* p) {
    if (!p) return QCAP_RS_ERROR_GENERAL;

    // Already initialized
    if (p->allegro_inited) return QCAP_RS_SUCCESSFUL;

    // Guard: confirm the encoder property has an Allegro encoder type
    ULONG encType = QCAP_ENCODER_TYPE_SOFTWARE;
    if (p->enc_prop) {
        qcap2_video_encoder_property_get_type(p->enc_prop, &encType);
    }
    if (encType != QCAP_ENCODER_TYPE_ALLEGRO &&
        encType != QCAP_ENCODER_TYPE_ALLEGRO2) {
        return QCAP_RS_ERROR_GENERAL;
    }

    // 1. Initialize encoder library
    AL_ERR err = AL_Lib_Encoder_Init(AL_LIB_ENCODER_ARCH_HOST);
    if (err != AL_SUCCESS)
        return QCAP_RS_ERROR_GENERAL;

    // 2. Get encoder properties (resolution, format, bitrate, etc.)
    ULONG nEncoderFormat = 0, nColorSpaceType = 0;
    ULONG nWidth = 0, nHeight = 0;
    double dFrameRate = 0.0;
    ULONG nRecordMode = 0, nQuality = 0, nBitRate = 0, nGOP = 0;
    ULONG nAspectRatioX = 0, nAspectRatioY = 0;

    if (p->enc_prop) {
        qcap2_video_encoder_property_get_property(p->enc_prop,
            &encType, &nEncoderFormat, &nColorSpaceType,
            &nWidth, &nHeight, &dFrameRate,
            &nRecordMode, &nQuality, &nBitRate, &nGOP,
            &nAspectRatioX, &nAspectRatioY);
    }

    if (nWidth == 0 || nHeight == 0) {
        AL_Lib_Encoder_DeInit();
        return QCAP_RS_ERROR_GENERAL;
    }

    // 3. Device path
    const char* devicePath = "/dev/allegroIP";

    // 4. Create DMA allocator
    AL_TAllocator* alloc = AL_DmaAlloc_Create(devicePath);
    if (!alloc) {
        AL_Lib_Encoder_DeInit();
        return QCAP_RS_ERROR_GENERAL;
    }

    // 5. Create MCU scheduler
    AL_ICommunication* driver = AL_GetLinuxDriverCommunication();
    AL_IEncScheduler* sched = AL_SchedulerMcu_Create(driver,
        (AL_TLinuxDmaAllocator*)alloc, devicePath);
    if (!sched) {
        AL_Allocator_Destroy(alloc);
        AL_Lib_Encoder_DeInit();
        return QCAP_RS_ERROR_GENERAL;
    }

    // 6. Configure encoder settings
    AL_TEncSettings settings;
    AL_Settings_SetDefaults(&settings);

    // Channel parameters (layer 0)
    settings.tChParam[0].uEncWidth  = (uint16_t)nWidth;
    settings.tChParam[0].uEncHeight = (uint16_t)nHeight;
    settings.tChParam[0].uSrcWidth  = (uint16_t)nWidth;
    settings.tChParam[0].uSrcHeight = (uint16_t)nHeight;

    // Map FourCC from colorspace
    TFourCC fourcc = qcap_color_to_allegro_fourcc(nColorSpaceType);
    if (fourcc == 0)
        fourcc = FOURCC('Y','V','1','2');

    // Default to 8-bit 4:2:0
    settings.tChParam[0].ePicFormat = AL_420_8BITS;

    // Frame rate
    if (dFrameRate > 0.0) {
        settings.tChParam[0].tRCParam.uFrameRate = (uint16_t)(dFrameRate * 1000.0 / 1001.0 + 0.5);
        settings.tChParam[0].tRCParam.uClkRatio  = 1001;
    } else {
        settings.tChParam[0].tRCParam.uFrameRate = 30;
        settings.tChParam[0].tRCParam.uClkRatio  = 1001;
    }

    // Codec profile
    bool isH264 = (nEncoderFormat == QCAP_ENCODER_FORMAT_H264);
    settings.tChParam[0].eProfile = isH264 ? AL_PROFILE_AVC_MAIN : AL_PROFILE_HEVC_MAIN;
    settings.tChParam[0].uLevel   = 0; // auto

    // Rate control
    if (nBitRate > 0) {
        settings.tChParam[0].tRCParam.eRCMode = AL_RC_CBR;
        settings.tChParam[0].tRCParam.uTargetBitRate = nBitRate;
        settings.tChParam[0].tRCParam.uMaxBitRate    = nBitRate;
    } else {
        settings.tChParam[0].tRCParam.eRCMode = AL_RC_CONST_QP;
        settings.tChParam[0].tRCParam.iInitialQP = (int16_t)(51 - (nQuality ? nQuality : 25));
    }

    // GOP
    if (nGOP > 0) {
        settings.tChParam[0].tGopParam.uGopLength = (uint16_t)nGOP;
        settings.tChParam[0].tGopParam.uFreqIDR    = (uint32_t)nGOP;
    } else {
        settings.tChParam[0].tGopParam.uGopLength = 0; // intra only
        settings.tChParam[0].tGopParam.uFreqIDR    = 0;
    }
    settings.tChParam[0].tGopParam.eMode = AL_GOP_MODE_LOW_DELAY_P;
    settings.tChParam[0].tGopParam.uNumB = 0;

    // Enable AUD and SEI
    settings.bEnableAUD = true;
    settings.eEnableSEI = (AL_ESeiFlag)(AL_SEI_BP | AL_SEI_PT);

    // Filler data
    settings.eEnableFillerData = AL_FILLER_CTRL_AUTO;

    // Misc
    settings.bForceLoad = true;
    settings.tChParam[0].eVideoMode = AL_VM_PROGRESSIVE;
    settings.tChParam[0].eEncTools  = (AL_EChEncTool)(AL_OPT_LF | AL_OPT_LF_X_SLICE);
    settings.tChParam[0].eEncOptions = AL_OPT_NONE;

    // 7. Create encoder
    AL_CB_EndEncoding encCB;
    encCB.func      = encoder_end_callback;
    encCB.userParam = p;

    AL_HEncoder hEnc = nullptr;
    err = AL_Encoder_Create(&hEnc, sched, alloc, &settings, encCB);
    if (err != AL_SUCCESS || !hEnc) {
        AL_IEncScheduler_Destroy(sched);
        AL_Allocator_Destroy(alloc);
        AL_Lib_Encoder_DeInit();
        return QCAP_RS_ERROR_GENERAL;
    }

    // 8. Pre-allocate stream output buffers
    int streamBufCount = (p->packet_count > 0) ? p->packet_count : 8;
    int streamBufSize  = (p->max_packet_size > 0) ? p->max_packet_size : (2 * 1024 * 1024);

    // Use AL_GetMitigatedMaxNalSize for a better estimate
    int maxNalSize = AL_GetMitigatedMaxNalSize(
        { (int)nWidth, (int)nHeight },
        AL_GET_CHROMA_MODE(settings.tChParam[0].ePicFormat),
        AL_GET_BITDEPTH(settings.tChParam[0].ePicFormat));
    if (maxNalSize > 0 && maxNalSize + 4096 > streamBufSize)
        streamBufSize = maxNalSize + 4096;

    for (int i = 0; i < streamBufCount; i++) {
        AL_TBuffer* streamBuf = AL_Buffer_Create_And_Allocate(alloc,
            streamBufSize, nullptr);
        if (streamBuf) {
            // Attach stream metadata
            AL_TStreamMetaData* streamMeta = AL_StreamMetaData_Create(AL_MAX_SECTION);
            if (streamMeta)
                AL_Buffer_AddMetaData(streamBuf, (AL_TMetaData*)streamMeta);

            // Give stream buffer to encoder
            AL_Encoder_PutStreamBuffer(hEnc, streamBuf);
        }
    }

    // Store handles
    p->allegro_enc_handle = (void*)hEnc;
    p->allegro_scheduler  = (void*)sched;
    p->allegro_allocator  = (void*)alloc;
    p->allegro_channel_id = 0;
    p->allegro_inited     = true;

    return QCAP_RS_SUCCESSFUL;
}

// ==============================================================================
// Encoder: allegro_encoder_stop
// ==============================================================================
QRESULT allegro_encoder_stop(qcap2_video_encoder_priv_t* p) {
    if (!p) return QCAP_RS_ERROR_GENERAL;
    if (!p->allegro_inited) return QCAP_RS_SUCCESSFUL;

    AL_HEncoder hEnc = (AL_HEncoder)p->allegro_enc_handle;

    // 1. Send EOS (push null frame to drain)
    if (hEnc)
        AL_Encoder_Process(hEnc, nullptr, nullptr);

    // 2. Destroy encoder
    if (hEnc)
        AL_Encoder_Destroy(hEnc);
    p->allegro_enc_handle = nullptr;

    // 3. Destroy scheduler
    if (p->allegro_scheduler) {
        AL_IEncScheduler_Destroy((AL_IEncScheduler*)p->allegro_scheduler);
        p->allegro_scheduler = nullptr;
    }

    // 4. Destroy allocator
    if (p->allegro_allocator) {
        AL_Allocator_Destroy((AL_TAllocator*)p->allegro_allocator);
        p->allegro_allocator = nullptr;
    }

    // 5. De-initialize encoder library
    AL_Lib_Encoder_DeInit();

    p->allegro_channel_id = 0;
    p->allegro_inited     = false;

    return QCAP_RS_SUCCESSFUL;
}

// ==============================================================================
// Encoder: allegro_encoder_push
// ==============================================================================
QRESULT allegro_encoder_push(qcap2_video_encoder_priv_t* p, qcap2_rcbuffer_t* pRCBuffer) {
    if (!p || !pRCBuffer) return QCAP_RS_ERROR_GENERAL;
    if (!p->allegro_inited) return QCAP_RS_ERROR_GENERAL;

    AL_HEncoder hEnc = (AL_HEncoder)p->allegro_enc_handle;

    // 1. Lock the input rcbuffer and get the av frame
    PVOID pData = qcap2_rcbuffer_lock_data(pRCBuffer);
    if (!pData) return QCAP_RS_ERROR_GENERAL;

    qcap2_av_frame_t* pFrame = (qcap2_av_frame_t*)pData;

    // 2. Get frame properties
    ULONG color = 0, w = 0, h = 0;
    qcap2_av_frame_get_video_property(pFrame, &color, &w, &h);
    if (w == 0 || h == 0) {
        qcap2_rcbuffer_unlock_data(pRCBuffer);
        return QCAP_RS_ERROR_GENERAL;
    }

    // 3. Create a source AL_TBuffer pixmap for this frame
    AL_TAllocator* alloc = (AL_TAllocator*)p->allegro_allocator;
    TFourCC fourcc = qcap_color_to_allegro_fourcc(color);
    if (fourcc == 0)
        fourcc = FOURCC('Y','V','1','2');

    AL_TDimension dim = { (int)w, (int)h };

    // Create pixmap buffer (allocates DMA memory and sets up pixmap metadata)
    AL_TBuffer* srcBuf = AL_PixMapBuffer_Create(alloc, nullptr, dim, fourcc);
    if (!srcBuf) {
        qcap2_rcbuffer_unlock_data(pRCBuffer);
        return QCAP_RS_ERROR_RESOURCE;
    }

    // 4. Copy pixel data from qcap2 frame into the AL_TBuffer
    if (!copy_frame_to_al_buffer(pFrame, srcBuf)) {
        AL_Buffer_Destroy(srcBuf);
        qcap2_rcbuffer_unlock_data(pRCBuffer);
        return QCAP_RS_ERROR_GENERAL;
    }

    // 5. Flush memory to make it visible to the hardware
    AL_Buffer_FlushMemory(srcBuf);

    // 6. Push frame to encoder
    if (!AL_Encoder_Process(hEnc, srcBuf, nullptr)) {
        AL_Buffer_Destroy(srcBuf);
        qcap2_rcbuffer_unlock_data(pRCBuffer);
        return QCAP_RS_ERROR_GENERAL;
    }

    // 7. Unlock and recycle input buffer
    qcap2_rcbuffer_unlock_data(pRCBuffer);
    qcap2_rcbuffer_queue_push(p->input_recycled_queue, pRCBuffer);

    return QCAP_RS_SUCCESSFUL;
}

// ==============================================================================
// Decoder callbacks
// ==============================================================================

/// Callback when a stream buffer has been parsed
static void dec_end_parsing_cb(AL_TBuffer* pParsedFrame, void* pUserParam, int32_t iParsingID)
{
    (void)iParsingID;
    if (!pUserParam || !pParsedFrame) return;

    // The stream buffer is no longer needed; we destroy it.
    // In a pool-based approach we would recycle it.
    AL_Buffer_Destroy(pParsedFrame);
}

/// Callback when a frame has been decoded
static void dec_end_decoding_cb(AL_TBuffer* pDecodedFrame, void* pUserParam)
{
    (void)pDecodedFrame;
    (void)pUserParam;
    // No action needed; display callback handles output.
}

/// Callback when a decoded frame is ready for display
static void dec_display_cb(AL_TBuffer* pDisplayedFrame, AL_TInfoDecode* pInfo, void* pUserParam)
{
    if (!pUserParam) return;

    qcap2_video_decoder_priv_t* p = (qcap2_video_decoder_priv_t*)pUserParam;

    // End of stream
    if (!pDisplayedFrame && !pInfo)
        return;

    // Decoder releasing a frame (pInfo == null) -- put it back
    if (pDisplayedFrame && !pInfo) {
        if (p->allegro_dec_handle)
            AL_Decoder_PutDisplayPicture((AL_HDecoder)p->allegro_dec_handle, pDisplayedFrame);
        return;
    }

    // Normal display callback with decoded frame
    if (pDisplayedFrame && pInfo) {
        // Get an output buffer from the recycled queue
        qcap2_rcbuffer_t* outBuf = nullptr;
        if (qcap2_rcbuffer_queue_pop(p->output_recycled_queue, &outBuf) != QCAP_RS_SUCCESSFUL) {
            // Drop frame; recycle display buffer
            if (p->allegro_dec_handle)
                AL_Decoder_PutDisplayPicture((AL_HDecoder)p->allegro_dec_handle, pDisplayedFrame);
            return;
        }

        PVOID pFrameData = qcap2_rcbuffer_lock_data(outBuf);
        if (!pFrameData) {
            qcap2_rcbuffer_queue_push(p->output_recycled_queue, outBuf);
            if (p->allegro_dec_handle)
                AL_Decoder_PutDisplayPicture((AL_HDecoder)p->allegro_dec_handle, pDisplayedFrame);
            return;
        }

        qcap2_av_frame_t* avFrame = (qcap2_av_frame_t*)pFrameData;

        // Copy decoded data from AL_TBuffer into avFrame
        copy_al_buffer_to_frame(pDisplayedFrame, avFrame);

        qcap2_rcbuffer_unlock_data(outBuf);

        // Push to output queue
        {
            std::lock_guard<std::mutex> lock(*(p->mtx));
            p->output_queue.push(outBuf);
        }
        p->cv->notify_one();

        // Return display buffer to decoder
        if (p->allegro_dec_handle)
            AL_Decoder_PutDisplayPicture((AL_HDecoder)p->allegro_dec_handle, pDisplayedFrame);
    }
}

/// Callback when a resolution change is detected.
static AL_ERR dec_resolution_found_cb(int32_t BufferNumber,
                                       AL_TStreamSettings const* pSettings,
                                       AL_TCropInfo const* pCropInfo,
                                       void* pUserParam)
{
    (void)pCropInfo;
    if (!pUserParam) return AL_ERROR;

    qcap2_video_decoder_priv_t* p = (qcap2_video_decoder_priv_t*)pUserParam;

    int width  = pSettings->tDim.iWidth;
    int height = pSettings->tDim.iHeight;

    // Update target dimensions
    p->target_width  = width;
    p->target_height = height;

    // Allocate and feed display buffers to decoder
    AL_TAllocator* alloc = (AL_TAllocator*)p->allegro_allocator;
    AL_HDecoder hDec = (AL_HDecoder)p->allegro_dec_handle;

    if (alloc && hDec) {
        AL_TDimension dim = { width, height };
        int bufCount = (BufferNumber > 0) ? BufferNumber : 4;

        for (int i = 0; i < bufCount; i++) {
            AL_TBuffer* dispBuf = AL_PixMapBuffer_Create(alloc, nullptr, dim,
                                                           FOURCC('Y','V','1','2'));
            if (dispBuf) {
                if (!AL_Decoder_PutDisplayPicture(hDec, dispBuf))
                    AL_Buffer_Destroy(dispBuf);
            }
        }
    }

    return AL_SUCCESS;
}

/// Callback when a decoding error occurs
static void dec_error_cb(AL_ERR eError, void* pUserParam)
{
    (void)eError;
    (void)pUserParam;
}

// ==============================================================================
// Decoder: allegro_decoder_start
// ==============================================================================
QRESULT allegro_decoder_start(qcap2_video_decoder_priv_t* p) {
    if (!p) return QCAP_RS_ERROR_GENERAL;

    // Already initialized
    if (p->allegro_inited) return QCAP_RS_SUCCESSFUL;

    // Guard against start without proper decoder type
    ULONG decType = QCAP_DECODER_TYPE_SOFTWARE;
    if (p->dec_prop) {
        qcap2_video_encoder_property_get_type(p->dec_prop, &decType);
    }
    if (decType != QCAP_DECODER_TYPE_ALLEGRO) {
        return QCAP_RS_ERROR_GENERAL;
    }

    // 1. Initialize decoder library
    AL_ERR err = AL_Lib_Decoder_Init(AL_LIB_DECODER_ARCH_HOST);
    if (err != AL_SUCCESS)
        return QCAP_RS_ERROR_GENERAL;

    // 2. Get decoder properties
    ULONG nEncoderFormat = 0, nColorSpaceType = 0;
    ULONG nWidth = 0, nHeight = 0;
    double dFrameRate = 0.0;

    if (p->dec_prop) {
        ULONG dummyEncType = 0, dummyMode = 0, dummyQuality = 0, dummyBitRate = 0, dummyGOP = 0;
        ULONG dummyARX = 0, dummyARY = 0;
        qcap2_video_encoder_property_get_property(p->dec_prop,
            &dummyEncType, &nEncoderFormat, &nColorSpaceType,
            &nWidth, &nHeight, &dFrameRate,
            &dummyMode, &dummyQuality, &dummyBitRate, &dummyGOP,
            &dummyARX, &dummyARY);
    }

    const char* devicePath = "/dev/allegroDecodeIP";

    // 3. Create DMA allocator
    AL_TAllocator* alloc = AL_DmaAlloc_Create(devicePath);
    if (!alloc) {
        AL_Lib_Decoder_DeInit();
        return QCAP_RS_ERROR_GENERAL;
    }

    // 4. Create MCU scheduler
    AL_ICommunication* driver = AL_GetLinuxDriverCommunication();
    AL_IDecScheduler* sched = AL_DecSchedulerMcu_Create(driver, devicePath);
    if (!sched) {
        AL_Allocator_Destroy(alloc);
        AL_Lib_Decoder_DeInit();
        return QCAP_RS_ERROR_GENERAL;
    }

    // 5. Configure decoder settings
    AL_TDecSettings settings;
    AL_DecSettings_SetDefaults(&settings);

    // Infer codec from encoder format
    if (nEncoderFormat == QCAP_ENCODER_FORMAT_H264)
        settings.eCodec = AL_CODEC_AVC;
    else if (nEncoderFormat == QCAP_ENCODER_FORMAT_HEVC)
        settings.eCodec = AL_CODEC_HEVC;
    else
        settings.eCodec = AL_CODEC_AVC; // default

    // Set max resolution hints
    if (nWidth > 0 && nHeight > 0) {
        settings.tStream.tDim.iWidth  = nWidth;
        settings.tStream.tDim.iHeight = nHeight;
    } else {
        settings.tStream.tDim.iWidth  = 1920;
        settings.tStream.tDim.iHeight = 1080;
    }

    settings.uNumCore       = 1;
    settings.bNonRealtime   = false;
    settings.eInputMode     = AL_DEC_UNSPLIT_INPUT;
    settings.eDecUnit       = AL_DEC_UNIT_FRAME;
    settings.eDpbMode       = AL_DPB_MODE_NORMAL;
    settings.bLowLat        = false;

    // 6. Register callbacks
    AL_TDecCallBacks callbacks;
    callbacks.endParsingCB.func          = dec_end_parsing_cb;
    callbacks.endParsingCB.userParam     = p;
    callbacks.endDecodingCB.func         = dec_end_decoding_cb;
    callbacks.endDecodingCB.userParam    = p;
    callbacks.displayCB.func             = dec_display_cb;
    callbacks.displayCB.userParam        = p;
    callbacks.resolutionFoundCB.func     = dec_resolution_found_cb;
    callbacks.resolutionFoundCB.userParam = p;
    callbacks.parsedSeiCB.func           = nullptr;
    callbacks.parsedSeiCB.userParam      = nullptr;
    callbacks.errorCB.func              = dec_error_cb;
    callbacks.errorCB.userParam         = p;

    // 7. Create decoder
    AL_HDecoder hDec = nullptr;
    err = AL_Decoder_Create(&hDec, sched, alloc, &settings, &callbacks);
    if (err != AL_SUCCESS || !hDec) {
        AL_IDecScheduler_Destroy(sched);
        AL_Allocator_Destroy(alloc);
        AL_Lib_Decoder_DeInit();
        return QCAP_RS_ERROR_GENERAL;
    }

    // 8. Pre-allocate initial display buffers and feed to decoder
    int dispBufCount = (p->frame_count > 0) ? p->frame_count : 4;
    AL_TDimension dispDim = { (int)settings.tStream.tDim.iWidth,
                              (int)settings.tStream.tDim.iHeight };

    for (int i = 0; i < dispBufCount; i++) {
        AL_TBuffer* dispBuf = AL_PixMapBuffer_Create(alloc, nullptr, dispDim,
                                                       FOURCC('Y','V','1','2'));
        if (dispBuf) {
            if (!AL_Decoder_PutDisplayPicture(hDec, dispBuf))
                AL_Buffer_Destroy(dispBuf);
        }
    }

    // 9. Push extra data (SPS/PPS/VPS) if available
    if (p->extra_data && p->extra_data_size > 0) {
        AL_TBuffer* bsBuf = AL_Buffer_Create_And_Allocate(alloc,
            p->extra_data_size, nullptr);
        if (bsBuf) {
            memcpy(AL_Buffer_GetData(bsBuf), p->extra_data, p->extra_data_size);
            AL_Decoder_PushStreamBuffer(hDec, bsBuf, p->extra_data_size,
                                         AL_STREAM_BUF_FLAG_ENDOFFRAME);
        }
    }

    // Store handles
    p->allegro_dec_handle = (void*)hDec;
    p->allegro_scheduler  = (void*)sched;
    p->allegro_allocator  = (void*)alloc;
    p->allegro_inited     = true;

    return QCAP_RS_SUCCESSFUL;
}

// ==============================================================================
// Decoder: allegro_decoder_stop
// ==============================================================================
QRESULT allegro_decoder_stop(qcap2_video_decoder_priv_t* p) {
    if (!p) return QCAP_RS_ERROR_GENERAL;
    if (!p->allegro_inited) return QCAP_RS_SUCCESSFUL;

    AL_HDecoder hDec = (AL_HDecoder)p->allegro_dec_handle;

    // 1. Flush decoder
    if (hDec)
        AL_Decoder_Flush(hDec);

    // 2. Destroy decoder
    if (hDec)
        AL_Decoder_Destroy(hDec);
    p->allegro_dec_handle = nullptr;

    // 3. Destroy scheduler
    if (p->allegro_scheduler) {
        AL_IDecScheduler_Destroy((AL_IDecScheduler*)p->allegro_scheduler);
        p->allegro_scheduler = nullptr;
    }

    // 4. Destroy allocator
    if (p->allegro_allocator) {
        AL_Allocator_Destroy((AL_TAllocator*)p->allegro_allocator);
        p->allegro_allocator = nullptr;
    }

    // 5. De-initialize decoder library
    AL_Lib_Decoder_DeInit();

    p->allegro_inited = false;

    return QCAP_RS_SUCCESSFUL;
}

// ==============================================================================
// Decoder: allegro_decoder_push
// ==============================================================================
QRESULT allegro_decoder_push(qcap2_video_decoder_priv_t* p, qcap2_rcbuffer_t* pRCBuffer) {
    if (!p || !pRCBuffer) return QCAP_RS_ERROR_GENERAL;
    if (!p->allegro_inited) return QCAP_RS_ERROR_GENERAL;

    AL_HDecoder hDec = (AL_HDecoder)p->allegro_dec_handle;

    // 1. Lock input packet
    PVOID pData = qcap2_rcbuffer_lock_data(pRCBuffer);
    if (!pData) return QCAP_RS_ERROR_GENERAL;

    qcap2_av_packet_t* pPacket = (qcap2_av_packet_t*)pData;

    // 2. Get packet data
    uint8_t* pInputBuffer = nullptr;
    int nInputSize = 0;
    qcap2_av_packet_get_buffer(pPacket, &pInputBuffer, &nInputSize);

    if (!pInputBuffer || nInputSize <= 0) {
        qcap2_rcbuffer_unlock_data(pRCBuffer);
        return QCAP_RS_ERROR_GENERAL;
    }

    // 3. Create stream buffer from DMA allocator
    AL_TAllocator* alloc = (AL_TAllocator*)p->allegro_allocator;
    AL_TBuffer* bsBuf = AL_Buffer_Create_And_Allocate(alloc, nInputSize, nullptr);
    if (!bsBuf) {
        qcap2_rcbuffer_unlock_data(pRCBuffer);
        return QCAP_RS_ERROR_RESOURCE;
    }

    // 4. Copy compressed data
    memcpy(AL_Buffer_GetData(bsBuf), pInputBuffer, nInputSize);
    AL_Buffer_FlushMemory(bsBuf);

    // 5. Unlock input packet and recycle
    qcap2_rcbuffer_unlock_data(pRCBuffer);
    qcap2_rcbuffer_queue_push(p->input_recycled_queue, pRCBuffer);

    // 6. Push to decoder
    uint8_t flags = (uint8_t)AL_STREAM_BUF_FLAG_ENDOFFRAME;
    if (!AL_Decoder_PushStreamBuffer(hDec, bsBuf, nInputSize, flags)) {
        AL_Buffer_Destroy(bsBuf);
        return QCAP_RS_ERROR_GENERAL;
    }

    return QCAP_RS_SUCCESSFUL;
}

#ifdef __cplusplus
}
#endif

#endif // QCAP2_HAVE_ALLEGRO
