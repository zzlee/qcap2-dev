#ifndef __QCAP_HI3531A_H__
#define __QCAP_HI3531A_H__

#include "qcap.types.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

// HWND custom property
enum {
	QCAP_WINPROP_ROTATE						= 100, // in,out: ULONG (in degrees, 0~360)
	QCAP_WINPROP_ZOOM						= 101, // in: VO_ZOOM_ATTR_S

	// private use only
	QCAP_WINPROP_BINDING_VPSS				= 10800, // in: [ULONG, ULONG]
	QCAP_WINPROP_BINDING_AVS				= 10801, // in: [ULONG, ULONG]
	QCAP_WINPROP_BINDING_VDEC				= 10802, // in: [ULONG]
};

// constant values
enum {
	QCAP_MAX_AUDIO_RENDERER_COUNT = 2,
};

#pragma pack(push)
#pragma pack(1)

typedef struct QCAP_PROPERTY_AUDIO_RENDERER_VOLUME_IN QCAP_PROPERTY_AUDIO_RENDERER_VOLUME_IN;
struct QCAP_PROPERTY_AUDIO_RENDERER_VOLUME_IN
{
	ULONG nVolume[QCAP_MAX_AUDIO_RENDERER_COUNT];
};

typedef struct QCAP_PROPERTY_AUDIO_RENDERER_VOLUME_OUT QCAP_PROPERTY_AUDIO_RENDERER_VOLUME_OUT;
struct QCAP_PROPERTY_AUDIO_RENDERER_VOLUME_OUT
{
	ULONG nVolume[QCAP_MAX_AUDIO_RENDERER_COUNT];
};

#pragma pack(pop)

typedef struct qcap_av_packet_t qcap_av_packet_t;
struct qcap_av_packet_t {
	PVOID __pPrivateData0; // private use

	LONGLONG nPTS; // in microseconds
	LONGLONG nDTS; // in microseconds
	BYTE* pData;
	int nSize;
	int nIndex;
	int nFlags; // 1 for key-frame
};

typedef struct qcap_native_audio_frame_t qcap_native_audio_frame_t;
struct qcap_native_audio_frame_t {
	struct {
		int32_t __nPrivateData0[2]; // private use

		uint8_t *pData[2];

		int32_t __nPrivateData1[2]; // private use

		int64_t nTimeStamp;

		int32_t __nPrivateData2; // private use

		int32_t nLength;

		int32_t __nPrivateData3[2]; // private use
	} frames[2];
};

typedef struct qcap_av_frame_t qcap_av_frame_t;
struct qcap_av_frame_t {
	BYTE* pData[8]; // data pointer for each image plane
	int nPitch[8]; // pitch for each image plane

	PVOID pPrivateData0;

	// width & height of the video frame
	int nWidth;
	int nHeight;

	int nSamples; // number of audio samples (per channel)

	int nFormat;
};

typedef struct qcap_generic_av_frame_t qcap_generic_av_frame_t;
struct qcap_generic_av_frame_t {
	int type;
	// case 0xCAFE0001: video_frame
	// case 0xCAFE0002: audio_frame or native_audio_frame
	// case 0xCAFE0003: av_frame

	union {
#if 0
		VIDEO_FRAME_INFO_S video_frame;
#endif

		qcap_native_audio_frame_t native_audio_frame;
		qcap_av_frame_t av_frame;
	} u;
};

typedef QRETURN (QCAP_EXPORT *PF_MOTION_DETECT_CALLBACK)( PVOID pVDAHandle /*IN*/, BYTE *SadResult /*IN*/, ULONG nAvgValue /*IN*/, PVOID pUserData /*IN*/ );
typedef QRETURN (QCAP_EXPORT *PF_HDMI_SINK_CALLBACK)( ULONG nEvent, PVOID pUserData /*IN*/ ); // nEvent (HI_HDMI_EVENT_TYPE_E)

// HISIV init/uninit
QCAP_EXT_API QRESULT QCAP_EXPORT QCAP_HI_INITIALIZE() __attribute__ ((deprecated));
QCAP_EXT_API QRESULT QCAP_EXPORT QCAP_HI_UNINITIALIZE() __attribute__ ((deprecated));

QCAP_EXT_API double QCAP_EXPORT QCAP_HI_GET_TIME();

QCAP_EXT_API QRESULT QCAP_EXPORT QCAP_HI_SET_CUSTOM_PROPERTY(ULONG nProperty, BYTE* pValue, ULONG nBytes);
QCAP_EXT_API QRESULT QCAP_EXPORT QCAP_HI_GET_CUSTOM_PROPERTY(ULONG nProperty, BYTE* pValue, ULONG nBytes);

//// Window system
QCAP_EXT_API QRESULT QCAP_EXPORT QCAP_HI_CREATE_WINDOW(int x, int y, int w, int h, HWND hParent, HWND* pWnd);
QCAP_EXT_API QRESULT QCAP_EXPORT QCAP_HI_DESTROY_WINDOW(HWND hWnd);

QCAP_EXT_API QRESULT QCAP_EXPORT QCAP_HI_SHOW_WINDOW(HWND hWnd);
QCAP_EXT_API QRESULT QCAP_EXPORT QCAP_HI_HIDE_WINDOW(HWND hWnd);
QCAP_EXT_API QRESULT QCAP_EXPORT QCAP_HI_MOVE_WINDOW(HWND hWnd, int x, int y, int w, int h);
QCAP_EXT_API QRESULT QCAP_EXPORT QCAP_HI_GET_WINDOW_RECT(HWND hWnd, int* px, int* py, int* pw, int* ph);
QCAP_EXT_API QRESULT QCAP_EXPORT QCAP_HI_WINDOW_BRING_TO_FRONT(HWND hWnd);
QCAP_EXT_API QRESULT QCAP_EXPORT QCAP_HI_WINDOW_SEND_TO_BACK(HWND hWnd);
QCAP_EXT_API QRESULT QCAP_EXPORT QCAP_HI_WINDOW_BRING_FORWARD(HWND hWnd);
QCAP_EXT_API QRESULT QCAP_EXPORT QCAP_HI_WINDOW_SEND_BACKWARD(HWND hWnd);
QCAP_EXT_API QRESULT QCAP_EXPORT QCAP_HI_SET_WINDOW_ASPECT_RATIO(HWND hWnd, int flags, int bgColor);
QCAP_EXT_API QRESULT QCAP_EXPORT QCAP_HI_ENABLE_WINDOW_SPLIT_MODE(HWND hWnd, BOOL bEnable);
QCAP_EXT_API QRESULT QCAP_EXPORT QCAP_HI_SET_VIDEO_WINDOW_UNCOMPRESSION_BUFFER(HWND hWnd, BYTE * pFrameBuffer /*IN*/, ULONG nFrameBufferLen /*IN*/, double dSampleTime);
QCAP_EXT_API QRESULT QCAP_EXPORT QCAP_HI_SET_VIDEO_WINDOW_FRAME(HWND hWnd, qcap_generic_av_frame_t* pAVFrame);
QCAP_EXT_API QRESULT QCAP_EXPORT QCAP_HI_GET_WINDOW_PROPERTY(HWND hWnd, ULONG nProperty, BYTE * pValue /*OUT*/, ULONG nBytes /*IN*/);
QCAP_EXT_API QRESULT QCAP_EXPORT QCAP_HI_SET_WINDOW_PROPERTY(HWND hWnd, ULONG nProperty, BYTE * pValue /*OUT*/, ULONG nBytes /*IN*/);

//// RTSP Server
QCAP_EXT_API QRESULT QCAP_EXPORT QCAP_HI_RESET_BROADCAST_SERVER(PVOID pServer, UINT iSessionNum);

//// Share record
QCAP_EXT_API QRESULT QCAP_EXPORT QCAP_HI_RESET_SHARE_RECORD(UINT iRecodNum);

QCAP_EXT_API QRESULT QCAP_EXPORT QCAP_BUFFER_ADD_REF( BYTE * pBuffer, ULONG nBufferLen ) __attribute__ ((deprecated));
QCAP_EXT_API QRESULT QCAP_EXPORT QCAP_BUFFER_RELEASE( BYTE * pBuffer, ULONG nBufferLen ) __attribute__ ((deprecated));

QCAP_EXT_API QRESULT QCAP_EXPORT QCAP_HI_ALPHA_BLEND( ULONG nSrcColorSpaceType /*IN*/, ULONG nSrcWidth /*IN*/, ULONG nSrcHeight /*IN*/, BYTE * pSrcFrameBuffer, ULONG nSrcFrameBufferLen, ULONG nDstColorSpaceType /*IN*/, ULONG nDstWidth /*IN*/, ULONG nDstHeight /*IN*/, BYTE * pDstFrameBuffer, ULONG nDstFrameBufferLen, BYTE bAlpha );

QCAP_EXT_API QRESULT QCAP_EXPORT QCAP_HI_CLONE_VIDEO_FRAME( ULONG nColorSpaceType /*IN*/, BYTE * pFrameBuffer /*IN*/, ULONG nFrameWidth /*IN*/, ULONG nFrameHeight /*IN*/, ULONG nFramePitch /*IN*/, BYTE ** ppDstFrameBuffer, ULONG* pDstFrameBufferLen );

QCAP_EXT_API QRESULT QCAP_EXPORT QCAP_HI_CREATE_FRAME_POOL( ULONG nColorSpaceType /*IN*/, ULONG nFrameWidth /*IN*/, ULONG nFrameHeight /*IN*/, ULONG nFrames, PVOID* ppFramePool ) __attribute__ ((deprecated));
QCAP_EXT_API QRESULT QCAP_EXPORT QCAP_HI_GET_FRAME_BUFFER( PVOID pFramePool, BYTE** ppFrameBuffer /*IN*/, ULONG* pFrameBufferSize ) __attribute__ ((deprecated));
QCAP_EXT_API QRESULT QCAP_EXPORT QCAP_HI_DESTROY_FRAME_POOL( PVOID pFramePool ) __attribute__ ((deprecated));

QCAP_EXT_API QRESULT QCAP_EXPORT QCAP_HI_COPY_VIDEO_FRAME( BYTE * pSrcFrameBuffer /*IN*/, ULONG nSrcFrameBufferLen /*IN*/, BYTE * pDstFrameBuffer, ULONG nDstFrameBufferLen );

QCAP_EXT_API QRESULT QCAP_EXPORT QCAP_HI_SET_NATIVE_AUDIO_RENDERER_VOLUME( UINT nSoundNum /*IN*/, ULONG nVolume /*IN*/ );
QCAP_EXT_API QRESULT QCAP_EXPORT QCAP_HI_GET_NATIVE_AUDIO_RENDERER_VOLUME( UINT nSoundNum /*IN*/, ULONG* pVolume /*IN*/ );

// Motion detector
QCAP_EXT_API QRESULT QCAP_EXPORT QCAP_HI_REGISTER_MOTION_DETECT_CALLBACK( PVOID pVDAHandle /*IN*/, PF_MOTION_DETECT_CALLBACK pCB /*IN*/, PVOID pUserData /*IN*/ );

QCAP_EXT_API QRESULT QCAP_EXPORT QCAP_HI_CREATE_MOTION_MONITOR( PVOID *pVDAHandle /*OUT*/, ULONG nFrameWidth /*IN*/, ULONG nFrameHeight /*IN*/, ULONG VdaAlg /*IN*/, ULONG *nBlockWidth /*OUT*/, ULONG *nBlockHeight /*OUT*/);
QCAP_EXT_API QRESULT QCAP_EXPORT QCAP_HI_DESTROY_MOTION_MONITOR( PVOID pVDAHandle /*IN*/);
QCAP_EXT_API QRESULT QCAP_EXPORT QCAP_HI_START_MOTION_MONITOR( PVOID pVDAHandle /*IN*/);
QCAP_EXT_API QRESULT QCAP_EXPORT QCAP_HI_STOP_MOTION_MONITOR( PVOID pVDAHandle /*IN*/);
QCAP_EXT_API QRESULT QCAP_EXPORT QCAP_HI_SET_MOTION_DETECT_BUFFER( PVOID pVDAHandle /*IN*/, BYTE * pFrameBuffer /*IN*/, ULONG nFrameBufferLen /*IN*/);
QCAP_EXT_API QRESULT QCAP_EXPORT QCAP_HI_SET_MOTION_PROPERTY(PVOID pVDAHandle /*IN*/, ULONG nVdaIntvl /*IN*/, ULONG nBgUpSrcWgt /*IN*/, ULONG nSadTh /*IN*/);
QCAP_EXT_API QRESULT QCAP_EXPORT QCAP_HI_GET_MOTION_PROPERTY(PVOID pVDAHandle /*IN*/, ULONG *nVdaIntvl /*OUT*/, ULONG *nBgUpSrcWgt /*OUT*/, ULONG *nSadTh /*OUT*/);

// HDMI sink
QCAP_EXT_API QRESULT QCAP_EXPORT QCAP_HI_REGISTER_HDMI_SINK_CALLBACK( PF_HDMI_SINK_CALLBACK pCB /*IN*/, PVOID pUserData /*IN*/ );
QCAP_EXT_API QRESULT QCAP_EXPORT QCAP_HI_GET_HDMI_SINK_CAPS( PBYTE pData, ULONG nBytes); // pData: HI_HDMI_SINK_CAPABILITY_S (in)
QCAP_EXT_API QRESULT QCAP_EXPORT QCAP_HI_GET_HDMI_SINK_EDID( PBYTE pData, ULONG nBytes); // pData: HI_HDMI_EDID_S (in)

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//// GENERAL

// constant values
enum {
	QCAP_MAX_AUDIO_MIXER_SOURCE_COUNT = 16,
};

#pragma pack(push)
#pragma pack(1)

typedef struct QCAP_PROPERTY_AUDIO_MIXER_SOURCE_VOLUME_VALUE QCAP_PROPERTY_AUDIO_MIXER_SOURCE_VOLUME_VALUE;
struct QCAP_PROPERTY_AUDIO_MIXER_SOURCE_VOLUME_VALUE
{
	uint8_t volume[QCAP_MAX_AUDIO_MIXER_SOURCE_COUNT];
};

#pragma pack(pop)

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//// SC6D0N4

#define QCAP_PROPERTY_SC6D0N4_LED					0x00001000
#define QCAP_PROPERTY_SC6D0N4_MCU_VERSION			QCAP_PROPERTY_MCU_VERSION

typedef QCAP_PROPERTY_MCU_VERSION_RESPONSE QCAP_PROPERTY_SC6D0N4_MCU_VERSION_RESPONSE;

#pragma pack(push)
#pragma pack(1)

typedef struct QCAP_PROPERTY_SC6D0N4_LED_REQ QCAP_PROPERTY_SC6D0N4_LED_REQ;
struct QCAP_PROPERTY_SC6D0N4_LED_REQ
{
	unsigned char period;
	unsigned char red_start;
	unsigned char red_end;
	unsigned char green_start;
	unsigned char green_end;
};

typedef struct QCAP_PROPERTY_SC6D0N4_LED_VALUE QCAP_PROPERTY_SC6D0N4_LED_VALUE;
struct QCAP_PROPERTY_SC6D0N4_LED_VALUE
{
	QCAP_PROPERTY_SC6D0N4_LED_REQ led[5];
};

#pragma pack(pop)

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//// SC6D0N4SDI

#define QCAP_PROPERTY_SC6D0N4SDI_LED					0x06D0D400
#define QCAP_PROPERTY_SC6D0N4SDI_MCU_VERSION			QCAP_PROPERTY_MCU_VERSION
#define QCAP_PROPERTY_SC6D0N4SDI_BUTTON					0x06D0D402
#define QCAP_PROPERTY_SC6D0N4SDI_BUTTON_BACKLIGHT		0x06D0D403
#define QCAP_PROPERTY_SC6D0N4SDI_AUDIO_MIXER			0x06D0D404
#define QCAP_PROPERTY_SC6D0N4SDI_AUDIO_MIXER_VOLUME		0x06D0D405
// #define QCAP_PROPERTY_SC6D0N4SDI_AUDIO_INPUT_TYPE		0x06D0D406

typedef QCAP_PROPERTY_MCU_VERSION_RESPONSE QCAP_PROPERTY_SC6D0N4SDI_MCU_VERSION_OUT;

#pragma pack(push)
#pragma pack(1)

typedef struct QCAP_PROPERTY_SC6D0N4SDI_LED_IN QCAP_PROPERTY_SC6D0N4SDI_LED_IN;
struct QCAP_PROPERTY_SC6D0N4SDI_LED_IN
{
	struct SC6D0N4SDI_LED
	{
		unsigned char period;
		unsigned char green_start;
		unsigned char green_end;
		unsigned char red_start;
		unsigned char red_end;
	} led[5];
};

typedef struct QCAP_PROPERTY_SC6D0N4SDI_BUTTON_OUT QCAP_PROPERTY_SC6D0N4SDI_BUTTON_OUT;
struct QCAP_PROPERTY_SC6D0N4SDI_BUTTON_OUT
{
	int size;

	uint32_t state[8];
};

typedef struct QCAP_PROPERTY_SC6D0N4SDI_BUTTON_BACKLIGHT_IN QCAP_PROPERTY_SC6D0N4SDI_BUTTON_BACKLIGHT_IN;
struct QCAP_PROPERTY_SC6D0N4SDI_BUTTON_BACKLIGHT_IN
{
	struct SC6D0N4SDI_BUTTON_BACKLIGHT_LED
	{
		unsigned char period;
		unsigned char level;
		unsigned char red_start;
		unsigned char red_end;
		unsigned char green_start;
		unsigned char green_end;
		unsigned char blue_start;
		unsigned char blue_end;
	} led[13];

	int async_mode;
};

typedef struct QCAP_PROPERTY_SC6D0N4SDI_AUDIO_MIXER_IN QCAP_PROPERTY_SC6D0N4SDI_AUDIO_MIXER_IN;
struct QCAP_PROPERTY_SC6D0N4SDI_AUDIO_MIXER_IN
{
	unsigned char mixer_mask; // 1111 1111 => (LineIn4 LineIn3 LineIn2 LineIn1) (SDI4 SDI3 SDI2 SDI1)
};

typedef struct QCAP_PROPERTY_SC6D0N4SDI_AUDIO_MIXER_VOLUME_IN QCAP_PROPERTY_SC6D0N4SDI_AUDIO_MIXER_VOLUME_IN;
struct QCAP_PROPERTY_SC6D0N4SDI_AUDIO_MIXER_VOLUME_IN
{
	unsigned char mixer_volume[8];
};

// struct QCAP_PROPERTY_SC6D0N4SDI_AUDIO_INPUT_TYPE_IN
// {
// 	unsigned char input_type[4];
// };

#pragma pack(pop)

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//// SC632N8

#if 0
// value type = QCAP_PROPERTY_SC632N8_LED_VALUE
#define QCAP_PROPERTY_SC632N8_LED				0x06320800
// value type = QCAP_PROPERTY_SC632N8_SOURCE_VALUE
#define QCAP_PROPERTY_SC632N8_SOURCE			0x06320801
// value type = QCAP_PROPERTY_SC632N8_MCU_VERSION_RESPONSE
#define QCAP_PROPERTY_SC632N8_MCU_VERSION		0x06320802

typedef QCAP_PROPERTY_MCU_VERSION_RESPONSE QCAP_PROPERTY_SC632N8_MCU_VERSION_RESPONSE;

#pragma pack(push)
#pragma pack(1)

struct QCAP_PROPERTY_SC632N8_SOURCE_VALUE
{
	int channel;
	int type;
};

struct QCAP_PROPERTY_SC632N8_LED_VALUE
{
	int index;

	int period;
	int red_start;
	int red_end;
	int green_start;
	int green_end;
};

#pragma pack(pop)
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//// SC6D0N4SDIQL

#if 0
#define QCAP_PROPERTY_SC6D0N4SDIQL_LED				QCAP_PROPERTY_SC6D0N4SDI_LED
#define QCAP_PROPERTY_SC6D0N4SDIQL_MCU_VERSION		QCAP_PROPERTY_SC6D0N4SDI_MCU_VERSION

typedef QCAP_PROPERTY_SC6D0N4SDI_LED_IN QCAP_PROPERTY_SC6D0N4SDIQL_LED_IN;
typedef QCAP_PROPERTY_SC6D0N4SDI_MCU_VERSION_OUT QCAP_PROPERTY_SC6D0N4SDIQL_MCU_VERSION_OUT;
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//// SC6D0N4HDMI

#define QCAP_PROPERTY_SC6D0N4HDMI_LED					QCAP_PROPERTY_SC6D0N4SDI_LED
#define QCAP_PROPERTY_SC6D0N4HDMI_MCU_VERSION			QCAP_PROPERTY_SC6D0N4SDI_MCU_VERSION
#define QCAP_PROPERTY_SC6D0N4HDMI_BUTTON				QCAP_PROPERTY_SC6D0N4SDI_BUTTON
#define QCAP_PROPERTY_SC6D0N4HDMI_BUTTON_BACKLIGHT		QCAP_PROPERTY_SC6D0N4SDI_BUTTON_BACKLIGHT
#define QCAP_PROPERTY_SC6D0N4HDMI_AUDIO_MIXER			QCAP_PROPERTY_SC6D0N4SDI_AUDIO_MIXER
#define QCAP_PROPERTY_SC6D0N4HDMI_AUDIO_MIXER_VOLUME	QCAP_PROPERTY_SC6D0N4SDI_AUDIO_MIXER_VOLUME
// #define QCAP_PROPERTY_SC6D0N4HDMI_AUDIO_INPUT_TYPE		0x06D0D406

typedef QCAP_PROPERTY_MCU_VERSION_RESPONSE QCAP_PROPERTY_SC6D0N4HDMI_MCU_VERSION_OUT;
typedef QCAP_PROPERTY_SC6D0N4SDI_LED_IN QCAP_PROPERTY_SC6D0N4HDMI_LED_IN;
typedef QCAP_PROPERTY_SC6D0N4SDI_BUTTON_OUT QCAP_PROPERTY_SC6D0N4HDMI_BUTTON_OUT;
typedef QCAP_PROPERTY_SC6D0N4SDI_BUTTON_BACKLIGHT_IN QCAP_PROPERTY_SC6D0N4HDMI_BUTTON_BACKLIGHT_IN;
typedef QCAP_PROPERTY_SC6D0N4SDI_AUDIO_MIXER_IN QCAP_PROPERTY_SC6D0N4HDMI_AUDIO_MIXER_IN;
typedef struct QCAP_PROPERTY_SC6D0N4SDI_AUDIO_MIXER_VOLUME_IN QCAP_PROPERTY_SC6D0N4HDMI_AUDIO_MIXER_VOLUME_IN;

#pragma pack(push)
#pragma pack(1)

#if 0
struct QCAP_PROPERTY_SC6D0N4HDMI_LED_IN
{
	struct LED
	{
		unsigned char period;
		unsigned char green_start;
		unsigned char green_end;
		unsigned char red_start;
		unsigned char red_end;
	} led[5];
};

struct QCAP_PROPERTY_SC6D0N4HDMI_BUTTON_OUT
{
	int size;

	uint32_t state[8];
};

struct QCAP_PROPERTY_SC6D0N4HDMI_BUTTON_BACKLIGHT_IN
{
	struct LED
	{
		unsigned char period;
		unsigned char level;
		unsigned char red_start;
		unsigned char red_end;
		unsigned char green_start;
		unsigned char green_end;
		unsigned char blue_start;
		unsigned char blue_end;
	} led[13];

	int async_mode;
};

struct QCAP_PROPERTY_SC6D0N4HDMI_AUDIO_MIXER_IN
{
	unsigned char mixer_mask; // 1111 1111 => (LineIn4 LineIn3 LineIn2 LineIn1) (SDI4 SDI3 SDI2 SDI1)
};

struct QCAP_PROPERTY_SC6D0N4HDMI_AUDIO_MIXER_VOLUME_IN
{
	unsigned char mixer_volume[8];
};
#endif

// struct QCAP_PROPERTY_SC6D0N4HDMI_AUDIO_INPUT_TYPE_IN
// {
// 	unsigned char input_type[4];
// };

#pragma pack(pop)

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//// SC6D0N4HDMIQL

#define QCAP_PROPERTY_SC6D0N4HDMIQL_LED					QCAP_PROPERTY_SC6D0N4HDMI_LED
#define QCAP_PROPERTY_SC6D0N4HDMIQL_MCU_VERSION			QCAP_PROPERTY_SC6D0N4HDMI_MCU_VERSION

typedef QCAP_PROPERTY_SC6D0N4HDMI_LED_IN				QCAP_PROPERTY_SC6D0N4HDMIQL_LED_IN;
typedef QCAP_PROPERTY_SC6D0N4HDMI_MCU_VERSION_OUT		QCAP_PROPERTY_SC6D0N4HDMIQL_MCU_VERSION_OUT;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//// SC6D0N4HDMI40

#define QCAP_PROPERTY_SC6D0N4HDMI40_LED						QCAP_PROPERTY_SC6D0N4SDI_LED
#define QCAP_PROPERTY_SC6D0N4HDMI40_MCU_VERSION				QCAP_PROPERTY_SC6D0N4SDI_MCU_VERSION
#define QCAP_PROPERTY_SC6D0N4HDMI40_BUTTON					QCAP_PROPERTY_SC6D0N4SDI_BUTTON
#define QCAP_PROPERTY_SC6D0N4HDMI40_SATA_LED				0x06D0D504
#define QCAP_PROPERTY_SC6D0N4HDMI40_SATA_LOCK				0x06D0D505

typedef QCAP_PROPERTY_SC6D0N4SDI_BUTTON_OUT QCAP_PROPERTY_SC6D0N4HDMI40_BUTTON_OUT;

#pragma pack(push)
#pragma pack(1)

#if 0
struct QCAP_PROPERTY_SC6D0N4HDMI40_BUTTON_OUT
{
	int size;

	int state[8];
};
#endif

typedef struct QCAP_PROPERTY_SC6D0N4HDMI40_SATA_LED_IN QCAP_PROPERTY_SC6D0N4HDMI40_SATA_LED_IN;
struct QCAP_PROPERTY_SC6D0N4HDMI40_SATA_LED_IN
{
	int value;
};

typedef struct QCAP_PROPERTY_SC6D0N4HDMI40_SATA_LOCK_OUT QCAP_PROPERTY_SC6D0N4HDMI40_SATA_LOCK_OUT;
struct QCAP_PROPERTY_SC6D0N4HDMI40_SATA_LOCK_OUT
{
	int state;
};

#pragma pack(pop)

typedef QCAP_PROPERTY_SC6D0N4SDI_LED_IN				QCAP_PROPERTY_SC6D0N4HDMI40_LED_IN;
typedef QCAP_PROPERTY_SC6D0N4SDI_MCU_VERSION_OUT	QCAP_PROPERTY_SC6D0N4HDMI40_MCU_VERSION_OUT;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//// SC6D0N8

#define QCAP_PROPERTY_SC6D0N8_MCU_VERSION				0x06D0D801
typedef QCAP_PROPERTY_MCU_VERSION_RESPONSE QCAP_PROPERTY_SC6D0N8_MCU_VERSION_OUT;

#if 0
struct QCAP_PROPERTY_SC6D0N8_MCU_VERSION_OUT
{
	BYTE ver[4];
};
#endif

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif // __QCAP_HI3531A_H__
