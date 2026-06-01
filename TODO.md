# QCAP2 SDK Backend Implementation Status & TODO List

This document tracks the implementation status of all backend types defined in `include/qcap2.types.h` across the different QCAP2 SDK modules.

---

## 1. Video Source Module (`qcap2_video_source_t`)
**Implementation File**: [src/qcap2.devices.cpp](file:///home/zzlee/docker/qcap2-dev/src/qcap2.devices.cpp)
**Status Summary**: Core local, simulated, and custom injection backends are fully implemented. Specific proprietary/embedded hardware platform backends remain unimplemented.

- [x] **`QCAP2_VIDEO_SOURCE_BACKEND_TYPE_V4L2`** — **[FULLY IMPLEMENTED]**
  - Fully supports `MMAP`, `USERPTR`, and `DMABUF` (using genuine `VIDIOC_EXPBUF` export or mock temp-file imports) memory modes.
  - Features high-performance automatic reference-counted re-queueing on free and data-plane pinning (`lock_data`/`unlock_data`).
- [x] **`QCAP2_VIDEO_SOURCE_BACKEND_TYPE_USER`** — **[FULLY IMPLEMENTED]**
  - Custom user-space buffer injection and queueing.
- [x] **`QCAP2_VIDEO_SOURCE_BACKEND_TYPE_TPG`** — **[FULLY IMPLEMENTED]**
  - Simulated Test Pattern Generator backend running on a precise background thread.
- [ ] **Proprietary & Platform-Specific Video Source Backends** — **[UNIMPLEMENTED]**
  - *TODO*: Implement hardware-specific drivers and lifecycle interfaces.
  - [ ] `QCAP2_VIDEO_SOURCE_BACKEND_TYPE_PYLON` (Basler cameras)
  - [ ] `QCAP2_VIDEO_SOURCE_BACKEND_TYPE_VITIS` (Xilinx Vitis capture)
  - [ ] `QCAP2_VIDEO_SOURCE_BACKEND_TYPE_XLNX` (Xilinx capture)
  - [ ] `QCAP2_VIDEO_SOURCE_BACKEND_TYPE_V4L2_SG` (V4L2 Scatter-Gather)
  - [ ] `QCAP2_VIDEO_SOURCE_BACKEND_TYPE_LBLWR` (Low bandwidth capture)
  - [ ] `QCAP2_VIDEO_SOURCE_BACKEND_TYPE_NVT_HDAL` (Novatek HDAL)
  - [ ] `QCAP2_VIDEO_SOURCE_BACKEND_TYPE_HSB` (Holographic stream)
  - [ ] `QCAP2_VIDEO_SOURCE_BACKEND_TYPE_LT6911` (HDMI-to-MIPI bridge capture)
  - [ ] `QCAP2_VIDEO_SOURCE_BACKEND_TYPE_IMX585` (Sony IMX585 image sensor capture)
  - [ ] `QCAP2_VIDEO_SOURCE_BACKEND_TYPE_COE` (Custom COE capture)

---

## 2. Audio Source Module (`qcap2_audio_source_t`)
**Implementation File**: [src/qcap2.devices.cpp](file:///home/zzlee/docker/qcap2-dev/src/qcap2.devices.cpp)
**Status Summary**: ALSA audio capture and sine-wave TPG simulator backends are fully implemented.

- [x] **`QCAP2_AUDIO_SOURCE_BACKEND_TYPE_ALSA`** — **[FULLY IMPLEMENTED]**
  - PCM audio capture from standard ALSA cards/devices with zero-copy reference-counted buffer wrapping.
- [x] **`QCAP2_AUDIO_SOURCE_BACKEND_TYPE_TPG`** — **[FULLY IMPLEMENTED]**
  - High-precision sine-wave generator simulation backend.
- [x] **`QCAP2_AUDIO_SOURCE_BACKEND_TYPE_UNKNOWN`** — **[FALLBACK / USER IMPLEMENTED]**
  - Fallback to `qcap2_user_audio_source_backend_t` for user-injected audio frames.
- [ ] **Proprietary & Platform-Specific Audio Source Backends** — **[UNIMPLEMENTED]**
  - *TODO*: Implement platform PCM audio capture backends.
  - [ ] `QCAP2_AUDIO_SOURCE_BACKEND_TYPE_VITIS`
  - [ ] `QCAP2_AUDIO_SOURCE_BACKEND_TYPE_V4L2` (V4L2-based audio capture)
  - [ ] `QCAP2_AUDIO_SOURCE_BACKEND_TYPE_NVT_HDAL`

---

## 3. Video Scaler Module (`qcap2_video_scaler_t`)
**Implementation File**: [src/qcap2.processing.cpp](file:///home/zzlee/docker/qcap2-dev/src/qcap2.processing.cpp)
**Status Summary**: Software scaling via FFmpeg swscale and complex filtering via filter graphs are fully implemented.

- [x] **`QCAP2_VIDEO_SCALER_BACKEND_TYPE_DEFAULT`** — **[FULLY IMPLEMENTED]**
  - Maps to the high-performance software swscale backend.
- [x] **`QCAP2_VIDEO_SCALER_BACKEND_TYPE_FF_FILTER_GRAPH`** — **[FULLY IMPLEMENTED]**
  - Uses FFmpeg libavfilter for advanced scaling, cropping, and color conversion.
- [ ] **Hardware-Accelerated Scaler Backends** — **[UNIMPLEMENTED]**
  - *TODO*: Integrate GPU/DSP-based scalers.
  - [ ] `QCAP2_VIDEO_SCALER_BACKEND_TYPE_NPP` (NVIDIA Performance Primitives)
  - [ ] `QCAP2_VIDEO_SCALER_BACKEND_TYPE_LBL_COPY`
  - [ ] `QCAP2_VIDEO_SCALER_BACKEND_TYPE_NVT_HDAL`

---

## 4. Window / Renderer Module (`qcap2_window_t`)
**Implementation File**: [src/qcap2.sync.cpp](file:///home/zzlee/docker/qcap2-dev/src/qcap2.sync.cpp)
**Status Summary**: Mock rendering and X11 window backends are fully implemented.

- [x] **`QCAP2_WINDOW_BACKEND_TYPE_FAKE`** — **[FULLY IMPLEMENTED]**
  - Mock renderer used for testing.
- [x] **`QCAP2_WINDOW_BACKEND_TYPE_X11`** — **[FULLY IMPLEMENTED]**
  - Standard X11 window display rendering.
- [ ] **Other Window Backends** — **[UNIMPLEMENTED]**
  - [ ] `QCAP2_WINDOW_BACKEND_TYPE_NULL` (No-op renderer)

---

## 5. Video Sink Module (`qcap2_video_sink_t`)
**Implementation File**: [src/qcap2.devices.cpp](file:///home/zzlee/docker/qcap2-dev/src/qcap2.devices.cpp)
**Status Summary**: Standard V4L2 output backend is fully implemented. Specific proprietary/embedded hardware platform backends remain unimplemented.

- [x] **`QCAP2_VIDEO_SINK_BACKEND_TYPE_V4L2`** — **[FULLY IMPLEMENTED]**
  - Thread-safe, asynchronous V4L2 playback backend writing video frames directly to Linux `/dev/video%d` or custom configured V4L2 device nodes.
- [ ] **Proprietary & Platform-Specific Video Sink Backends** — **[UNIMPLEMENTED]**
  - *TODO*: Implement hardware-specific drivers and lifecycle interfaces.
  - [ ] `QCAP2_VIDEO_SINK_BACKEND_TYPE_DAVMF`
  - [ ] `QCAP2_VIDEO_SINK_BACKEND_TYPE_GSTREAMER`
  - [ ] `QCAP2_VIDEO_SINK_BACKEND_TYPE_VITIS`
  - [ ] `QCAP2_VIDEO_SINK_BACKEND_TYPE_V4L2CAP`
  - [ ] `QCAP2_VIDEO_SINK_BACKEND_TYPE_XLNX`
  - [ ] `QCAP2_VIDEO_SINK_BACKEND_TYPE_L4T`
  - [ ] `QCAP2_VIDEO_SINK_BACKEND_TYPE_LBLRD`
  - [ ] `QCAP2_VIDEO_SINK_BACKEND_TYPE_NVT_HDAL`

---

## 6. Audio Sink Module (`qcap2_audio_sink_t`)
**Implementation File**: [src/qcap2.devices.cpp](file:///home/zzlee/docker/qcap2-dev/src/qcap2.devices.cpp)
**Status Summary**: Standard ALSA playback backend is fully implemented. Specific proprietary/embedded hardware platform backends remain unimplemented.

- [x] **`QCAP2_AUDIO_SINK_BACKEND_TYPE_ALSA`** — **[FULLY IMPLEMENTED]**
  - Thread-safe, asynchronous ALSA playback backend writing PCM frames directly to Linux `/dev/snd/pcmC%dD%dp` sound character devices.
- [ ] **Proprietary & Platform-Specific Audio Sink Backends** — **[UNIMPLEMENTED]**
  - *TODO*: Implement platform PCM audio playback backends.
  - [ ] `QCAP2_AUDIO_SINK_BACKEND_TYPE_VITIS`
  - [ ] `QCAP2_AUDIO_SINK_BACKEND_TYPE_V4L2CAP`
  - [ ] `QCAP2_AUDIO_SINK_BACKEND_TYPE_NVT_HDAL`

---

## 7. Frame Pool Module (`qcap2_frame_pool_t`)
**Implementation File**: [src/qcap2.processing.cpp](file:///home/zzlee/docker/qcap2-dev/src/qcap2.processing.cpp)
**Status Summary**: Frame pool is functional using the default memory allocator. Specialized allocators are unimplemented.
- [x] **`QCAP2_FRAME_POOL_BACKEND_TYPE_DEFAULT`** — **[FULLY IMPLEMENTED]**
- [ ] **Specialized Pool Backends** — **[UNIMPLEMENTED]**
  - *TODO*: Implement hardware-integrated pool allocators.
  - [ ] `QCAP2_FRAME_POOL_BACKEND_TYPE_RKMPP` (Rockchip Media Processing Platform pool)
  - [ ] `QCAP2_FRAME_POOL_BACKEND_TYPE_QDMABUF` (DMA-buf allocation-integrated pool)

---

## 8. Demuxer Module (`qcap2_demuxer_t`)
**Implementation File**: [src/qcap2.demuxer.cpp](file:///home/zzlee/docker/qcap2-dev/src/qcap2.demuxer.cpp)
**Status Summary**: Default FFmpeg demuxer with advanced program parsing, DTS-based interruptible pacing, stream metadata embedding, and direct compressed packet delivery queues is fully implemented.

- [x] **`QCAP2_DEMUXER_TYPE_DEFAULT`** — **[FULLY IMPLEMENTED]**
  - High-performance default backend using FFmpeg APIs.
  - Features concurrent audio/video encoder matching and initialization.
  - Features stream extra-data extraction and propagation.
  - Features thread-safe reference-counted packet queue pushing and delivery.
  - Features highly responsive steady-clock pacing via interruptible condition variables.
- [ ] **Proprietary & Custom Demuxer Backends** — **[UNIMPLEMENTED]**
  - *TODO*: Implement hardware or proprietary demuxers.
  - [ ] `QCAP2_DEMUXER_TYPE_PYLON`
  - [ ] `QCAP2_DEMUXER_TYPE_USBCAM`
  - [ ] `QCAP2_DEMUXER_TYPE_FIFO`
  - [ ] `QCAP2_DEMUXER_TYPE_RTP`
  - [ ] `QCAP2_DEMUXER_TYPE_JSRTSP`
  - [ ] `QCAP2_DEMUXER_TYPE_VITIS`
  - [ ] `QCAP2_DEMUXER_TYPE_YUANCAP`
  - [ ] `QCAP2_DEMUXER_TYPE_NVT_HDAL`
  - [ ] `QCAP2_DEMUXER_TYPE_SC6F0`

---

## 9. Muxer Module (`qcap2_muxer_t`)
**Implementation File**: [src/qcap2.muxer.cpp](file:///home/zzlee/docker/qcap2-dev/src/qcap2.muxer.cpp)
**Status Summary**: Default FFmpeg-backed muxer with advanced stream mapping, extra-data propagation, and background DTS-interleaved writing thread is fully implemented.

- [x] **`QCAP2_MUXER_TYPE_DEFAULT`** — **[FULLY IMPLEMENTED]**
  - High-performance default backend using FFmpeg APIs.
  - Features dynamic video/audio decoder auto-creation and stream mappings.
  - Features stream codec parameters and extra-data (SPS/PPS, AudioSpecificConfig) propagation.
  - Features reactive background DTS-interleaved multiplexer thread with precise timebase conversion.
- [ ] **Proprietary & Streaming Muxer Backends** — **[UNIMPLEMENTED]**
  - *TODO*: Implement RTSP/JSRTSP or platform-specific muxer types.
  - [ ] `QCAP2_MUXER_TYPE_JSRTSP`
  - [ ] `QCAP2_MUXER_TYPE_RTSP`

---

## 10. Audio Resampler Module (`qcap2_audio_resampler_t`)
**Implementation File**: [src/qcap2.processing.cpp](file:///home/zzlee/docker/qcap2-dev/src/qcap2.processing.cpp)
**Status Summary**: Single unified high-performance resampler backed by FFmpeg libswresample is fully implemented.

- [x] **`qcap2_audio_resampler_t` Module & APIs** — **[FULLY IMPLEMENTED]**
  - Implements a single, unified backend using `SwrContext` from FFmpeg's `libswresample`.
  - Supports all audio format properties, channel layouts, sampling frequencies, and frame sizes conversion.
  - Fully supports thread-safe packet queueing, pushing, and polling.

