#ifndef __QCAP2_ALLEGRO_H__
#define __QCAP2_ALLEGRO_H__

#include "qcap2.types.h"
#include "qcap2.processing.h"

// This header provides Allegro VCU (Xilinx video codec unit) specific
// configuration for the QCAP2 video encoder/decoder backends.
//
// These functions are only available when the encoder/decoder's nEncoderType
// property is set to QCAP_ENCODER_TYPE_ALLEGRO or QCAP_DECODER_TYPE_ALLEGRO
// via qcap2_video_encoder_set_video_property() /
// qcap2_video_decoder_set_video_property().
//
// They must be called BEFORE qcap2_video_encoder_start() /
// qcap2_video_decoder_start().

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

// Filler data control mode for Allegro encoder.
// 0 = AL_FILLER_CTRL_AUTO, 1 = AL_FILLER_CTRL_OFF, 2 = AL_FILLER_CTRL_ON
// Valid range [0..2]. Default: 0 (auto).
void qcap2_video_encoder_set_filler_ctrl_mode(qcap2_video_encoder_t* pThis, int nFillerCtrlMode);

// Set Allegro encoder device path (e.g. "/dev/al5e")
void qcap2_video_encoder_set_allegro_device_path(qcap2_video_encoder_t* pThis, const char* path);

// Set Allegro encoder instance index (default: 0)
void qcap2_video_encoder_set_allegro_instance(qcap2_video_encoder_t* pThis, int nInstance);

// Decoder input mode for Allegro decoder.
// 0 = AL_DEC_UNSPLIT_INPUT (decoder finds frames itself)
// 1 = AL_DEC_SPLIT_INPUT (each buffer is one decoding unit)
void qcap2_video_decoder_set_input_mode(qcap2_video_decoder_t* pThis, int nInputMode);

// Set Allegro decoder device path (e.g. "/dev/al5d")
void qcap2_video_decoder_set_allegro_device_path(qcap2_video_decoder_t* pThis, const char* path);

// Set Allegro decoder instance index (default: 0)
void qcap2_video_decoder_set_allegro_instance(qcap2_video_decoder_t* pThis, int nInstance);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif // __QCAP2_ALLEGRO_H__
