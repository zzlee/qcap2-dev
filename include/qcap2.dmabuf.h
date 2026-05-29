#ifndef __QCAP2_DMABUF_H__
#define __QCAP2_DMABUF_H__

#include "qcap2.types.h"

typedef struct qcap2_dmabuf_t qcap2_dmabuf_t;

struct qcap2_dmabuf_t {
	int fd;
	size_t dmabuf_size;

	void* pVirAddr;
	uintptr_t nPhyAddr;
	size_t nSize;
};

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

// qcap2_av_frame_t
QRESULT qcap2_av_frame_set_dmabuf(qcap2_av_frame_t* pFrame, qcap2_dmabuf_t* pDMABuf);
QRESULT qcap2_av_frame_get_dmabuf(qcap2_av_frame_t* pFrame, qcap2_dmabuf_t** ppDMABuf);
QRESULT qcap2_av_frame_alloc_dmabuf(qcap2_av_frame_t* pFrame, int nSize, int nProt);
QRESULT qcap2_av_frame_free_dmabuf(qcap2_av_frame_t* pFrame);
QRESULT qcap2_av_frame_map_dmabuf(qcap2_av_frame_t* pFrame, int nProt);
QRESULT qcap2_av_frame_unmap_dmabuf(qcap2_av_frame_t* pFrame);
QRESULT qcap2_av_frame_alloc_mapped_dmabuf(qcap2_av_frame_t* pFrame, int nSize, int nProt);
QRESULT qcap2_av_frame_free_mapped_dmabuf(qcap2_av_frame_t* pFrame);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif // __QCAP2_DMABUF_H__