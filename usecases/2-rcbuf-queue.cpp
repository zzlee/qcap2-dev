
// _FreeStack_ is a free-function stack

qcap2_rcbuffer_queue_t* pRCBufferQ = qcap2_rcbuffer_queue_new();
_FreeStack_ += [pRCBufferQ]() {
	qcap2_rcbuffer_queue_delete(pRCBufferQ);
};

qcap2_event_t* pRCBufferQEvent = ...; // new event
qcap2_rcbuffer_queue_set_event(pRCBufferQ, pRCBufferQEvent);

qcap2_rcbuffer_t*[] pRCBuffers = ...; // array of qcap2_rcbuffer_t*
qcap2_rcbuffer_queue_set_buffers(pRCBufferQ, pRCBuffers);

qcap2_rcbuffer_queue_start(pRCBufferQ);
_FreeStack_ += [pRCBufferQ]() {
	qcap2_rcbuffer_queue_stop(pRCBufferQ);
};

qcap2_rcbuffer_queue_t* pRCBufferSourceQ = qcap2_rcbuffer_queue_new();
_FreeStack_ += [pRCBufferSourceQ]() {
	qcap2_rcbuffer_queue_delete(pRCBufferSourceQ);
};

qcap2_event_t* pRCBufferSourceQEvent = ...; // new event
qcap2_rcbuffer_queue_set_event(pRCBufferSourceQ, pRCBufferSourceQEvent);

qcap2_rcbuffer_queue_start(pRCBufferSourceQ);
_FreeStack_ += [pRCBufferSourceQ]() {
	qcap2_rcbuffer_queue_stop(pRCBufferSourceQ);
};

// In Thread 1:
while(true) {
	qcap2_event_wait(pRCBufferQEvent);

	qcap2_rcbuffer_t* pRCBuffer;
	qcap2_rcbuffer_queue_pop(pRCBufferQ, &pRCBuffer);

	// To prepare pRCBuffer.

	qcap2_rcbuffer_queue_push(pRCBufferSourceQ, pRCBuffer);
}

// In Thread 2:
while(true) {
	qcap2_event_wait(pRCBufferSourceQEvent);

	qcap2_rcbuffer_t* pRCBuffer;
	qcap2_rcbuffer_queue_pop(pRCBufferSourceQ, &pRCBuffer);

	// To use pRCBuffer.

	qcap2_rcbuffer_queue_push(pRCBufferQ, pRCBuffer);
}
