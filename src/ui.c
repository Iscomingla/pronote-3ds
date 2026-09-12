/*
 * ui.c -- citro2d UI layer for notApro
 *
 * Clear/scene ordering:
 *   C2D_TargetClear must happen before C2D_SceneBegin.
 *   Pattern:
 *     ui_clear_target(ui_get_target(GFX_TOP),    COL_BG);
 *     ui_clear_target(ui_get_target(GFX_BOTTOM), COL_BG);
 *     ui_target(GFX_TOP);    // draw ...
 *     ui_target(GFX_BOTTOM); // draw ...
 *
 * Camera preview texture:
 *   The GPU requires Morton (Z-order / tile) encoded textures.
 *   ui_cam_tex_upload() encodes the row-major RGB565 camera buffer into
 *   the GPU texture in one pass, then flushes the texture cache.
 *   ui_cam_tex_draw() draws the texture using a C2D_Image.
 */

#include "ui.h"
#include <string.h>

static C3D_RenderTarget *s_top  = NULL;
static C3D_RenderTarget *s_bot  = NULL;
static C3D_RenderTarget *s_cur  = NULL;

static C2D_TextBuf s_tbuf;
static C2D_Font    s_font = NULL;

// Camera preview texture (RGB565, pow2 dimensions)
static C3D_Tex s_cam_tex;
static bool    s_cam_tex_ready = false;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
void ui_init(void) {
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    s_top = C2D_CreateScreenTarget(GFX_TOP,    GFX_LEFT);
    s_bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    s_cur = s_top;

    s_tbuf = C2D_TextBufNew(1024);
    s_font = C2D_FontLoadSystem(CFG_REGION_EUR);
}

void ui_exit(void) {
    ui_cam_tex_free();
    if (s_font) { C2D_FontFree(s_font); s_font = NULL; }
    C2D_TextBufDelete(s_tbuf);
    C2D_Fini();
    C3D_Fini();
}

void ui_suspend(void) {
    ui_cam_tex_free();
    if (s_font) { C2D_FontFree(s_font); s_font = NULL; }
    C2D_TextBufDelete(s_tbuf);
    s_tbuf = NULL;
    s_top = s_bot = s_cur = NULL;
    C2D_Fini();
    C3D_Fini();
}

void ui_resume(void) {
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    s_top = C2D_CreateScreenTarget(GFX_TOP,    GFX_LEFT);
    s_bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    s_cur = s_top;

    s_tbuf = C2D_TextBufNew(1024);
    s_font = C2D_FontLoadSystem(CFG_REGION_EUR);
}

// ---------------------------------------------------------------------------
// Frame helpers
// ---------------------------------------------------------------------------
void ui_frame_begin(void) {
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TextBufClear(s_tbuf);
}

void ui_frame_end(void) {
    C3D_FrameEnd(0);
}

// ---------------------------------------------------------------------------
// Target helpers
// ---------------------------------------------------------------------------
C3D_RenderTarget *ui_get_target(gfxScreen_t screen) {
    return (screen == GFX_TOP) ? s_top : s_bot;
}

void ui_clear_target(C3D_RenderTarget *t, u32 colour) {
    C2D_TargetClear(t, colour);
}

void ui_target(gfxScreen_t screen) {
    s_cur = (screen == GFX_TOP) ? s_top : s_bot;
    C2D_SceneBegin(s_cur);
}

void ui_clear(u32 colour) {
    C2D_TargetClear(s_cur, colour);
}

// ---------------------------------------------------------------------------
// Camera preview texture
// ---------------------------------------------------------------------------

/*
 * Morton (Z-order) tile encoding for the GPU.
 * The 3DS PICA200 expects textures in 8x8 tile order.
 * For a pixel at (x, y) in a texture of pow2 width tw:
 *
 *   tile_x = x >> 3        (which 8-wide tile column)
 *   tile_y = y >> 3        (which 8-tall tile row)
 *   within_x = x & 7
 *   within_y = y & 7
 *
 *   dst = (tile_y * (tw >> 3) + tile_x) * 64
 *       + interleave_bits(within_x, within_y)   <- 6-bit Morton index
 *
 * interleave_bits gives: y2 x2 y1 x1 y0 x0
 *   = (x&1) | ((y&1)<<1) | ((x&2)<<1) | ((y&2)<<2) | ((x&4)<<2) | ((y&4)<<3)
 *
 * This matches exactly what FBI's screen_load_texture_untiled() does.
 */
static inline u32 morton_offset(int x, int y, int tex_w) {
    int tile_x  = x >> 3;
    int tile_y  = y >> 3;
    int wx      = x & 7;
    int wy      = y & 7;
    int within  = (wx & 1)
                | ((wy & 1) << 1)
                | ((wx & 2) << 1)
                | ((wy & 2) << 2)
                | ((wx & 4) << 2)
                | ((wy & 4) << 3);
    return (u32)(tile_y * (tex_w >> 3) + tile_x) * 64 + within;
}

void ui_cam_tex_init(void) {
    if (s_cam_tex_ready) return;
    // GPU_RGB565, pow2 dimensions
    C3D_TexInit(&s_cam_tex, CAM_TEX_W, CAM_TEX_H, GPU_RGB565);
    C3D_TexSetFilter(&s_cam_tex, GPU_LINEAR, GPU_NEAREST);
    s_cam_tex_ready = true;
}

void ui_cam_tex_free(void) {
    if (!s_cam_tex_ready) return;
    C3D_TexDelete(&s_cam_tex);
    s_cam_tex_ready = false;
}

/*
 * Upload a row-major RGB565 camera frame into the GPU texture.
 * src_w * src_h must be <= CAM_TEX_W * CAM_TEX_H.
 * Called every frame inside a ui_frame_begin/end pair.
 */
void ui_cam_tex_upload(const u16 *src, int src_w, int src_h) {
    if (!s_cam_tex_ready || !src) return;

    u16 *dst = (u16 *)s_cam_tex.data;

    for (int y = 0; y < src_h; y++) {
        for (int x = 0; x < src_w; x++) {
            u32 off = morton_offset(x, y, CAM_TEX_W);
            dst[off] = src[y * src_w + x];
        }
    }

    C3D_TexFlush(&s_cam_tex);
}

/*
 * Draw the camera texture at (x, y) scaled to (w x h) pixels.
 * UV coordinates account for the padded pow2 texture dimensions:
 *   U range: [0, src_w / CAM_TEX_W]
 *   V range: [0, src_h / CAM_TEX_H] (Y axis is flipped for the tilt)
 */
void ui_cam_tex_draw(float x, float y, float w, float h) {
    if (!s_cam_tex_ready) return;

    C2D_Image img;
    img.tex    = &s_cam_tex;
    img.subtex = &(Tex3DS_SubTexture){
        .width  = (u16)SCREEN_TOP_W,
        .height = (u16)SCREEN_H,
        .left   = 0.0f,
        .top    = 1.0f,
        .right  = (float)SCREEN_TOP_W / CAM_TEX_W,
        .bottom = 1.0f - (float)SCREEN_H  / CAM_TEX_H,
    };

    C2D_DrawImageAt(img, x, y, 0.5f,
                    NULL,   // no tint
                    w / SCREEN_TOP_W,
                    h / SCREEN_H);
}

// ---------------------------------------------------------------------------
// Primitives
// ---------------------------------------------------------------------------
void ui_rect(float x, float y, float w, float h, u32 colour) {
    C2D_DrawRectSolid(x, y, 0.0f, w, h, colour);
}

void ui_hline(float x, float y, float len, u32 colour) {
    C2D_DrawRectSolid(x, y, 0.0f, len, 1.0f, colour);
}

void ui_text(float x, float y, float size, u32 colour, const char *str) {
    if (!str || !*str) return;
    C2D_Text t;
    C2D_TextFontParse(&t, s_font, s_tbuf, str);
    C2D_TextOptimize(&t);
    C2D_DrawText(&t, C2D_WithColor, x, y, 0.5f, size, size, colour);
}

float ui_text_width(float size, const char *str) {
    if (!str || !*str) return 0.0f;
    C2D_Text t;
    C2D_TextFontParse(&t, s_font, s_tbuf, str);
    C2D_TextOptimize(&t);
    float w = 0.0f, h = 0.0f;
    C2D_TextGetDimensions(&t, size, size, &w, &h);
    return w;
}

void ui_text_centred(float x, float w, float y, float size, u32 colour,
                     const char *str) {
    float tw = ui_text_width(size, str);
    ui_text(x + (w - tw) * 0.5f, y, size, colour, str);
}
