
// _FreeStack_ is a free-function stack

qcap2_rcbuffer_queue_t* pRCBufferQ = qcap2_rcbuffer_queue_new();
_FreeStack_ += [pRCBufferQ]() {
	qcap2_rcbuffer_queue_delete(pRCBufferQ);
};


// pRCBufferQ is a thread-safe queue storing rcbuf.

qcap2_rcbuffer_queue_start(pRCBufferQ);
_FreeStack_ += [pRCBufferQ]() {
	qcap2_rcbuffer_queue_stop(pRCBufferQ);
};

qcap2_event_t* pRCBufferQEvent = ...; // new event
qcap2_rcbuffer_queue_set_event(pRCBufferQ, pRCBufferQEvent);

// In Thread 1:
while(true) {
	qcap2_rcbuffer_t* pRCBuffer = ...; // acquired from object or user-defined
	qcap2_rcbuffer_queue_push(pRCBufferQ, pRCBufferQ);
}

// In Thread 2:
while(true) {
	qcap2_event_wait(pRCBufferQEvent);

	qcap2_rcbuffer_t* pRCBuffer;
	qcap2_rcbuffer_queue_pop(pRCBufferQ, &pRCBuffer);

	// DO somthing about pRCBuffer

	qcap2_rcbuffer_release(pRCBuffer); // call pOnFreeResource when weak-count decreasing to zero
}
