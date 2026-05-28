# Audio Encoder and Decoder APIs

This document outlines the implementation details and constraints for the newly added `qcap2_audio_encoder_t` and `qcap2_audio_decoder_t` structures, which are backed by the FFmpeg (`libavcodec`) library.

## General Information

Both `qcap2_audio_encoder_t` and `qcap2_audio_decoder_t` manage the encoding and decoding of audio data via the FFmpeg `avcodec` APIs. The properties are set using `qcap2_audio_encoder_property_t`, and audio packets/frames are wrapped within `qcap2_rcbuffer_t`.

### Conversion Mapping

The APIs utilize a mapping function `qcap2_audio_format_to_ffmpeg_codec_id(ULONG nEncoderFormat)` which translates the QCAP encoder format definitions (e.g., `QCAP_ENCODER_FORMAT_AAC`, `QCAP_ENCODER_FORMAT_MP3`) to FFmpeg `AVCodecID` types (e.g., `AV_CODEC_ID_AAC`, `AV_CODEC_ID_MP3`).

## `qcap2_audio_encoder_t`

The audio encoder encapsulates the process of taking uncompressed `qcap2_av_frame_t` objects from `qcap2_rcbuffer_t` and producing compressed `qcap2_av_packet_t` payloads.

- **Initialization:** Created via `qcap2_audio_encoder_new()`. Properties must be set before calling `qcap2_audio_encoder_start()`.
- **Property configuration:** Managed by `qcap2_audio_encoder_set_audio_property()`. Ensures variables like the channel layout, bitrate, and sampling frequencies map accurately to `AVCodecContext`.
- **Execution:**
  - `qcap2_audio_encoder_push()` accepts an input uncompressed rc-buffer, wraps it as an `AVFrame`, and feeds it into the encoder using `avcodec_send_frame()`.
  - It sequentially calls `avcodec_receive_packet()` to drain generated compressed packets into internally-allocated `qcap2_rcbuffer_t` objects encapsulating `qcap2_av_packet_t`.
  - Synchronization (`mtx` and `cv`) protects the internal buffer queue.
  - Events set by `qcap2_audio_encoder_set_event()` notify consumers when packets are ready.

## `qcap2_audio_decoder_t`

The audio decoder is responsible for converting compressed `qcap2_av_packet_t` representations back into uncompressed `qcap2_av_frame_t` payloads.

- **Initialization:** Similar to the encoder, created with `qcap2_audio_decoder_new()`.
- **Property configuration:** Expects configuration similar to the encoder to instantiate the correctly associated decoding context. Extra data is especially critical here for formats like AAC.
- **Execution:**
  - `qcap2_audio_decoder_push()` pulls compressed data off the input `qcap2_rcbuffer_t` and funnels it into `avcodec_send_packet()`.
  - The decoded frames are retrieved using `avcodec_receive_frame()`, converted dynamically, and re-wrapped inside uncompressed `qcap2_av_frame_t` outputs.
  - Generates dynamically scoped buffers via standard malloc and binds them to the `qcap2_rcbuffer_t` release sequence using a custom deleter strategy.

## Implementation Details

- **Concurrency Model:** Thread safety is implemented using explicit standard `std::mutex` and `std::condition_variable` primitives on the API surface. Note that FFmpeg context accesses per instance are intrinsically synchronized within each operation scope.
- **Lifecycle Guarantees:**
  - Starting (`qcap2_audio_encoder_start`) and stopping (`qcap2_audio_encoder_stop`) contexts handle the safe cleanup of `AVCodecContext` states.
  - Output queues drain properly and execute resource release via `qcap2_rcbuffer_release()` cleanly upon destruction to ensure no memory leakage.
  - Do NOT modify input contexts or push elements to these interfaces post-destruction.
