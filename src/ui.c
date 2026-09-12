/*
 * ui.c — citro2d UI layer for notApro
 *
 * Design notes:
 *   - C2D_FontLoadSystem(CFG_REGION_EUR) is called once and cached.
 *     EUR covers the full Latin charset and works on all 3DS regions.
 *     If it returns NULL (some firmwares), we fall back to the built-in
 *     default font via C2D_TextFontParse with font=NULL.
 *   - The text buffer is cleared at the start of every frame (ui_frame_begin)
 *     so it never grows unboundedly across frames.
 *   - s_cur tracks the currently active render target so callers don't
 *     need to pass it through every primitive call.
 *   - ui_clear() MUST be called before ui_target() switches the scene,
 *     i.e. call ui_clear() then ui_target() — NOT the other way around.
 *     C2D_TargetClear must happen before C2D_SceneBegin.
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
static C2D_Font    s_font = NULL;   /* system font, or NULL = built-in default */

/* -------------------------------------------------------------------------
 * Lifecycle
 * ---------------------------------------------------------------------- */
void ui_init(void) {
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    s_top = C2D_CreateScreenTarget(GFX_TOP,    GFX_LEFT);
    s_bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    s_cur = s_top;

    /* 1024 glyphs is plenty for all strings in a single login frame */
    s_tbuf = C2D_TextBufNew(1024);

    /*
     * CFG_REGION_EUR: full Latin charset, works on all console regions.
     * May return NULL on some firmwares — we keep s_font = NULL in that
     * case and pass NULL to C2D_TextFontParse, which uses the built-in font.
     */
    s_font = C2D_FontLoadSystem(CFG_REGION_EUR);
    /* s_font == NULL is handled gracefully — no crash */
}

void ui_exit(void) {
    if (s_font) {
        C2D_FontFree(s_font);
        s_font = NULL;
    }
    C2D_TextBufDelete(s_tbuf);
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

/*
 * ui_clear_target — clear a render target.
 * MUST be called BEFORE ui_target() for that screen, because
 * C2D_TargetClear must happen before C2D_SceneBegin.
 */
void ui_clear_target(C3D_RenderTarget *t, u32 colour) {
    C2D_TargetClear(t, colour);
}

void ui_target(gfxScreen_t screen) {
    s_cur = (screen == GFX_TOP) ? s_top : s_bot;
    C2D_SceneBegin(s_cur);
}

C3D_RenderTarget *ui_get_target(gfxScreen_t screen) {
    return (screen == GFX_TOP) ? s_top : s_bot;
}

/* -------------------------------------------------------------------------
 * Primitives
 * ---------------------------------------------------------------------- */
void ui_clear(u32 colour) {
    /*
     * C2D_TargetClear must happen BEFORE C2D_SceneBegin (i.e. before
     * ui_target). Callers must use the pattern:
     *   ui_clear_target(ui_get_target(GFX_TOP), COL_BG);
     *   ui_target(GFX_TOP);
     *   ... draw calls ...
     * This function clears the CURRENT target and is safe only if called
     * before any draw calls on a freshly begun scene — kept for compat but
     * prefer ui_clear_target().
     */
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
    C2D_TextFontParse(&t, s_font, s_tbuf, str);  /* s_font==NULL → built-in */
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
