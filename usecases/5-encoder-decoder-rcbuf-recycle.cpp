// =============================================================================
// 5-encoder-decoder-rcbuf-recycle.cpp
//
// Demonstrates the complete rcbuf recycle process for video encoder and decoder
// using the HPR (input recycling) and PPR (output recycling) patterns.
//
// Pipeline:  frame_pool → Encoder → packet consumer → Decoder → frame consumer
//
// Two threads:
//   - Encoder thread: fills frames from frame_pool, pushes to encoder,
//     pops encoded packets, then feeds them to the decoder.
//   - Decoder thread: pops decoded frames, consumes them, recycles output.
//
// Both sides demonstrate zero-allocation steady-state via HPR + PPR recycling.
// =============================================================================

#include "qcap2.h"

// ---------------------------------------------------------------------------
//  Constants
// ---------------------------------------------------------------------------
static const ULONG  VIDEO_WIDTH        = 1920;
static const ULONG  VIDEO_HEIGHT       = 1080;
static const ULONG  COLORSPACE         = QCAP_COLORSPACE_TYPE_I420;
static const ULONG  ENCODER_FORMAT     = QCAP_ENCODER_FORMAT_H264;
static const double FRAME_RATE         = 30.0;
static const ULONG  BITRATE_KBPS       = 4000;
static const int    FRAME_POOL_COUNT   = 4;
static const int    PACKET_POOL_COUNT  = 4;
static const int    TOTAL_FRAMES       = 300; // encode 300 frames then stop

// ---------------------------------------------------------------------------
//  Encoder-side: HPR (input recycle) + PPR (output recycle)
//
//  This section shows the user's perspective of driving the video encoder
//  with full buffer recycling — no malloc/free after start().
// ---------------------------------------------------------------------------
static void encoder_thread_func(
	qcap2_video_encoder_t* pEncoder,
	qcap2_frame_pool_t*    pFramePool,
	qcap2_event_t*         pEncoderEvent,
	qcap2_video_decoder_t* pDecoder,
	bool*                  pDone
) {
	qcap2_rcbuffer_t* pInputRCBuffer  = NULL;
	qcap2_rcbuffer_t* pOutputRCBuffer = NULL;

	for (int i = 0; i < TOTAL_FRAMES; i++) {

		// =============================================================
		// STEP 1: Acquire an input frame buffer
		//
		//   First iteration:  get from frame_pool (cold start)
		//   Subsequent:       pop_input() recycles the previous frame (HPR)
		// =============================================================
		if (pInputRCBuffer == NULL) {
			// Cold start — acquire from pool
			QRESULT qr = qcap2_frame_pool_get_buffer(pFramePool, &pInputRCBuffer);
			if (qr != QCAP_RS_SUCCESSFUL) {
				LOGE("frame_pool_get_buffer() failed at frame %d", i);
				break;
			}
		}

		// --- Fill the frame with pixel data ---
		PVOID pData = qcap2_rcbuffer_lock_data(pInputRCBuffer);
		{
			qcap2_av_frame_t* pAVFrame = (qcap2_av_frame_t*)pData;

			uint8_t* pBuffer[4];
			int      pStride[4];
			qcap2_av_frame_get_buffer1(pAVFrame, pBuffer, pStride);

			// ... fill pBuffer[0], pBuffer[1], pBuffer[2] with Y/U/V data ...
			// (e.g. from camera capture, test pattern generator, etc.)

			qcap2_av_frame_set_pts(pAVFrame, (int64_t)i);
		}
		qcap2_rcbuffer_unlock_data(pInputRCBuffer);

		// =============================================================
		// STEP 2: Push the raw frame into the encoder
		//
		//   After push(), the encoder processes the frame synchronously
		//   and moves pInputRCBuffer into its internal input_recycled_queue.
		// =============================================================
		QRESULT qr = qcap2_video_encoder_push(pEncoder, pInputRCBuffer);
		if (qr != QCAP_RS_SUCCESSFUL) {
			LOGE("encoder_push() failed at frame %d", i);
			break;
		}

		// =============================================================
		// STEP 3 (HPR): Reclaim the consumed input frame
		//
		//   pop_input() retrieves our frame from input_recycled_queue.
		//   We reuse it on the next iteration — zero allocation!
		// =============================================================
		pInputRCBuffer = NULL;
		qcap2_video_encoder_pop_input(pEncoder, &pInputRCBuffer);
		// pInputRCBuffer is now ready to be refilled for the next push()

		// =============================================================
		// STEP 4: Pop the encoded output packet
		//
		//   The encoder produced an encoded packet; retrieve it.
		// =============================================================
		qcap2_event_wait(pEncoderEvent); // wait for packet ready notification

		pOutputRCBuffer = NULL;
		qr = qcap2_video_encoder_pop(pEncoder, &pOutputRCBuffer);
		if (qr != QCAP_RS_SUCCESSFUL || pOutputRCBuffer == NULL) {
			continue; // no packet this time (rare with zerolatency)
		}

		// --- Read the encoded packet data ---
		PVOID pPktData = qcap2_rcbuffer_lock_data(pOutputRCBuffer);
		{
			qcap2_av_packet_t* pAVPacket = (qcap2_av_packet_t*)pPktData;

			uint8_t* pPktBuffer = NULL;
			int      nPktSize   = 0;
			qcap2_av_packet_get_buffer(pAVPacket, &pPktBuffer, &nPktSize);

			BOOL bIsKeyFrame = FALSE;
			int  nStreamIdx  = 0;
			qcap2_av_packet_get_property(pAVPacket, &nStreamIdx, &bIsKeyFrame);

			LOGI("Encoded frame %d: size=%d, keyframe=%d", i, nPktSize, bIsKeyFrame);

			// --- Forward the encoded packet to the decoder ---
			qcap2_video_decoder_push(pDecoder, pOutputRCBuffer);
		}
		qcap2_rcbuffer_unlock_data(pOutputRCBuffer);

		// =============================================================
		// STEP 5 (PPR): Return the empty packet buffer to the encoder
		//
		//   push_output() sends the used packet shell back to the
		//   encoder's output_recycled_queue for reuse on the next encode.
		//   Then release() drops our reference.
		// =============================================================
		qcap2_video_encoder_push_output(pEncoder, pOutputRCBuffer);
		qcap2_rcbuffer_release(pOutputRCBuffer);
		pOutputRCBuffer = NULL;
	}

	// Release the last recycled input frame back to the pool
	if (pInputRCBuffer != NULL) {
		qcap2_rcbuffer_release(pInputRCBuffer);
		pInputRCBuffer = NULL;
	}

	*pDone = true;
}

// ---------------------------------------------------------------------------
//  Decoder-side: HPR (input recycle) + PPR (output recycle)
//
//  This section shows the user's perspective of consuming decoded frames
//  from the video decoder with full buffer recycling.
// ---------------------------------------------------------------------------
static void decoder_thread_func(
	qcap2_video_decoder_t* pDecoder,
	qcap2_event_t*         pDecoderEvent,
	bool*                  pDone
) {
	while (!(*pDone)) {

		// Wait for a decoded frame to be available
		qcap2_event_wait(pDecoderEvent);

		// =============================================================
		// STEP 1: Pop a decoded raw frame from the decoder
		// =============================================================
		qcap2_rcbuffer_t* pOutputRCBuffer = NULL;
		QRESULT qr = qcap2_video_decoder_pop(pDecoder, &pOutputRCBuffer);
		if (qr != QCAP_RS_SUCCESSFUL || pOutputRCBuffer == NULL) {
			continue; // stopped or empty
		}

		// --- Consume the decoded frame data ---
		PVOID pData = qcap2_rcbuffer_lock_data(pOutputRCBuffer);
		{
			qcap2_av_frame_t* pAVFrame = (qcap2_av_frame_t*)pData;

			uint8_t* pBuffer[4];
			int      pStride[4];
			qcap2_av_frame_get_buffer1(pAVFrame, pBuffer, pStride);

			int64_t nPTS = 0;
			qcap2_av_frame_get_pts(pAVFrame, &nPTS);

			LOGI("Decoded frame PTS=%lld: Y=%p, stride=%d", nPTS, pBuffer[0], pStride[0]);

			// ... render, display, save, or further process the frame ...
		}
		qcap2_rcbuffer_unlock_data(pOutputRCBuffer);

		// =============================================================
		// STEP 2 (PPR): Return the empty frame buffer to the decoder
		//
		//   push_output() sends the used frame shell back to the
		//   decoder's output_recycled_queue for reuse on the next decode.
		//   Then release() drops our reference.
		// =============================================================
		qcap2_video_decoder_push_output(pDecoder, pOutputRCBuffer);
		qcap2_rcbuffer_release(pOutputRCBuffer);
		pOutputRCBuffer = NULL;
	}

	// =============================================================
	// Decoder input recycling (HPR) is handled automatically:
	//   - encoder_thread calls decoder_push() which puts the consumed
	//     packet into input_recycled_queue
	//   - In a full pipeline the encoder_thread would call
	//     decoder_pop_input() to reclaim its packet buffers
	//
	// Example:
	//   qcap2_rcbuffer_t* pRecycledPacket = NULL;
	//   qcap2_video_decoder_pop_input(pDecoder, &pRecycledPacket);
	//   // reuse pRecycledPacket for the next decoder_push()
	// =============================================================
}

// ---------------------------------------------------------------------------
//  Main: wire up the full encode → decode pipeline with recycling
// ---------------------------------------------------------------------------
int main() {

	bool bDone = false;

	// =====================================================================
	// 1. Create a frame pool for encoder input frames
	// =====================================================================
	qcap2_frame_pool_t* pFramePool = qcap2_frame_pool_new();
	qcap2_frame_pool_set_frame_count(pFramePool, FRAME_POOL_COUNT);
	qcap2_frame_pool_set_video_property(pFramePool, COLORSPACE, VIDEO_WIDTH, VIDEO_HEIGHT);
	qcap2_frame_pool_set_video_frame_align(pFramePool, 16, 1);
	qcap2_frame_pool_start(pFramePool);

	// =====================================================================
	// 2. Create and configure the video encoder
	// =====================================================================
	qcap2_video_encoder_t* pEncoder = qcap2_video_encoder_new();

	qcap2_video_encoder_property_t* pEncProp = qcap2_video_encoder_property_new();
	qcap2_video_encoder_property_set_property(pEncProp,
		0,                         // nEncoderType  (software)
		ENCODER_FORMAT,            // nEncoderFormat
		COLORSPACE,                // nColorSpaceType
		VIDEO_WIDTH,               // nWidth
		VIDEO_HEIGHT,              // nHeight
		FRAME_RATE,                // dFrameRate
		QCAP_RECORD_MODE_CBR,      // nRecordMode
		0,                         // nQuality
		BITRATE_KBPS,              // nBitRate (kbps)
		30,                        // nGOP
		0, 0                       // nAspectRatioX, nAspectRatioY
	);
	qcap2_video_encoder_set_video_property(pEncoder, pEncProp);
	qcap2_video_encoder_property_delete(pEncProp);

	qcap2_event_t* pEncoderEvent = qcap2_event_new();
	qcap2_event_start(pEncoderEvent);
	qcap2_video_encoder_set_event(pEncoder, pEncoderEvent);

	qcap2_video_encoder_start(pEncoder);

	// =====================================================================
	// 3. Create and configure the video decoder
	// =====================================================================
	qcap2_video_decoder_t* pDecoder = qcap2_video_decoder_new();

	// Reuse encoder property for decoder (same format/resolution)
	qcap2_video_encoder_property_t* pDecProp = qcap2_video_encoder_property_new();
	qcap2_video_encoder_property_set_property(pDecProp,
		0, ENCODER_FORMAT, COLORSPACE,
		VIDEO_WIDTH, VIDEO_HEIGHT, FRAME_RATE,
		QCAP_RECORD_MODE_CBR, 0, BITRATE_KBPS, 30, 0, 0
	);
	qcap2_video_decoder_set_video_property(pDecoder, pDecProp);
	qcap2_video_encoder_property_delete(pDecProp);

	// Pass encoder's SPS/PPS to decoder
	uint8_t* pExtraData     = NULL;
	int      nExtraDataSize = 0;
	qcap2_video_encoder_get_extra_data(pEncoder, &pExtraData, &nExtraDataSize);
	if (pExtraData && nExtraDataSize > 0) {
		qcap2_video_decoder_set_extra_data(pDecoder, pExtraData, nExtraDataSize);
	}

	qcap2_event_t* pDecoderEvent = qcap2_event_new();
	qcap2_event_start(pDecoderEvent);
	qcap2_video_decoder_set_event(pDecoder, pDecoderEvent);

	qcap2_video_decoder_start(pDecoder);

	// =====================================================================
	// 4. Run the pipeline threads
	// =====================================================================
	// (In real code, use std::thread or pthread)
	// Thread A: encoder_thread_func(pEncoder, pFramePool, pEncoderEvent, pDecoder, &bDone);
	// Thread B: decoder_thread_func(pDecoder, pDecoderEvent, &bDone);

	// =====================================================================
	// 5. Cleanup (reverse order)
	// =====================================================================
	qcap2_video_decoder_stop(pDecoder);
	qcap2_video_encoder_stop(pEncoder);

	qcap2_event_stop(pDecoderEvent);
	qcap2_event_delete(pDecoderEvent);

	qcap2_event_stop(pEncoderEvent);
	qcap2_event_delete(pEncoderEvent);

	qcap2_video_decoder_delete(pDecoder);
	qcap2_video_encoder_delete(pEncoder);

	qcap2_frame_pool_stop(pFramePool);
	qcap2_frame_pool_delete(pFramePool);

	return 0;
}

// =============================================================================
// SUMMARY OF THE RCBUF RECYCLE FLOW
//
// ┌──────────────────── Encoder Side ────────────────────┐
// │                                                      │
// │  frame_pool ──get_buffer()──► pInputRCBuffer         │
// │       ▲                            │                 │
// │       │                     push(pInputRCBuffer)     │
// │       │                            │                 │
// │       │                            ▼                 │
// │       │                     ┌─── Encoder ───┐        │
// │       │                     │ encode + move  │        │
// │       │                     │ input to       │        │
// │       │                     │ recycled_queue │        │
// │       │                     └───────────────┘        │
// │       │                            │                 │
// │  (after last frame)         pop_input() ◄──── HPR    │
// │  release() to pool                 │                 │
// │       ▲                            ▼                 │
// │       └──── reuse pInputRCBuffer on next iteration   │
// │                                                      │
// │  pop() ────────────────► pOutputRCBuffer             │
// │                                │                     │
// │                         (read packet data)           │
// │                                │                     │
// │                         push_output() ──────── PPR   │
// │                         release()                    │
// │                                │                     │
// │                                ▼                     │
// │                         encoder reuses packet buffer  │
// └──────────────────────────────────────────────────────┘
//
// ┌──────────────────── Decoder Side ────────────────────┐
// │                                                      │
// │  (packet arrives from encoder via decoder_push())    │
// │                            │                         │
// │                            ▼                         │
// │                     ┌─── Decoder ───┐                │
// │                     │ decode + move  │                │
// │                     │ input to       │                │
// │                     │ recycled_queue │                │
// │                     └───────────────┘                │
// │                            │                         │
// │  pop() ────────────────► pOutputRCBuffer             │
// │                                │                     │
// │                         (read frame data)            │
// │                                │                     │
// │                         push_output() ──────── PPR   │
// │                         release()                    │
// │                                │                     │
// │                                ▼                     │
// │                         decoder reuses frame buffer   │
// └──────────────────────────────────────────────────────┘
// =============================================================================
