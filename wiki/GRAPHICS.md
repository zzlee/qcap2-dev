# Graphics and Font Atlas APIs

This document describes the usage of `qcap2_graphics_t` and `qcap2_font_atlas_t` APIs.

## `qcap2_font_atlas_t`

The font atlas provides the typographic layout and rasterization capabilities using FreeType and HarfBuzz.

-   **Initialization:** Create with `qcap2_font_atlas_new()`. Set properties such as `font_file`, `char_size` (in points), and `dpi` before calling `qcap2_font_atlas_start()`.
-   **Usage:** Once started, the atlas will load the font face and create a HarfBuzz font object.
-   **Metrics:** Ascent, descent, and height metrics are retrievable using `qcap2_font_atlas_get_ascender`, `qcap2_font_atlas_get_descender`, and `qcap2_font_atlas_get_height`.

## `qcap2_graphics_t`

The graphics API allows rendering shapes and texts directly into a `qcap2_rcbuffer_t` backed by a `qcap2_av_frame_t`.

-   **Backend:** Defaults to `QCAP2_GRAPHICS_BACKEND_TYPE_DEFAULT` which performs CPU-based software rendering.
-   **Session:** Rendering happens between `qcap2_graphics_begin()` and `qcap2_graphics_end()`. The provided `qcap2_rcbuffer_t` is mapped for memory access during this session.
-   **Drawing:**
    -   `qcap2_graphics_fill_rect()`: Draws a solid filled rectangle.
    -   `qcap2_graphics_draw_text()`: Shapes text via HarfBuzz and rasterizes via FreeType onto the bound buffer.
    -   `qcap2_graphics_set_color()`: Sets the primary color (format `0xAARRGGBB`) used in subsequent drawing primitives. The alpha channel determines opacity.

**Note:** Buffer backing formats currently support 32-bit (ARGB/ABGR) and 24-bit (RGB/BGR) layouts.
