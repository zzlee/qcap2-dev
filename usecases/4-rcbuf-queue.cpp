
// _FreeStack_ is a free-function stack

qcap2_rcbuffer_queue_t* pRCBufferQ = qcap2_rcbuffer_queue_new();
_FreeStack_ += [pRCBufferQ]() {
	qcap2_rcbuffer_queue_delete(pRCBufferQ);
};

qcap2_event_t* pRCBufferQEvent = ...; // new event
qcap2_rcbuffer_queue_set_event(pRCBufferQ, pRCBufferQEvent);

qcap2_rcbuffer_t*[] pRCBuffers = ...; // array of qcap2_rcbuffer_t*
// prepare all pRCbuffers before start

// inject all these pRCBuffer into pRCBufferQ
qcap2_rcbuffer_queue_set_buffers(pRCBufferQ, pRCBuffers);
// HERE, qcap2_rcbuffer_queue_t will hook a pOnFreeResource for each pRCBuffer,
// when this pOnFreeResource is called, pRCBuffer is recycled back to pRCBufferQ

qcap2_rcbuffer_queue_start(pRCBufferQ); // NOTICE: start with a buffer array equipped
_FreeStack_ += [pRCBufferQ]() {
	qcap2_rcbuffer_queue_stop(pRCBufferQ);
};

while(true) {
	qcap2_event_wait(pRCBufferQEvent);

	qcap2_rcbuffer_t* pRCBuffer;
	qcap2_rcbuffer_queue_pop(pRCBufferQ, &pRCBuffer);

	// To use pRCBuffer.

	qcap2_rcbuffer_release(pRCBuffer); // recycle to pRCBufferQ when reference counter reaches zero (pOnFreeResource is called)
}

// IMPORTANT: this pRCBuffer recycling is a thread-safe operation