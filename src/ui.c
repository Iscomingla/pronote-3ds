/*
 * ui.c — citro2d UI layer for notApro
 *
 * Design notes:
 *   - C2D_FontLoadSystem() is called once and cached; calling it per
 *     draw would reload the font handle on every frame.
 *   - The text buffer is cleared at the start of every frame (ui_frame_begin)
 *     so it never grows unboundedly across frames.
 *   - s_cur tracks the currently active render target so callers don't
 *     need to pass it through every primitive call.
 */

#include "ui.h"
#include <string.h>
#include <stdio.h>

/* -------------------------------------------------------------------------
 * Internal state
 * ---------------------------------------------------------------------- */
static C3D_RenderTarget *s_top  = NULL;
static C3D_RenderTarget *s_bot  = NULL;
static C3D_RenderTarget *s_cur  = NULL;

static C2D_TextBuf s_tbuf;
static C2D_Font    s_font = NULL;   /* system font, loaded once */

/* -------------------------------------------------------------------------
 * Lifecycle
 * ---------------------------------------------------------------------- */
void ui_init(void) {
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    s_top  = C2D_CreateScreenTarget(GFX_TOP,    GFX_LEFT);
    s_bot  = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    s_cur  = s_top;

    /* 1024 glyphs is plenty for all strings in a single login frame */
    s_tbuf = C2D_TextBufNew(1024);

    /* Load the 3DS system font once — cheap to keep alive */
    s_font = C2D_FontLoadSystem();
}

void ui_exit(void) {
    C2D_TextBufDelete(s_tbuf);
    /* s_font is owned by citro2d; no manual free needed */
    C2D_Fini();
    C3D_Fini();
}

/* -------------------------------------------------------------------------
 * Frame
 * ---------------------------------------------------------------------- */
void ui_frame_begin(void) {
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TextBufClear(s_tbuf);
}

void ui_frame_end(void) {
    C3D_FrameEnd(0);
}

/* -------------------------------------------------------------------------
 * Target selection
 * ---------------------------------------------------------------------- */
void ui_target(gfxScreen_t screen) {
    s_cur = (screen == GFX_TOP) ? s_top : s_bot;
    C2D_SceneBegin(s_cur);
}

/* -------------------------------------------------------------------------
 * Primitives
 * ---------------------------------------------------------------------- */
void ui_clear(u32 colour) {
    C2D_TargetClear(s_cur, colour);
}

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
