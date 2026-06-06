#ifndef __QCAP2_DRM_H__
#define __QCAP2_DRM_H__

#include "qcap2.types.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

// utilities
int qcap2_get_drm_fd();
void qcap2_put_drm_fd(int fd);

// qcap2_video_sink_t
void qcap2_video_sink_set_connector_id(qcap2_video_sink_t* pThis, uint32_t nConnectorId);
void qcap2_video_sink_set_crtc_id(qcap2_video_sink_t* pThis, uint32_t nCrtcId);
void qcap2_video_sink_set_plane_id(qcap2_video_sink_t* pThis, uint32_t nPlaneId);
void qcap2_video_sink_set_drm_modifier(qcap2_video_sink_t* pThis, uint64_t nModifier);
void qcap2_video_sink_set_drm_format(qcap2_video_sink_t* pThis, uint32_t nFormat);

void qcap2_video_sink_get_connector_id(qcap2_video_sink_t* pThis, uint32_t* pConnectorId);
void qcap2_video_sink_get_crtc_id(qcap2_video_sink_t* pThis, uint32_t* pCrtcId);
void qcap2_video_sink_get_plane_id(qcap2_video_sink_t* pThis, uint32_t* pPlaneId);
void qcap2_video_sink_get_drm_modifier(qcap2_video_sink_t* pThis, uint64_t* pModifier);
void qcap2_video_sink_get_drm_format(qcap2_video_sink_t* pThis, uint32_t* pFormat);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif // __QCAP2_DRM_H__