#include "qcap2.graphics.h"
#include "qcap2.buffer.h"
#include "qcap.types.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>
#include <hb-ft.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <new>
#include <string>

#ifdef __cplusplus
extern "C" {
#endif

// Private definition for qcap2_font_atlas_t
struct qcap2_font_atlas_priv_t {
    std::string strFontFile;
    std::string strFamilyName;
    int nStyle;
    int nCharSize;
    int nDPI;
    int nWidth;
    int nHeight;

    FT_Library ftLibrary;
    FT_Face ftFace;
    hb_font_t* hbFont;

    bool bStarted;

    qcap2_font_atlas_priv_t()
        : nStyle(QCAP_FONT_STYLE_REGULAR), nCharSize(12), nDPI(96), nWidth(512), nHeight(512),
          ftLibrary(nullptr), ftFace(nullptr), hbFont(nullptr), bStarted(false) {}
};

// Private definition for qcap2_graphics_t
struct qcap2_graphics_priv_t {
    int nBackendType;
    qcap2_font_atlas_priv_t* pFontAtlas;
    int32_t nColor;

    qcap2_rcbuffer_t* pRCBuffer;
    uint8_t* pMappedBuffer[4];
    int pMappedStride[4];
    ULONG nColorSpaceType;
    ULONG nBufferWidth;
    ULONG nBufferHeight;

    bool bStarted;
    bool bBegun;

    qcap2_graphics_priv_t()
        : nBackendType(QCAP2_GRAPHICS_BACKEND_TYPE_DEFAULT), pFontAtlas(nullptr), nColor(0xFFFFFFFF),
          pRCBuffer(nullptr), nColorSpaceType(0), nBufferWidth(0), nBufferHeight(0), bStarted(false), bBegun(false) {
        memset(pMappedBuffer, 0, sizeof(pMappedBuffer));
        memset(pMappedStride, 0, sizeof(pMappedStride));
    }
};

// --- qcap2_font_atlas_t implementation ---
qcap2_font_atlas_t* qcap2_font_atlas_new() {
    return (qcap2_font_atlas_t*) new qcap2_font_atlas_priv_t();
}

void qcap2_font_atlas_delete(qcap2_font_atlas_t* pThis) {
    qcap2_font_atlas_priv_t* pPriv = (qcap2_font_atlas_priv_t*)pThis;
    if (pPriv) {
        qcap2_font_atlas_stop(pThis);
        delete pPriv;
    }
}

void qcap2_font_atlas_set_font_file(qcap2_font_atlas_t* pThis, const char* strFontFile) {
    qcap2_font_atlas_priv_t* pPriv = (qcap2_font_atlas_priv_t*)pThis;
    if (pPriv && strFontFile) {
        pPriv->strFontFile = strFontFile;
    }
}

void qcap2_font_atlas_set_family_name(qcap2_font_atlas_t* pThis, const char* strFamilyName) {
    qcap2_font_atlas_priv_t* pPriv = (qcap2_font_atlas_priv_t*)pThis;
    if (pPriv && strFamilyName) {
        pPriv->strFamilyName = strFamilyName;
    }
}

void qcap2_font_atlas_set_style(qcap2_font_atlas_t* pThis, int nStyle) {
    qcap2_font_atlas_priv_t* pPriv = (qcap2_font_atlas_priv_t*)pThis;
    if (pPriv) {
        pPriv->nStyle = nStyle;
    }
}

void qcap2_font_atlas_set_char_size(qcap2_font_atlas_t* pThis, int nCharSize) {
    qcap2_font_atlas_priv_t* pPriv = (qcap2_font_atlas_priv_t*)pThis;
    if (pPriv) {
        pPriv->nCharSize = nCharSize;
    }
}

void qcap2_font_atlas_set_dpi(qcap2_font_atlas_t* pThis, int nDPI) {
    qcap2_font_atlas_priv_t* pPriv = (qcap2_font_atlas_priv_t*)pThis;
    if (pPriv) {
        pPriv->nDPI = nDPI;
    }
}

void qcap2_font_atlas_set_atlas_size(qcap2_font_atlas_t* pThis, int nWidth, int nHeight) {
    qcap2_font_atlas_priv_t* pPriv = (qcap2_font_atlas_priv_t*)pThis;
    if (pPriv) {
        pPriv->nWidth = nWidth;
        pPriv->nHeight = nHeight;
    }
}

QRESULT qcap2_font_atlas_start(qcap2_font_atlas_t* pThis) {
    qcap2_font_atlas_priv_t* pPriv = (qcap2_font_atlas_priv_t*)pThis;
    if (!pPriv || pPriv->bStarted) return QCAP_RS_ERROR_GENERAL;

    if (FT_Init_FreeType(&pPriv->ftLibrary)) {
        return QCAP_RS_ERROR_GENERAL;
    }

    if (!pPriv->strFontFile.empty()) {
        if (FT_New_Face(pPriv->ftLibrary, pPriv->strFontFile.c_str(), 0, &pPriv->ftFace)) {
            FT_Done_FreeType(pPriv->ftLibrary);
            pPriv->ftLibrary = nullptr;
            return QCAP_RS_ERROR_GENERAL;
        }
    } else {
        // Fallback or handle memory font
        FT_Done_FreeType(pPriv->ftLibrary);
        pPriv->ftLibrary = nullptr;
        return QCAP_RS_ERROR_GENERAL; // No font specified
    }

    if (FT_Set_Char_Size(pPriv->ftFace, 0, pPriv->nCharSize * 64, pPriv->nDPI, pPriv->nDPI)) {
        FT_Done_Face(pPriv->ftFace);
        pPriv->ftFace = nullptr;
        FT_Done_FreeType(pPriv->ftLibrary);
        pPriv->ftLibrary = nullptr;
        return QCAP_RS_ERROR_GENERAL;
    }

    pPriv->hbFont = hb_ft_font_create(pPriv->ftFace, NULL);

    pPriv->bStarted = true;
    return QCAP_RS_SUCCESSFUL;
}

QRESULT qcap2_font_atlas_stop(qcap2_font_atlas_t* pThis) {
    qcap2_font_atlas_priv_t* pPriv = (qcap2_font_atlas_priv_t*)pThis;
    if (!pPriv || !pPriv->bStarted) return QCAP_RS_ERROR_GENERAL;

    if (pPriv->hbFont) {
        hb_font_destroy(pPriv->hbFont);
        pPriv->hbFont = nullptr;
    }
    if (pPriv->ftFace) {
        FT_Done_Face(pPriv->ftFace);
        pPriv->ftFace = nullptr;
    }
    if (pPriv->ftLibrary) {
        FT_Done_FreeType(pPriv->ftLibrary);
        pPriv->ftLibrary = nullptr;
    }

    pPriv->bStarted = false;
    return QCAP_RS_SUCCESSFUL;
}

float qcap2_font_atlas_get_ascender(qcap2_font_atlas_t* pThis) {
    qcap2_font_atlas_priv_t* pPriv = (qcap2_font_atlas_priv_t*)pThis;
    if (!pPriv || !pPriv->ftFace) return 0.0f;
    return (float)pPriv->ftFace->size->metrics.ascender / 64.0f;
}

float qcap2_font_atlas_get_descender(qcap2_font_atlas_t* pThis) {
    qcap2_font_atlas_priv_t* pPriv = (qcap2_font_atlas_priv_t*)pThis;
    if (!pPriv || !pPriv->ftFace) return 0.0f;
    return (float)pPriv->ftFace->size->metrics.descender / 64.0f;
}

float qcap2_font_atlas_get_height(qcap2_font_atlas_t* pThis) {
    qcap2_font_atlas_priv_t* pPriv = (qcap2_font_atlas_priv_t*)pThis;
    if (!pPriv || !pPriv->ftFace) return 0.0f;
    return (float)pPriv->ftFace->size->metrics.height / 64.0f;
}

// --- qcap2_graphics_t implementation ---

qcap2_graphics_t* qcap2_graphics_new() {
    return (qcap2_graphics_t*) new qcap2_graphics_priv_t();
}

void qcap2_graphics_delete(qcap2_graphics_t* pThis) {
    qcap2_graphics_priv_t* pPriv = (qcap2_graphics_priv_t*)pThis;
    if (pPriv) {
        qcap2_graphics_stop(pThis);
        delete pPriv;
    }
}

void qcap2_graphics_set_backend_type(qcap2_graphics_t* pThis, int nBackendType) {
    qcap2_graphics_priv_t* pPriv = (qcap2_graphics_priv_t*)pThis;
    if (pPriv) {
        pPriv->nBackendType = nBackendType;
    }
}

QRESULT qcap2_graphics_start(qcap2_graphics_t* pThis) {
    qcap2_graphics_priv_t* pPriv = (qcap2_graphics_priv_t*)pThis;
    if (!pPriv || pPriv->bStarted) return QCAP_RS_ERROR_GENERAL;
    pPriv->bStarted = true;
    return QCAP_RS_SUCCESSFUL;
}

QRESULT qcap2_graphics_stop(qcap2_graphics_t* pThis) {
    qcap2_graphics_priv_t* pPriv = (qcap2_graphics_priv_t*)pThis;
    if (!pPriv || !pPriv->bStarted) return QCAP_RS_ERROR_GENERAL;
    pPriv->bStarted = false;
    return QCAP_RS_SUCCESSFUL;
}

QRESULT qcap2_graphics_begin(qcap2_graphics_t* pThis, qcap2_rcbuffer_t* pRCBuffer) {
    qcap2_graphics_priv_t* pPriv = (qcap2_graphics_priv_t*)pThis;
    if (!pPriv || !pRCBuffer || pPriv->bBegun) return QCAP_RS_ERROR_GENERAL;

    void* pData = qcap2_rcbuffer_lock_data(pRCBuffer);
    if (!pData) return QCAP_RS_ERROR_GENERAL;

    qcap2_av_frame_t* pFrame = (qcap2_av_frame_t*)pData;
    qcap2_av_frame_get_video_property(pFrame, &pPriv->nColorSpaceType, &pPriv->nBufferWidth, &pPriv->nBufferHeight);

    // Validate supported formats (e.g., ARGB32, RGB24)
    if (pPriv->nColorSpaceType != QCAP_COLORSPACE_TYPE_ARGB32 &&
        pPriv->nColorSpaceType != QCAP_COLORSPACE_TYPE_RGB24 &&
        pPriv->nColorSpaceType != QCAP_COLORSPACE_TYPE_ABGR32 &&
        pPriv->nColorSpaceType != QCAP_COLORSPACE_TYPE_BGR24) {
        qcap2_rcbuffer_unlock_data(pRCBuffer);
        return QCAP_RS_ERROR_GENERAL;
    }

    qcap2_av_frame_get_buffer1(pFrame, pPriv->pMappedBuffer, pPriv->pMappedStride);

    pPriv->pRCBuffer = pRCBuffer;
    pPriv->bBegun = true;

    return QCAP_RS_SUCCESSFUL;
}

QRESULT qcap2_graphics_end(qcap2_graphics_t* pThis) {
    qcap2_graphics_priv_t* pPriv = (qcap2_graphics_priv_t*)pThis;
    if (!pPriv || !pPriv->bBegun) return QCAP_RS_ERROR_GENERAL;

    qcap2_rcbuffer_unlock_data(pPriv->pRCBuffer);
    pPriv->pRCBuffer = nullptr;
    pPriv->bBegun = false;

    return QCAP_RS_SUCCESSFUL;
}

void qcap2_graphics_set_font_atlas(qcap2_graphics_t* pThis, qcap2_font_atlas_t* pFontAtlas) {
    qcap2_graphics_priv_t* pPriv = (qcap2_graphics_priv_t*)pThis;
    if (pPriv) {
        pPriv->pFontAtlas = (qcap2_font_atlas_priv_t*)pFontAtlas;
    }
}

void qcap2_graphics_set_color(qcap2_graphics_t* pThis, int32_t nColor) {
    qcap2_graphics_priv_t* pPriv = (qcap2_graphics_priv_t*)pThis;
    if (pPriv) {
        pPriv->nColor = nColor;
    }
}

// Blend pixel helper
static void blend_pixel(uint8_t* pPixel, int32_t color, uint8_t alpha, ULONG colorspace) {
    uint8_t a = (color >> 24) & 0xFF;
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;

    // Modulate by glyph alpha
    a = (a * alpha) / 255;

    if (a == 0) return; // transparent

    if (colorspace == QCAP_COLORSPACE_TYPE_ARGB32) {
        // A, R, G, B in memory? Or B, G, R, A?
        // Assuming BGRA in memory for ARGB32 (common convention) or strictly A R G B byte order.
        // Let's use simple BGRA in memory for standard windows/linux little-endian
        pPixel[0] = (b * a + pPixel[0] * (255 - a)) / 255;
        pPixel[1] = (g * a + pPixel[1] * (255 - a)) / 255;
        pPixel[2] = (r * a + pPixel[2] * (255 - a)) / 255;
        // pPixel[3] = (a * 255 + pPixel[3] * (255 - a)) / 255;
    } else if (colorspace == QCAP_COLORSPACE_TYPE_ABGR32) {
        pPixel[0] = (r * a + pPixel[0] * (255 - a)) / 255;
        pPixel[1] = (g * a + pPixel[1] * (255 - a)) / 255;
        pPixel[2] = (b * a + pPixel[2] * (255 - a)) / 255;
    } else if (colorspace == QCAP_COLORSPACE_TYPE_RGB24) {
        // R, G, B
        pPixel[0] = (r * a + pPixel[0] * (255 - a)) / 255;
        pPixel[1] = (g * a + pPixel[1] * (255 - a)) / 255;
        pPixel[2] = (b * a + pPixel[2] * (255 - a)) / 255;
    } else if (colorspace == QCAP_COLORSPACE_TYPE_BGR24) {
        pPixel[0] = (b * a + pPixel[0] * (255 - a)) / 255;
        pPixel[1] = (g * a + pPixel[1] * (255 - a)) / 255;
        pPixel[2] = (r * a + pPixel[2] * (255 - a)) / 255;
    }
}

QRESULT qcap2_graphics_fill_rect(qcap2_graphics_t* pThis, int x, int y, int w, int h) {
    qcap2_graphics_priv_t* pPriv = (qcap2_graphics_priv_t*)pThis;
    if (!pPriv || !pPriv->bBegun) return QCAP_RS_ERROR_GENERAL;

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)pPriv->nBufferWidth) w = pPriv->nBufferWidth - x;
    if (y + h > (int)pPriv->nBufferHeight) h = pPriv->nBufferHeight - y;

    if (w <= 0 || h <= 0) return QCAP_RS_SUCCESSFUL;

    int bpp = (pPriv->nColorSpaceType == QCAP_COLORSPACE_TYPE_ARGB32 || pPriv->nColorSpaceType == QCAP_COLORSPACE_TYPE_ABGR32) ? 4 : 3;

    for (int j = 0; j < h; ++j) {
        uint8_t* pRow = pPriv->pMappedBuffer[0] + (y + j) * pPriv->pMappedStride[0] + x * bpp;
        for (int i = 0; i < w; ++i) {
            blend_pixel(pRow + i * bpp, pPriv->nColor, 255, pPriv->nColorSpaceType);
        }
    }

    return QCAP_RS_SUCCESSFUL;
}

QRESULT qcap2_graphics_draw_text(qcap2_graphics_t* pThis, const char* strText, int x, int y, int w, int h) {
    (void)w; (void)h; // Simple clip/wrap not implemented in this basic mockup
    qcap2_graphics_priv_t* pPriv = (qcap2_graphics_priv_t*)pThis;
    if (!pPriv || !pPriv->bBegun || !pPriv->pFontAtlas || !pPriv->pFontAtlas->bStarted) return QCAP_RS_ERROR_GENERAL;

    hb_buffer_t* hbBuf = hb_buffer_create();
    hb_buffer_add_utf8(hbBuf, strText, -1, 0, -1);
    hb_buffer_guess_segment_properties(hbBuf);

    hb_shape(pPriv->pFontAtlas->hbFont, hbBuf, NULL, 0);

    unsigned int glyph_count;
    hb_glyph_info_t* glyph_info = hb_buffer_get_glyph_infos(hbBuf, &glyph_count);
    hb_glyph_position_t* glyph_pos = hb_buffer_get_glyph_positions(hbBuf, &glyph_count);

    int current_x = x * 64; // in 26.6
    int current_y = y * 64;

    int bpp = (pPriv->nColorSpaceType == QCAP_COLORSPACE_TYPE_ARGB32 || pPriv->nColorSpaceType == QCAP_COLORSPACE_TYPE_ABGR32) ? 4 : 3;

    for (unsigned int i = 0; i < glyph_count; ++i) {
        hb_codepoint_t glyphid = glyph_info[i].codepoint;
        int x_offset = glyph_pos[i].x_offset;
        int y_offset = glyph_pos[i].y_offset;
        int x_advance = glyph_pos[i].x_advance;
        int y_advance = glyph_pos[i].y_advance;

        if (FT_Load_Glyph(pPriv->pFontAtlas->ftFace, glyphid, FT_LOAD_RENDER)) {
            continue;
        }

        FT_Bitmap* bitmap = &pPriv->pFontAtlas->ftFace->glyph->bitmap;
        int draw_x = (current_x + x_offset) / 64 + pPriv->pFontAtlas->ftFace->glyph->bitmap_left;
        int draw_y = (current_y + y_offset) / 64 - pPriv->pFontAtlas->ftFace->glyph->bitmap_top;
        draw_y += qcap2_font_atlas_get_ascender((qcap2_font_atlas_t*)pPriv->pFontAtlas); // baseline adjustment

        for (unsigned int r = 0; r < bitmap->rows; ++r) {
            for (unsigned int c = 0; c < bitmap->width; ++c) {
                int px = draw_x + c;
                int py = draw_y + r;

                if (px >= 0 && px < (int)pPriv->nBufferWidth && py >= 0 && py < (int)pPriv->nBufferHeight) {
                    uint8_t alpha = bitmap->buffer[r * bitmap->pitch + c];
                    if (alpha > 0) {
                        uint8_t* pPixel = pPriv->pMappedBuffer[0] + py * pPriv->pMappedStride[0] + px * bpp;
                        blend_pixel(pPixel, pPriv->nColor, alpha, pPriv->nColorSpaceType);
                    }
                }
            }
        }

        current_x += x_advance;
        current_y += y_advance;
    }

    hb_buffer_destroy(hbBuf);
    return QCAP_RS_SUCCESSFUL;
}

QRESULT qcap2_graphics_get_text_size(qcap2_graphics_t* pThis, const char* strText, int* x, int* y, int* w, int* h) {
    qcap2_graphics_priv_t* pPriv = (qcap2_graphics_priv_t*)pThis;
    if (!pPriv || !pPriv->pFontAtlas || !pPriv->pFontAtlas->bStarted || !strText || !w || !h) return QCAP_RS_ERROR_GENERAL;

    hb_buffer_t* hbBuf = hb_buffer_create();
    hb_buffer_add_utf8(hbBuf, strText, -1, 0, -1);
    hb_buffer_guess_segment_properties(hbBuf);

    hb_shape(pPriv->pFontAtlas->hbFont, hbBuf, NULL, 0);

    unsigned int glyph_count;
    hb_glyph_position_t* glyph_pos = hb_buffer_get_glyph_positions(hbBuf, &glyph_count);

    int total_width = 0;
    for (unsigned int i = 0; i < glyph_count; ++i) {
        total_width += glyph_pos[i].x_advance;
    }

    *w = total_width / 64;
    *h = (int)qcap2_font_atlas_get_height((qcap2_font_atlas_t*)pPriv->pFontAtlas);

    if (x) *x = 0;
    if (y) *y = 0;

    hb_buffer_destroy(hbBuf);
    return QCAP_RS_SUCCESSFUL;
}

#ifdef __cplusplus
}
#endif
