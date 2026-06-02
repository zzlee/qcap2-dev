#include "qcap2.utils.h"
#include "qcap2.buffer.h"
#include "qcap2.formats.h"
#include <time.h>
#include <string.h>
#include <stdio.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

int64_t qcap2_get_time() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void qcap2_get_build_config(qcap2_build_config_t* pBuildConfig) {
    if (!pBuildConfig) return;
    pBuildConfig->major = 1;
    pBuildConfig->minor = 0;
    pBuildConfig->patch = 0;
    pBuildConfig->qcap_major = 2;
    pBuildConfig->qcap_minor = 0;
    pBuildConfig->qcap_patch = 0;
    pBuildConfig->build_date = __DATE__;
    pBuildConfig->build_time = __TIME__;
    pBuildConfig->branch = "main";
    pBuildConfig->commit = "unknown";
    pBuildConfig->mods = "";
}

QRESULT qcap2_save_raw_video_frame(qcap2_rcbuffer_t* pRCBuffer, const char* prefix) {
    if (!pRCBuffer || !prefix) return QCAP_RS_ERROR_GENERAL;
    PVOID pData = qcap2_rcbuffer_lock_data(pRCBuffer);
    if (!pData) return QCAP_RS_ERROR_GENERAL;

    qcap2_av_frame_t* pFrame = (qcap2_av_frame_t*)pData;
    uint8_t* pBuffer = NULL;
    int nStride = 0;
    qcap2_av_frame_get_buffer(pFrame, &pBuffer, &nStride);

    if (pBuffer) {
        ULONG nColorSpaceType = 0, nWidth = 0, nHeight = 0;
        qcap2_av_frame_get_video_property(pFrame, &nColorSpaceType, &nWidth, &nHeight);

        char filename[256];
        snprintf(filename, sizeof(filename), "%s_%dx%d.raw", prefix, (int)nWidth, (int)nHeight);
        FILE* fp = fopen(filename, "wb");
        if (fp) {
            // For a simple implementation, we just write stride * height
            // True implementation would depend on the color space
            fwrite(pBuffer, 1, nStride * nHeight, fp);
            fclose(fp);
        }
    }

    qcap2_rcbuffer_unlock_data(pRCBuffer);
    return QCAP_RS_SUCCESSFUL;
}

QRESULT qcap2_print_video_frame_info(qcap2_rcbuffer_t* pRCBuffer, const char* prefix) {
    if (!pRCBuffer || !prefix) return QCAP_RS_ERROR_GENERAL;
    PVOID pData = qcap2_rcbuffer_lock_data(pRCBuffer);
    if (!pData) return QCAP_RS_ERROR_GENERAL;

    qcap2_av_frame_t* pFrame = (qcap2_av_frame_t*)pData;
    ULONG nColorSpaceType = 0, nWidth = 0, nHeight = 0;
    qcap2_av_frame_get_video_property(pFrame, &nColorSpaceType, &nWidth, &nHeight);
    int64_t nPTS = 0;
    qcap2_av_frame_get_pts(pFrame, &nPTS);

    printf("[%s] Video Frame Info: ColorSpaceType=%lu, Width=%lu, Height=%lu, PTS=%lld\n",
           prefix, nColorSpaceType, nWidth, nHeight, (long long)nPTS);

    qcap2_rcbuffer_unlock_data(pRCBuffer);
    return QCAP_RS_SUCCESSFUL;
}

QRESULT qcap2_print_audio_sample_info(qcap2_rcbuffer_t* pRCBuffer, const char* prefix) {
    if (!pRCBuffer || !prefix) return QCAP_RS_ERROR_GENERAL;
    PVOID pData = qcap2_rcbuffer_lock_data(pRCBuffer);
    if (!pData) return QCAP_RS_ERROR_GENERAL;

    qcap2_av_frame_t* pFrame = (qcap2_av_frame_t*)pData;
    ULONG nChannels = 0, nSampleFmt = 0, nSampleFrequency = 0, nFrameSize = 0;
    qcap2_av_frame_get_audio_property(pFrame, &nChannels, &nSampleFmt, &nSampleFrequency, &nFrameSize);
    int64_t nPTS = 0;
    qcap2_av_frame_get_pts(pFrame, &nPTS);

    printf("[%s] Audio Sample Info: Channels=%lu, SampleFmt=%lu, SampleFreq=%lu, FrameSize=%lu, PTS=%lld\n",
           prefix, nChannels, nSampleFmt, nSampleFrequency, nFrameSize, (long long)nPTS);

    qcap2_rcbuffer_unlock_data(pRCBuffer);
    return QCAP_RS_SUCCESSFUL;
}

QRESULT qcap2_print_packet_info(qcap2_rcbuffer_t* pRCBuffer, const char* prefix) {
    if (!pRCBuffer || !prefix) return QCAP_RS_ERROR_GENERAL;
    printf("[%s] Packet Info: Data pointer %p\n", prefix, pRCBuffer);
    return QCAP_RS_SUCCESSFUL;
}

QRESULT qcap2_fill_video_test_pattern(qcap2_rcbuffer_t* pRCBuffer, int nType) {
    if (!pRCBuffer) return QCAP_RS_ERROR_GENERAL;
    PVOID pData = qcap2_rcbuffer_lock_data(pRCBuffer);
    if (!pData) return QCAP_RS_ERROR_GENERAL;

    qcap2_av_frame_t* pFrame = (qcap2_av_frame_t*)pData;
    uint8_t* pBuffer = NULL;
    int nStride = 0;
    qcap2_av_frame_get_buffer(pFrame, &pBuffer, &nStride);

    if (pBuffer) {
        ULONG nColorSpaceType = 0, nWidth = 0, nHeight = 0;
        qcap2_av_frame_get_video_property(pFrame, &nColorSpaceType, &nWidth, &nHeight);

        // Simple fill depending on nType, e.g., solid colors for known types
        uint8_t fill_val = (uint8_t)(nType & 0xFF);
        memset(pBuffer, fill_val, nStride * nHeight);
    }

    qcap2_rcbuffer_unlock_data(pRCBuffer);
    return QCAP_RS_SUCCESSFUL;
}

QRESULT qcap2_load_picture(qcap2_rcbuffer_t* pRCBuffer, const char* strFilePath) {
    if (!pRCBuffer || !strFilePath) return QCAP_RS_ERROR_GENERAL;
    int x, y, comp;
    unsigned char *data = stbi_load(strFilePath, &x, &y, &comp, 3); // Force RGB
    if (!data) return QCAP_RS_ERROR_GENERAL;

    PVOID pData = qcap2_rcbuffer_lock_data(pRCBuffer);
    if (!pData) {
        stbi_image_free(data);
        return QCAP_RS_ERROR_GENERAL;
    }

    qcap2_av_frame_t* pFrame = (qcap2_av_frame_t*)pData;
    qcap2_av_frame_set_video_property(pFrame, QCAP_COLORSPACE_TYPE_RGB24, x, y);

    uint8_t* pBuffer = NULL;
    int nStride = 0;
    qcap2_av_frame_get_buffer(pFrame, &pBuffer, &nStride);
    if (!pBuffer) {
        if (!qcap2_av_frame_alloc_buffer(pFrame, 1, 1)) {
            qcap2_rcbuffer_unlock_data(pRCBuffer);
            stbi_image_free(data);
            return QCAP_RS_ERROR_GENERAL;
        }
        qcap2_av_frame_get_buffer(pFrame, &pBuffer, &nStride);
    }

    if (pBuffer) {
        int copy_stride = x * 3;
        for (int i = 0; i < y; i++) {
            memcpy(pBuffer + i * nStride, data + i * copy_stride, copy_stride);
        }
    }

    qcap2_rcbuffer_unlock_data(pRCBuffer);
    stbi_image_free(data);
    return QCAP_RS_SUCCESSFUL;
}

QRESULT qcap2_get_picture_info(const char* strFilePath, qcap2_video_format_t* pVideoFormat) {
    if (!strFilePath || !pVideoFormat) return QCAP_RS_ERROR_GENERAL;
    int x, y, comp;
    if (stbi_info(strFilePath, &x, &y, &comp)) {
        qcap2_video_format_set_property(pVideoFormat, QCAP_COLORSPACE_TYPE_RGB24, x, y, FALSE, 0.0);
        return QCAP_RS_SUCCESSFUL;
    }
    return QCAP_RS_ERROR_GENERAL;
}

const char* qcap2_get_pix_fmt_name(int nFormat) {
    switch (nFormat) {
        case 0: return "PIX_FMT_RGB24";
        case 1: return "PIX_FMT_YUV420P";
        case 2: return "PIX_FMT_NV12";
        case 3: return "PIX_FMT_YUYV422";
        default: return "UNKNOWN_PIX_FMT";
    }
}

const char* qcap2_get_sample_fmt_name(int nFormat) {
    switch (nFormat) {
        case 0: return "SAMPLE_FMT_S16";
        case 1: return "SAMPLE_FMT_FLT";
        case 2: return "SAMPLE_FMT_S32";
        default: return "UNKNOWN_SAMPLE_FMT";
    }
}

qcap2_rational_t qcap2_d2q(double d, int nMax) {
    qcap2_rational_t r;
    int sign = (d < 0) ? -1 : 1;
    d = (d < 0) ? -d : d;

    if (d == 0) {
        r.num = 0;
        r.den = 1;
        return r;
    }

    if (d > nMax) {
        r.num = sign * nMax;
        r.den = 1;
        return r;
    }

    int best_num = 0, best_den = 1;
    double best_err = d;

    for (int den = 1; den <= nMax; den++) {
        int num = (int)(d * den + 0.5);
        if (num > nMax) break;
        double err = (double)num / den - d;
        if (err < 0) err = -err;

        if (err < best_err) {
            best_num = num;
            best_den = den;
            best_err = err;
            if (err == 0) break;
        }
    }

    r.num = sign * best_num;
    r.den = best_den;
    return r;
}

#define MAX_DEBUG_VALUES 64
static int g_debug_values[MAX_DEBUG_VALUES] = {0};

int qcap2_debug_get(int n) {
    if (n >= 0 && n < MAX_DEBUG_VALUES) {
        return __sync_fetch_and_add(&g_debug_values[n], 0);
    }
    return 0;
}

void qcap2_debug_set(int n, int v) {
    if (n >= 0 && n < MAX_DEBUG_VALUES) {
        __sync_lock_test_and_set(&g_debug_values[n], v);
    }
}

int qcap2_debug_fetch_add(int n, int v) {
    if (n >= 0 && n < MAX_DEBUG_VALUES) {
        return __sync_fetch_and_add(&g_debug_values[n], v);
    }
    return 0;
}

int qcap2_debug_fetch_sub(int n, int v) {
    if (n >= 0 && n < MAX_DEBUG_VALUES) {
        return __sync_fetch_and_sub(&g_debug_values[n], v);
    }
    return 0;
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
