# qcap2_av_frame_alloc_buffer usage notes

`qcap2_av_frame_alloc_buffer()` allocates raw video image storage for `qcap2_av_frame_t` according to the frame's video properties.

Before calling it, set video properties with:

```cpp
qcap2_av_frame_set_video_property(frame, nColorSpaceType, width, height);
```

`nColorSpaceType` is expected to be one of the legacy `QCAP_COLORSPACE_TYPE_XXX` enumeration values from `qcap.types.h`, not the newer `qcap2_color_space_t` colorimetry/range enum.

## Parameters

```cpp
bool qcap2_av_frame_alloc_buffer(qcap2_av_frame_t* pFrame, int align, int valign);
```

- `align`: byte alignment for each plane stride. Values `<= 1` mean no extra stride alignment.
- `valign`: line-count alignment for plane heights. Values `<= 1` mean no extra height alignment.

The allocator computes plane strides and plane offsets from the color-space type, width, height, `align`, and `valign`.

## Supported layouts

### Packed single-plane formats

These allocate one plane at `pBuffer[0]`:

| Format | Bytes per pixel | Stride |
|---|---:|---|
| `QCAP_COLORSPACE_TYPE_RGB24` | 3 | `align(width * 3, align)` |
| `QCAP_COLORSPACE_TYPE_BGR24` | 3 | `align(width * 3, align)` |
| `QCAP_COLORSPACE_TYPE_ARGB32` | 4 | `align(width * 4, align)` |
| `QCAP_COLORSPACE_TYPE_ABGR32` | 4 | `align(width * 4, align)` |
| `QCAP_COLORSPACE_TYPE_Y416` | 8 | `align(width * 8, align)` |
| `QCAP_COLORSPACE_TYPE_YUY2` | 2 | `align(width * 2, align)` |
| `QCAP_COLORSPACE_TYPE_UYVY` | 2 | `align(width * 2, align)` |
| `QCAP_COLORSPACE_TYPE_Y800` | 1 | `align(width, align)` |

Plane size is:

```cpp
stride[0] * align(height, valign)
```

### Planar 4:2:0 formats

`QCAP_COLORSPACE_TYPE_YV12` and `QCAP_COLORSPACE_TYPE_I420` allocate three planes:

```cpp
pBuffer[0] = Y plane
pBuffer[1] = first chroma plane
pBuffer[2] = second chroma plane
```

Current implementation uses the same allocation shape for both YV12 and I420. It does not encode semantic U/V naming in the API; users interpret planes according to the color-space type.

Strides:

```cpp
stride[0] = align(width, align);
stride[1] = align((width + 1) / 2, align);
stride[2] = stride[1];
```

Heights:

```cpp
Y height      = align(height, valign);
chroma height = align((height + 1) / 2, valign);
```

### Planar 4:4:4 format

`QCAP_COLORSPACE_TYPE_YV24` allocates three full-resolution planes:

```cpp
stride[0] = align(width, align);
stride[1] = stride[0];
stride[2] = stride[0];
```

Each plane height is `align(height, valign)`.

### Semi-planar 4:2:0 formats

`QCAP_COLORSPACE_TYPE_NV12` allocates two planes:

```cpp
pBuffer[0] = Y plane
pBuffer[1] = interleaved UV plane
```

Strides:

```cpp
stride[0] = align(width, align);
stride[1] = stride[0];
```

Heights:

```cpp
Y height  = align(height, valign);
UV height = align((height + 1) / 2, valign);
```

`QCAP_COLORSPACE_TYPE_P010` is similar, but uses 16-bit samples:

```cpp
stride[0] = align(width * 2, align);
stride[1] = stride[0];
```

### Semi-planar 4:2:2 16-bit format

`QCAP_COLORSPACE_TYPE_P210` allocates two planes:

```cpp
stride[0] = align(width * 2, align);
stride[1] = stride[0];
```

Both planes use full aligned frame height.

## Unsupported formats

Compressed/bitstream formats do not have a raw video plane layout and return `false` from `qcap2_av_frame_alloc_buffer()`.

Examples:

- `QCAP_COLORSPACE_TYPE_MJPG`
- `QCAP_COLORSPACE_TYPE_H264`
- `QCAP_COLORSPACE_TYPE_H265`
- `QCAP_COLORSPACE_TYPE_MPG2`
- `QCAP_COLORSPACE_TYPE_AV01`

Use packet/bitstream APIs for encoded data instead of `qcap2_av_frame_alloc_buffer()`.

## Ownership and cleanup

Allocated planes are backed by a single contiguous allocation owned by the frame. Plane pointers are offsets into that allocation.

Call:

```cpp
qcap2_av_frame_free_buffer(frame);
```

to release the allocation. Freeing clears all plane pointers and strides.

If `qcap2_av_frame_alloc_buffer()` is called again on a frame that already owns a buffer, the existing owned allocation is freed before allocating a new one.

## Example

```cpp
qcap2_av_frame_t frame;
qcap2_av_frame_init(&frame);

qcap2_av_frame_set_video_property(&frame, QCAP_COLORSPACE_TYPE_NV12, 1920, 1080);

if (qcap2_av_frame_alloc_buffer(&frame, 16, 1)) {
	uint8_t* pBuffer[4];
	int pStride[4];
	qcap2_av_frame_get_buffer1(&frame, pBuffer, pStride);

	// pBuffer[0] = Y plane, pStride[0] = aligned Y pitch
	// pBuffer[1] = UV plane, pStride[1] = aligned UV pitch

	qcap2_av_frame_free_buffer(&frame);
}
```

## Common pitfalls

- Do not pass `qcap2_color_space_t` values such as `QCAP2_COLOR_SPACE_BT709`; these describe colorimetry/range, not memory layout.
- Do not use this function for encoded formats such as H.264/H.265/MJPG.
- Do not free individual plane pointers. They point into one contiguous allocation owned by the frame.
- After `qcap2_av_frame_free_buffer()`, all plane pointers and strides are cleared.
