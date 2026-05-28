#include "qcap2.graphics.h"
#include "qcap2.buffer.h"
#include "qcap.types.h"
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

void on_free_rcbuf(PVOID pData) {
    qcap2_av_frame_t* pFrame = (qcap2_av_frame_t*)pData;
    qcap2_av_frame_free_buffer(pFrame);
    free(pFrame);
}

int main() {
    printf("Starting test_qcap2_graphics...\n");

    // 1. Setup RC buffer wrapping an AV frame
    qcap2_av_frame_t* pFrame = (qcap2_av_frame_t*)malloc(sizeof(qcap2_av_frame_t));
    qcap2_av_frame_init(pFrame);
    qcap2_av_frame_set_video_property(pFrame, QCAP_COLORSPACE_TYPE_ARGB32, 640, 480);
    bool bAllocated = qcap2_av_frame_alloc_buffer(pFrame, 1, 1);
    assert(bAllocated);

    // clear frame to black
    uint8_t* pBufs[4];
    int pStrides[4];
    qcap2_av_frame_get_buffer1(pFrame, pBufs, pStrides);
    memset(pBufs[0], 0, pStrides[0] * 480);

    qcap2_rcbuffer_t* pRCBuffer = qcap2_rcbuffer_new(pFrame, on_free_rcbuf);

    // 2. Setup font atlas
    qcap2_font_atlas_t* pFontAtlas = qcap2_font_atlas_new();
    // Assuming a generic font path on ubuntu for testing
    qcap2_font_atlas_set_font_file(pFontAtlas, "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
    qcap2_font_atlas_set_char_size(pFontAtlas, 24);
    QRESULT hr = qcap2_font_atlas_start(pFontAtlas);
    if (hr != QCAP_RS_SUCCESSFUL) {
        printf("WARNING: Could not start font atlas, font might be missing. Skipping graphics draw test.\n");
    } else {
        // 3. Setup graphics
        qcap2_graphics_t* pGraphics = qcap2_graphics_new();
        qcap2_graphics_set_backend_type(pGraphics, QCAP2_GRAPHICS_BACKEND_TYPE_DEFAULT);
        qcap2_graphics_set_font_atlas(pGraphics, pFontAtlas);
        assert(qcap2_graphics_start(pGraphics) == QCAP_RS_SUCCESSFUL);

        assert(qcap2_graphics_begin(pGraphics, pRCBuffer) == QCAP_RS_SUCCESSFUL);

        qcap2_graphics_set_color(pGraphics, 0xFFFF0000); // Red
        assert(qcap2_graphics_fill_rect(pGraphics, 50, 50, 100, 100) == QCAP_RS_SUCCESSFUL);

        qcap2_graphics_set_color(pGraphics, 0xFF00FF00); // Green
        assert(qcap2_graphics_draw_text(pGraphics, "Hello QCAP2", 50, 200, 0, 0) == QCAP_RS_SUCCESSFUL);

        int w=0, h=0;
        assert(qcap2_graphics_get_text_size(pGraphics, "Hello QCAP2", NULL, NULL, &w, &h) == QCAP_RS_SUCCESSFUL);
        assert(w > 0 && h > 0);

        assert(qcap2_graphics_end(pGraphics) == QCAP_RS_SUCCESSFUL);

        qcap2_graphics_stop(pGraphics);
        qcap2_graphics_delete(pGraphics);
    }

    qcap2_font_atlas_stop(pFontAtlas);
    qcap2_font_atlas_delete(pFontAtlas);
    qcap2_rcbuffer_release(pRCBuffer);

    printf("test_qcap2_graphics passed.\n");
    return 0;
}
