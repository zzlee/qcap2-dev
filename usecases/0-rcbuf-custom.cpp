struct MyVideoFrame {
	int index;
	void* buffers[4];
	qcap2_av_frame_t av_frame;

	MyVideoFrame() {
		qcap2_av_frame_init(&av_frame);
		memset(buffers, 0, sizeof(buffers));
	}

	~MyVideoFrame() {
		for(int i = 0;i < 4;++i)
			free(buffers[i]);
	}

	static void _on_free_resource(PVOID pData) {
		MyVideoFrame* pThis = qcap2_container_of(pData, MyVideoFrame, av_frame);
		pThis->on_free_resource();
	}

	void on_free_resource() {
		uint8_t* pBuffer[4];
		int pStride[4];
		qcap2_av_frame_get_buffer1(&av_frame, pBuffer, pStride);

		LOGI("-%d: [%p/%p], [%d/%d]", index, pBuffer[0], pBuffer[1], pStride[0], pStride[1]);
	}
};

// _FreeStack_ is a free-function stack

MyVideoFrame* pVideoFrame = new MyVideoFrame();
_FreeStack_ += [pVideoFrame]() {
	delete pVideoFrame;
};

qcap2_rcbuffer_t* pRCBuffer = qcap2_rcbuffer_new(&pVideoFrame->av_frame, MyVideoFrame::_on_free_resource);
_FreeStack_ += [pRCBuffer]() {
	qcap2_rcbuffer_delete(pRCBuffer);
};

qcap2_av_frame_t* pAVFrame = (qcap2_av_frame_t*)qcap2_rcbuffer_get_data(pRCBuffer);
assert(pAVFrame == &pVideoFrame->av_frame);

qcap2_av_frame_set_video_property(pAVFrame, nColorSpaceType, nVideoWidth, nVideoHeight);

int pStride[4];
memset(pStride, 0, sizeof(pStride));
pStride[0] = __testkit__::align((int)nVideoWidth, 16);
pStride[1] = pStride[0];

err = posix_memalign(&pVideoFrame->buffers[0], 16, pStride[0] * nVideoHeight);
if(err) {
	LOGE("%s(%d): posix_memalign() failed, err=%d", __FUNCTION__, __LINE__, err);
	break;
}
err = posix_memalign(&pVideoFrame->buffers[1], 16, pStride[1] * nVideoHeight / 2);
if(err) {
	LOGE("%s(%d): posix_memalign() failed, err=%d", __FUNCTION__, __LINE__, err);
	break;
}

uint8_t* pBuffer[4];
memset(pBuffer, 0, sizeof(pBuffer));
pBuffer[0] = (uint8_t*)pVideoFrame->buffers[0];
pBuffer[1] = (uint8_t*)pVideoFrame->buffers[1];

qcap2_av_frame_set_buffer1(&pVideoFrame->av_frame, pBuffer, pStride);
LOGI("+%d: [%p/%p], [%d/%d]", i, pBuffer[0], pBuffer[1], pStride[0], pStride[1]);

_FreeStack_.flush(); // pop free-function and execute it