// In Thread 1
QRESULT qres;
while(true) {
	qcap2_rcbuffer_t* pRCBuffer;
	qres = qcap2_rcbuffer_queue_push(pRCBufferQ, pRCBuffer); // qcap2_rcbuffer_add_ref(pRCBuffer) is called when pushing is succeeded
	if(qres != QCAP_RS_SUCCESSFUL) {
		// error handling
	}

	...
}

// In Thread 2
QRESULT qres;
while(true) {
	qcap2_rcbuffer_t* pRCBuffer;
	qres = qcap2_rcbuffer_queue_pop(pRCBufferQ, &pRCBuffer);
	if(qres != QCAP_RS_SUCCESSFUL) {
		// error handling
	}

	// To use pRCBuffer

	// ALWAYS release or resource leakage occured
	qcap2_rcbuffer_release(pRCBuffer);
}

