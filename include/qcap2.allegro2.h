#ifndef __QCAP2_ALLEGRO2_H__
#define __QCAP2_ALLEGRO2_H__

#include "qcap2.types.h"
#include "qcap2.processing.h"

// This header provides Allegro VCU2 (next-generation Xilinx video codec unit)
// specific configuration for the QCAP2 video encoder/decoder backends.
//
// These functions are only available when the encoder's nEncoderType property
// is set to QCAP_ENCODER_TYPE_ALLEGRO2 via
// qcap2_video_encoder_set_video_property().
//
// They must be called BEFORE qcap2_video_encoder_start().

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

// Filler data control mode for Allegro2 encoder.
// 0 = AL_FILLER_CTRL_AUTO, 1 = AL_FILLER_CTRL_OFF, 2 = AL_FILLER_CTRL_ON
void qcap2_video_encoder_set_filler_ctrl_mode(qcap2_video_encoder_t* pThis, int nFillerCtrlMode);

// Set Allegro2 encoder device path
void qcap2_video_encoder_set_allegro_device_path(qcap2_video_encoder_t* pThis, const char* path);

// Set Allegro2 encoder instance
void qcap2_video_encoder_set_allegro_instance(qcap2_video_encoder_t* pThis, int nInstance);

// Decoder input mode for Allegro2 decoder.
// 0 = AL_DEC_UNSPLIT_INPUT, 1 = AL_DEC_SPLIT_INPUT
void qcap2_video_decoder_set_input_mode(qcap2_video_decoder_t* pThis, int nInputMode);

// Set Allegro2 decoder device path
void qcap2_video_decoder_set_allegro_device_path(qcap2_video_decoder_t* pThis, const char* path);

// Set Allegro2 decoder instance
void qcap2_video_decoder_set_allegro_instance(qcap2_video_decoder_t* pThis, int nInstance);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif // __QCAP2_ALLEGRO2_H__
