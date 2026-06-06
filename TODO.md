# QCAP2 SDK Implementation Status & TODO List

This document tracks the implementation status and outstanding tasks for the QCAP2 SDK, including unimplemented public APIs, backend modules, core infrastructure, known bugs, and architectural improvements.

---

## 1. Unimplemented Public APIs (by Header)

### [qcap2.allegro.h](file:///home/zzlee/qcap2-dev/include/qcap2.allegro.h)
- [ ] `qcap2_video_decoder_set_input_mode`
- [ ] `qcap2_video_encoder_set_filler_ctrl_mode`

### [qcap2.alsa.h](file:///home/zzlee/qcap2-dev/include/qcap2.alsa.h)
- [ ] `qcap2_audio_sink_set_alsa_card`
- [ ] `qcap2_audio_sink_set_alsa_device`
- [ ] `qcap2_audio_source_set_alsa_card`
- [ ] `qcap2_audio_source_set_alsa_device`

### [qcap2.coe.h](file:///home/zzlee/qcap2-dev/include/qcap2.coe.h)
- [ ] `qcap2_video_source_set_config_file`
- [ ] `qcap2_video_source_set_verbosity`

### [qcap2.cuda.h](file:///home/zzlee/qcap2-dev/include/qcap2.cuda.h)
- [ ] `qcap2_av_frame_alloc_cuda_buffer`
- [ ] `qcap2_av_frame_alloc_cuda_host_buffer`
- [ ] `qcap2_av_frame_alloc_cuda_managed_buffer`
- [ ] `qcap2_av_frame_free_cuda_buffer`
- [ ] `qcap2_av_frame_free_cuda_host_buffer`
- [ ] `qcap2_av_frame_free_cuda_managed_buffer`
- [ ] `qcap2_cuda_device_synchronize`
- [ ] `qcap2_kernel_mask_comp`
- [ ] `qcap2_kernel_sbs`
- [ ] `qcap2_rcbuffer_get_cures`
- [ ] `qcap2_rcbuffer_get_mapped_eglbuffer`
- [ ] `qcap2_rcbuffer_get_mapped_eglframe`

### [qcap2.devices.h](file:///home/zzlee/qcap2-dev/include/qcap2.devices.h)
- [ ] `qcap2_audio_sink_delete`
- [ ] `qcap2_audio_sink_new`
- [ ] `qcap2_audio_sink_pop`
- [ ] `qcap2_audio_sink_push`
- [ ] `qcap2_audio_sink_set_audio_format`
- [ ] `qcap2_audio_sink_set_backend_type`
- [ ] `qcap2_audio_sink_set_buffer_time`
- [ ] `qcap2_audio_sink_set_card`
- [ ] `qcap2_audio_sink_set_device`
- [ ] `qcap2_audio_sink_set_period_time`
- [ ] `qcap2_audio_sink_start`
- [ ] `qcap2_audio_sink_stop`
- [ ] `qcap2_audio_source_push`
- [ ] `qcap2_clock_source_delete`
- [ ] `qcap2_clock_source_new`
- [ ] `qcap2_clock_source_pop`
- [ ] `qcap2_clock_source_set_event`
- [ ] `qcap2_clock_source_set_listen_address`
- [ ] `qcap2_clock_source_set_polling_time`
- [ ] `qcap2_clock_source_set_port`
- [ ] `qcap2_clock_source_set_resync_range`
- [ ] `qcap2_clock_source_set_server_address`
- [ ] `qcap2_clock_source_start`
- [ ] `qcap2_clock_source_stop`
- [ ] `qcap2_dns_source_delete`
- [ ] `qcap2_dns_source_new`
- [ ] `qcap2_dns_source_pop`
- [ ] `qcap2_dns_source_set_event`
- [ ] `qcap2_dns_source_set_host_name`
- [ ] `qcap2_dns_source_set_listen_address`
- [ ] `qcap2_dns_source_set_multicast_address`
- [ ] `qcap2_dns_source_set_polling_time`
- [ ] `qcap2_dns_source_set_port`
- [ ] `qcap2_dns_source_start`
- [ ] `qcap2_dns_source_stop`
- [ ] `qcap2_muxer_add_program_info`
- [ ] `qcap2_muxer_add_user`
- [ ] `qcap2_muxer_delete`
- [ ] `qcap2_muxer_get_audio_decoder`
- [ ] `qcap2_muxer_get_audio_decoder_count`
- [ ] `qcap2_muxer_get_audio_sink`
- [ ] `qcap2_muxer_get_audio_sink_count`
- [ ] `qcap2_muxer_get_program_count`
- [ ] `qcap2_muxer_get_program_info`
- [ ] `qcap2_muxer_get_video_decoder`
- [ ] `qcap2_muxer_get_video_decoder_count`
- [ ] `qcap2_muxer_get_video_sink`
- [ ] `qcap2_muxer_get_video_sink_count`
- [ ] `qcap2_muxer_new`
- [ ] `qcap2_muxer_play`
- [ ] `qcap2_muxer_set_certificate_chain_file`
- [ ] `qcap2_muxer_set_endpoint`
- [ ] `qcap2_muxer_set_max_threads`
- [ ] `qcap2_muxer_set_private_key_file`
- [ ] `qcap2_muxer_set_realm`
- [ ] `qcap2_muxer_set_ssl`
- [ ] `qcap2_muxer_set_type`
- [ ] `qcap2_muxer_start`
- [ ] `qcap2_muxer_stop`
- [ ] `qcap2_qdev_config_video_source`
- [ ] `qcap2_qdev_delete`
- [ ] `qcap2_qdev_enum_delete`
- [ ] `qcap2_qdev_enum_new`
- [ ] `qcap2_qdev_enum_pop`
- [ ] `qcap2_qdev_enum_set_event`
- [ ] `qcap2_qdev_enum_set_type`
- [ ] `qcap2_qdev_enum_start`
- [ ] `qcap2_qdev_enum_stop`
- [ ] `qcap2_qdev_get_audio_count`
- [ ] `qcap2_qdev_get_audio_encoder_count`
- [ ] `qcap2_qdev_get_audio_input`
- [ ] `qcap2_qdev_get_video_count`
- [ ] `qcap2_qdev_get_video_encoder_count`
- [ ] `qcap2_qdev_get_video_input`
- [ ] `qcap2_qdev_info_get_device_id`
- [ ] `qcap2_qdev_info_get_path`
- [ ] `qcap2_qdev_info_get_plugged`
- [ ] `qcap2_qdev_info_get_type`
- [ ] `qcap2_qdev_info_get_uid`
- [ ] `qcap2_qdev_info_get_vendor_id`
- [ ] `qcap2_qdev_info_lock_from`
- [ ] `qcap2_qdev_new`
- [ ] `qcap2_qdev_pop`
- [ ] `qcap2_qdev_set_audio_input`
- [ ] `qcap2_qdev_set_event`
- [ ] `qcap2_qdev_set_info`
- [ ] `qcap2_qdev_set_poll_duration`
- [ ] `qcap2_qdev_set_video_input`
- [ ] `qcap2_qdev_start`
- [ ] `qcap2_qdev_stop`
- [ ] `qcap2_video_sink_delete`
- [ ] `qcap2_video_sink_new`
- [ ] `qcap2_video_sink_pop`
- [ ] `qcap2_video_sink_push`
- [ ] `qcap2_video_sink_set_backend_type`
- [ ] `qcap2_video_sink_set_device_index`
- [ ] `qcap2_video_sink_set_display_system`
- [ ] `qcap2_video_sink_set_dst_ss_type`
- [ ] `qcap2_video_sink_set_frame_count`
- [ ] `qcap2_video_sink_set_gpu_direct`
- [ ] `qcap2_video_sink_set_graphic_window_system`
- [ ] `qcap2_video_sink_set_low_bandwidth`
- [ ] `qcap2_video_sink_set_multithread`
- [ ] `qcap2_video_sink_set_native_handle`
- [ ] `qcap2_video_sink_set_scale_style`
- [ ] `qcap2_video_sink_set_src_ss_type`
- [ ] `qcap2_video_sink_set_video_format`
- [ ] `qcap2_video_sink_start`
- [ ] `qcap2_video_sink_stop`

### [qcap2.drm.h](file:///home/zzlee/qcap2-dev/include/qcap2.drm.h)
- [ ] `qcap2_get_drm_fd`
- [ ] `qcap2_put_drm_fd`
- [ ] `qcap2_video_sink_set_connector_id`
- [ ] `qcap2_video_sink_set_crtc_id`

### [qcap2.gst.h](file:///home/zzlee/qcap2-dev/include/qcap2.gst.h)
- [ ] `qcap2_video_sink_set_gst_sink_name`

### [qcap2.hsb.h](file:///home/zzlee/qcap2-dev/include/qcap2.hsb.h)
- [ ] `qcap2_video_source_set_device_ordinal`
- [ ] `qcap2_video_source_set_hololink_ip`
- [ ] `qcap2_video_source_set_ibv_name`
- [ ] `qcap2_video_source_set_ibv_port`

### [qcap2.lic.h](file:///home/zzlee/qcap2-dev/include/qcap2.lic.h)
- [ ] `qcap2_lic_register`

### [qcap2.nvbuf.h](file:///home/zzlee/qcap2-dev/include/qcap2.nvbuf.h)
- [ ] `qcap2_rcbuffer_alloc_mapped_nvbuf`
- [ ] `qcap2_rcbuffer_alloc_nvbuf`
- [ ] `qcap2_rcbuffer_free_mapped_nvbuf`
- [ ] `qcap2_rcbuffer_free_nvbuf`
- [ ] `qcap2_rcbuffer_get_nvbuf`
- [ ] `qcap2_rcbuffer_map_cures_nvbuf`
- [ ] `qcap2_rcbuffer_map_nvbuf`
- [ ] `qcap2_rcbuffer_set_nvbuf`
- [ ] `qcap2_rcbuffer_sync_nvbuf_for_cpu`
- [ ] `qcap2_rcbuffer_sync_nvbuf_for_cpu1`
- [ ] `qcap2_rcbuffer_sync_nvbuf_for_device`
- [ ] `qcap2_rcbuffer_unmap_cures_nvbuf`
- [ ] `qcap2_rcbuffer_unmap_nvbuf`
- [ ] `qcap2_rcbuffer_update_nvbuf_for_cpu`
- [ ] `qcap2_rcbuffer_update_nvbuf_for_device`
- [ ] `qcap2_video_sink_set_nvbuf`

### [qcap2.nvt.hdal.h](file:///home/zzlee/qcap2-dev/include/qcap2.nvt.hdal.h)
- [ ] `qcap2_audio_encoder_set_aenc_id`
- [ ] `qcap2_audio_sink_set_aout_id`
- [ ] `qcap2_audio_sink_set_aout_output`
- [ ] `qcap2_audio_sink_set_aout_volume`
- [ ] `qcap2_audio_source_set_acap_id`
- [ ] `qcap2_hd_init`
- [ ] `qcap2_hd_mem_init_config`
- [ ] `qcap2_rcbuffer_alloc_hd_video_frame`
- [ ] `qcap2_rcbuffer_free_hd_video_frame`
- [ ] `qcap2_rcbuffer_get_hd_audio_frame`
- [ ] `qcap2_rcbuffer_get_hd_mmap_addr`
- [ ] `qcap2_rcbuffer_get_hd_video_frame`
- [ ] `qcap2_rcbuffer_get_hd_videoenc_bs`
- [ ] `qcap2_rcbuffer_map_hd_video_frame`
- [ ] `qcap2_rcbuffer_set_hd_audio_frame`
- [ ] `qcap2_rcbuffer_set_hd_video_frame`
- [ ] `qcap2_rcbuffer_set_hd_videoenc_bs`
- [ ] `qcap2_rcbuffer_unmap_hd_video_frame`
- [ ] `qcap2_video_decoder_set_in_id`
- [ ] `qcap2_video_decoder_set_out_id`
- [ ] `qcap2_video_decoder_set_vdec_id`
- [ ] `qcap2_video_decoder_set_vproc_id`
- [ ] `qcap2_video_encoder_get_buf_info`
- [ ] `qcap2_video_encoder_get_mmap_addr`
- [ ] `qcap2_video_encoder_set_in_id`
- [ ] `qcap2_video_encoder_set_out_id`
- [ ] `qcap2_video_encoder_set_venc_id`
- [ ] `qcap2_video_scaler_set_ctrl_func`
- [ ] `qcap2_video_scaler_set_hd_src_dim`
- [ ] `qcap2_video_scaler_set_hd_src_isp_id`
- [ ] `qcap2_video_scaler_set_hd_src_pipe`
- [ ] `qcap2_video_scaler_set_hd_src_pxl_fmt`
- [ ] `qcap2_video_scaler_set_out_crop`
- [ ] `qcap2_video_scaler_set_out_depth`
- [ ] `qcap2_video_scaler_set_out_id`
- [ ] `qcap2_video_scaler_set_ref_path_3dnr`
- [ ] `qcap2_video_scaler_set_vproc_id`
- [ ] `qcap2_video_sink_set_vout_id`
- [ ] `qcap2_video_source_set_ctrl_func`
- [ ] `qcap2_video_source_set_drv_config`
- [ ] `qcap2_video_source_set_hd_src_dim`
- [ ] `qcap2_video_source_set_hd_src_pxl_fmt`
- [ ] `qcap2_video_source_set_vcap_id`
- [ ] `qcap2_video_source_set_vcap_id2`
- [ ] `qcap2_video_source_set_vendor_isp_config`
- [ ] `qcap2_video_source_set_vproc_id`

### [qcap2.processing.h](file:///home/zzlee/qcap2-dev/include/qcap2.processing.h)
- [ ] `qcap2_bitstream_filter_delete`
- [ ] `qcap2_bitstream_filter_get_audio_encoder_format`
- [ ] `qcap2_bitstream_filter_get_video_encoder_format`
- [ ] `qcap2_bitstream_filter_new`
- [ ] `qcap2_bitstream_filter_pop`
- [ ] `qcap2_bitstream_filter_push`
- [ ] `qcap2_bitstream_filter_set_audio_encoder_format`
- [ ] `qcap2_bitstream_filter_set_backend_type`
- [ ] `qcap2_bitstream_filter_set_event`
- [ ] `qcap2_bitstream_filter_set_packet_count`
- [ ] `qcap2_bitstream_filter_set_video_encoder_format`
- [ ] `qcap2_bitstream_filter_start`
- [ ] `qcap2_bitstream_filter_stop`
- [ ] `qcap2_video_blender_delete`
- [ ] `qcap2_video_blender_get_video_format`
- [ ] `qcap2_video_blender_new`
- [ ] `qcap2_video_blender_pop`
- [ ] `qcap2_video_blender_push`
- [ ] `qcap2_video_blender_set_backend_type`
- [ ] `qcap2_video_blender_set_buffers`
- [ ] `qcap2_video_blender_set_frame_count`
- [ ] `qcap2_video_blender_set_video_format`
- [ ] `qcap2_video_blender_start`
- [ ] `qcap2_video_blender_stop`
- [ ] `qcap2_video_matte_delete`
- [ ] `qcap2_video_matte_get_video_format`
- [ ] `qcap2_video_matte_new`
- [ ] `qcap2_video_matte_pop`
- [ ] `qcap2_video_matte_push`
- [ ] `qcap2_video_matte_set_alpha_buffers`
- [ ] `qcap2_video_matte_set_backend_type`
- [ ] `qcap2_video_matte_set_buffers`
- [ ] `qcap2_video_matte_set_frame_count`
- [ ] `qcap2_video_matte_set_params`
- [ ] `qcap2_video_matte_set_video_format`
- [ ] `qcap2_video_matte_start`
- [ ] `qcap2_video_matte_stop`

### [qcap2.pylon.h](file:///home/zzlee/qcap2-dev/include/qcap2.pylon.h)
- [ ] `qcap2_video_source_get_device_handle`
- [ ] `qcap2_video_source_set_auto_gain_lower_limit`
- [ ] `qcap2_video_source_set_auto_gain_upper_limit`
- [ ] `qcap2_video_source_set_exposure_time`
- [ ] `qcap2_video_source_set_gain`
- [ ] `qcap2_video_source_set_gain_auto`
- [ ] `qcap2_video_source_set_offsetx`
- [ ] `qcap2_video_source_set_offsety`
- [ ] `qcap2_video_source_set_trigger_mode`
- [ ] `qcap2_video_source_set_white_balance_auto`
- [ ] `qcap2_video_source_trigger`

### [qcap2.sipl.h](file:///home/zzlee/qcap2-dev/include/qcap2.sipl.h)
- [ ] `qcap2_rcbuffer_get_sipl_buffer`

### [qcap2.user.h](file:///home/zzlee/qcap2-dev/include/qcap2.user.h)
- [ ] `qcap2_video_source_fire_event`

### [qcap2.v4l2.h](file:///home/zzlee/qcap2-dev/include/qcap2.v4l2.h)
- [ ] `qcap2_audio_sink_get_v4l2_name`
- [ ] `qcap2_audio_sink_set_buf_type`
- [ ] `qcap2_audio_sink_set_memory`
- [ ] `qcap2_audio_sink_set_v4l2_name`
- [ ] `qcap2_audio_source_set_v4l2_name`
- [ ] `qcap2_video_encoder_get_v4l2_name`
- [ ] `qcap2_video_encoder_set_buf_type`
- [ ] `qcap2_video_encoder_set_memory`
- [ ] `qcap2_video_encoder_set_v4l2_name`
- [ ] `qcap2_video_sink_get_fd`
- [ ] `qcap2_video_sink_get_v4l2_name`
- [ ] `qcap2_video_sink_set_buf_type`
- [ ] `qcap2_video_sink_set_memory`
- [ ] `qcap2_video_sink_set_v4l2_name`

### [qcap2.v4l2.ioctl.h](file:///home/zzlee/qcap2-dev/include/qcap2.v4l2.ioctl.h)
- [ ] `qcap2_video_sink_get_key`
- [ ] `qcap2_video_sink_get_panel`
- [ ] `qcap2_video_sink_get_pip`
- [ ] `qcap2_video_sink_set_key`
- [ ] `qcap2_video_sink_set_panel`
- [ ] `qcap2_video_sink_set_pip`

---

## 2. Backend Implementation Status (by Module)

This section tracks the implementation status of all backend types defined in [qcap2.types.h](file:///home/zzlee/qcap2-dev/include/qcap2.types.h) across the different QCAP2 SDK modules.

### 2.1 Video Source Module (`qcap2_video_source_t`)
**Implementation File**: [src/qcap2.devices.cpp](file:///home/zzlee/qcap2-dev/src/qcap2.devices.cpp)
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

### 2.2 Audio Source Module (`qcap2_audio_source_t`)
**Implementation File**: [src/qcap2.devices.cpp](file:///home/zzlee/qcap2-dev/src/qcap2.devices.cpp)
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

### 2.3 Video Scaler Module (`qcap2_video_scaler_t`)
**Implementation File**: [src/qcap2.processing.cpp](file:///home/zzlee/qcap2-dev/src/qcap2.processing.cpp)
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

### 2.4 Window / Renderer Module (`qcap2_window_t`)
**Implementation File**: [src/qcap2.sync.cpp](file:///home/zzlee/qcap2-dev/src/qcap2.sync.cpp)
**Status Summary**: Mock rendering and X11 window backends are fully implemented.

- [x] **`QCAP2_WINDOW_BACKEND_TYPE_FAKE`** — **[FULLY IMPLEMENTED]**
  - Mock renderer used for testing.
- [x] **`QCAP2_WINDOW_BACKEND_TYPE_X11`** — **[FULLY IMPLEMENTED]**
  - Standard X11 window display rendering.
- [ ] **Other Window Backends** — **[UNIMPLEMENTED]**
  - [ ] `QCAP2_WINDOW_BACKEND_TYPE_NULL` (No-op renderer)

### 2.5 Video Sink Module (`qcap2_video_sink_t`)
**Implementation File**: [src/qcap2.devices.cpp](file:///home/zzlee/qcap2-dev/src/qcap2.devices.cpp)
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

### 2.6 Audio Sink Module (`qcap2_audio_sink_t`)
**Implementation File**: [src/qcap2.devices.cpp](file:///home/zzlee/qcap2-dev/src/qcap2.devices.cpp)
**Status Summary**: Standard ALSA playback backend is fully implemented. Specific proprietary/embedded hardware platform backends remain unimplemented.

- [x] **`QCAP2_AUDIO_SINK_BACKEND_TYPE_ALSA`** — **[FULLY IMPLEMENTED]**
  - Thread-safe, asynchronous ALSA playback backend writing PCM frames directly to Linux `/dev/snd/pcmC%dD%dp` sound character devices.
- [ ] **Proprietary & Platform-Specific Audio Sink Backends** — **[UNIMPLEMENTED]**
  - *TODO*: Implement platform PCM audio playback backends.
  - [ ] `QCAP2_AUDIO_SINK_BACKEND_TYPE_VITIS`
  - [ ] `QCAP2_AUDIO_SINK_BACKEND_TYPE_V4L2CAP`
  - [ ] `QCAP2_AUDIO_SINK_BACKEND_TYPE_NVT_HDAL`

### 2.7 Frame Pool Module (`qcap2_frame_pool_t`)
**Implementation File**: [src/qcap2.processing.cpp](file:///home/zzlee/qcap2-dev/src/qcap2.processing.cpp)
**Status Summary**: Frame pool is functional using the default memory allocator. Specialized allocators are unimplemented.
- [x] **`QCAP2_FRAME_POOL_BACKEND_TYPE_DEFAULT`** — **[FULLY IMPLEMENTED]**
- [ ] **Specialized Pool Backends** — **[UNIMPLEMENTED]**
  - *TODO*: Implement hardware-integrated pool allocators.
  - [ ] `QCAP2_FRAME_POOL_BACKEND_TYPE_RKMPP` (Rockchip Media Processing Platform pool)
  - [ ] `QCAP2_FRAME_POOL_BACKEND_TYPE_QDMABUF` (DMA-buf allocation-integrated pool)

### 2.8 Demuxer Module (`qcap2_demuxer_t`)
**Implementation File**: [src/qcap2.demuxer.cpp](file:///home/zzlee/qcap2-dev/src/qcap2.demuxer.cpp)
**Status Summary**: Default FFmpeg demuxer with advanced program parsing, DTS-based interruptible pacing, stream metadata embedding, and direct compressed packet delivery queues is fully implemented.

- [x] **`QCAP2_DEMUXER_TYPE_DEFAULT`** — **[FULLY IMPLEMENTED]**
  - High-performance default backend using FFmpeg APIs.
  - Features concurrent audio/video encoder matching and initialization.
  - Features stream extra-data extraction and propagation.
  - Features thread-safe reference-counted packet queue pushing and delivery.
  - Features highly responsive steady-clock pacing via interruptible condition variables.
- [x] **`QCAP2_DEMUXER_TYPE_RTSP`** — **[FULLY IMPLEMENTED]**
  - Advanced RTSP streaming client backend with keep-alive, auto-reconnection, customizable timeout/buffer/transport settings, low-latency pacing, and support for media types H264, H265, AAC, PCMA, and PCMU.
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

### 2.9 Muxer Module (`qcap2_muxer_t`)
**Implementation File**: [src/qcap2.muxer.cpp](file:///home/zzlee/qcap2-dev/src/qcap2.muxer.cpp)
**Status Summary**: Default FFmpeg-backed muxer with advanced stream mapping, extra-data propagation, and background DTS-interleaved writing thread is fully implemented.

- [x] **`QCAP2_MUXER_TYPE_DEFAULT`** — **[FULLY IMPLEMENTED]**
  - High-performance default backend using FFmpeg APIs.
  - Features dynamic video/audio decoder auto-creation and stream mappings.
  - Features stream codec parameters and extra-data (SPS/PPS, AudioSpecificConfig) propagation.
  - Features reactive background DTS-interleaved multiplexer thread with precise timebase conversion.
- [x] **`QCAP2_MUXER_TYPE_RTSP`** — **[FULLY IMPLEMENTED]**
  - RTSP media streaming publication backend supporting TLS/SSL encryption, authentication credentials addition, transport configuration, and real-time DTS-paced bypass multiplexing.
- [ ] **Proprietary & Streaming Muxer Backends** — **[UNIMPLEMENTED]**
  - *TODO*: Implement RTSP/JSRTSP or platform-specific muxer types.
  - [ ] `QCAP2_MUXER_TYPE_JSRTSP`

### 2.10 Audio Resampler Module (`qcap2_audio_resampler_t`)
**Implementation File**: [src/qcap2.processing.cpp](file:///home/zzlee/qcap2-dev/src/qcap2.processing.cpp)
**Status Summary**: Single unified high-performance resampler backed by FFmpeg libswresample is fully implemented.

- [x] **`qcap2_audio_resampler_t` Module & APIs** — **[FULLY IMPLEMENTED]**
  - Implements a single, unified backend using `SwrContext` from FFmpeg's `libswresample`.
  - Supports all audio format properties, channel layouts, sampling frequencies, and frame sizes conversion.
  - Fully supports thread-safe packet queueing, pushing, and polling.

---

## 3. Core Infrastructure & Tooling
**Status Summary**: Issues with build stability and test infrastructure.
- [ ] Fix Docker build rate limiting issues for `ubuntu:24.04` base image or document a workaround.
- [ ] Migrate testing framework from simple `assert()` statements to a structured framework like GTest or Catch2 to improve reporting.
- [ ] Introduce dependency management via vcpkg or Conan to simplify local builds and reduce global installation requirements.

---

## 4. Known Bugs & Code Adjustments
**Status Summary**: Identified issues in the current implementation.
- [ ] `qcap2_save_raw_video_frame` (in [src/qcap2.utils.cpp](file:///home/zzlee/qcap2-dev/src/qcap2.utils.cpp)): Currently implements a simplified `stride * height` file write. Needs correct buffer size calculation for planar color spaces (like YUV420P where size is `stride * height * 1.5`).
- [ ] `qcap2_load_picture` & `qcap2_get_picture_info` (in [src/qcap2.utils.cpp](file:///home/zzlee/qcap2-dev/src/qcap2.utils.cpp)): Replace mock implementations with actual image decoding/metadata parsing.
- [ ] Investigate the DirectShow Video Mixing Renderer bug patch impact (referenced in [include/qcap.system.h](file:///home/zzlee/qcap2-dev/include/qcap.system.h)) and document its initialization delay behavior in the wiki.

---

## 5. Architectural Improvements
**Status Summary**: Long-term structural updates to improve codebase health.
- [ ] Improve error reporting by moving beyond simple `QRESULT` codes. Consider an extended error API that can return context-rich string messages for debugging failures.
