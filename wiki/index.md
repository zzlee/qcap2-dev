# QCAP2 API Wiki

This directory contains implementation notes and usage constraints for QCAP2 APIs. Read relevant pages before refactoring or adding features.

## Buffer APIs

- [RCBUF.md](./RCBUF.md) — `qcap2_rcbuffer_t` lifetime, data pointer identity, embedded-member memory-layout rules, and `qcap2_container_of()` usage.
- [RCBUF-QUEUE.md](./RCBUF-QUEUE.md) — `qcap2_rcbuffer_queue_t` producer/consumer usage, event integration, queue lifecycle, and ownership handoff rules.
- [AV-FRAME-BUFFER.md](./AV-FRAME-BUFFER.md) — `qcap2_av_frame_alloc_buffer()` raw video allocation rules based on `QCAP_COLORSPACE_TYPE_XXX` layouts.

## Synchronization APIs

- [SYNC-EVENT-HANDLERS.md](./SYNC-EVENT-HANDLERS.md) — `qcap2_event_handlers_t` thread-based poll implementation, concurrency model, and invoke semantics.


## Graphics APIs

- [GRAPHICS.md](./GRAPHICS.md) — `qcap2_graphics_t` and `qcap2_font_atlas_t` CPU rendering, FreeType/HarfBuzz integration, and draw commands.

## Processing APIs

- [AUDIO-RESAMPLER.md](./AUDIO-RESAMPLER.md) — `qcap2_audio_resampler_t` thread-safe concurrent conversion, FFmpeg dynamic reinitialization, and no-leak lifecycle management.
- [VIDEO-SCALER.md](./VIDEO-SCALER.md) — `qcap2_video_scaler_t` dual-backend conversion (swscale/avfilter), cropping math, and zero-allocation buffer pool recycling.
- [VIDEO-ENCODER.md](./VIDEO-ENCODER.md) — `qcap2_video_encoder_t` C-compatible video encoding component using `libavcodec`, supporting software rate control, low-latency, and dynamic properties.
- [VIDEO-DECODER.md](./VIDEO-DECODER.md) — `qcap2_video_decoder_t` C-compatible video decoding component using `libavcodec`, supporting low-latency decoding, output scaling/colorspace format conversion, and registered buffer recycling.
- [FRAME-POOL.md](./FRAME-POOL.md) — `qcap2_frame_pool_t` pre-allocated frame buffer pool with reference-counted recycling for video and audio frames.
- [PACKET-POOL.md](./PACKET-POOL.md) — `qcap2_packet_pool_t` pre-allocated packet buffer pool with dynamic resizing and reference-counted recycling.
- [AUDIO-ENCODER-DECODER.md](./AUDIO-ENCODER-DECODER.md) — `qcap2_audio_encoder_t` and `qcap2_audio_decoder_t` FFmpeg-backed implementations, context mappings, and thread safety rules.

