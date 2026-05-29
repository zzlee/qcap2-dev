#ifndef HDAL_H
#define HDAL_H

#include <stdint.h>

typedef uintptr_t UINTPTR;
typedef int HD_BOOL;

typedef struct {
    uint32_t w;
    uint32_t h;
} HD_DIM;

typedef enum {
    HD_VIDEO_PXLFMT_YUYV = 0,
} HD_VIDEO_PXLFMT;

typedef struct {
    int dummy;
} HD_COMMON_MEM_INIT_CONFIG;

typedef struct {
    int dummy;
} HD_VIDEO_FRAME;

typedef enum {
    HD_COMMON_MEM_POOL_TYPE_DEFAULT = 0,
} HD_COMMON_MEM_POOL_TYPE;

typedef enum {
    HD_COMMON_MEM_DDR_ID_0 = 0,
} HD_COMMON_MEM_DDR_ID;

typedef enum {
    HD_COMMON_MEM_MEM_TYPE_CACHE = 0,
} HD_COMMON_MEM_MEM_TYPE;

typedef struct {
    int dummy;
} HD_AUDIO_FRAME;

typedef struct {
    int dummy;
} HD_VIDEOENC_BS;

typedef struct {
    int dummy;
} HD_VIDEOCAP_DRV_CONFIG;

typedef enum {
    HD_VIDEOCAP_CTRLFUNC_DEFAULT = 0,
} HD_VIDEOCAP_CTRLFUNC;

typedef enum {
    HD_AUDIOOUT_OUTPUT_DEFAULT = 0,
} HD_AUDIOOUT_OUTPUT;

typedef enum {
    HD_AUDIOOUT_VOLUME_DEFAULT = 0,
} HD_AUDIOOUT_VOLUME;

typedef struct {
    int dummy;
} HD_VIDEOENC_BUFINFO;

typedef enum {
    HD_VIDEOPROC_PIPE_DEFAULT = 0,
} HD_VIDEOPROC_PIPE;

typedef enum {
    HD_VIDEOPROC_CTRLFUNC_DEFAULT = 0,
} HD_VIDEOPROC_CTRLFUNC;

typedef struct {
    int dummy;
} HD_VIDEOPROC_CROP;

#endif // HDAL_H
