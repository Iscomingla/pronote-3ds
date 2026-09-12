#pragma once
/*
 * ui.h — citro2d UI layer for notApro
 *
 * Pronote theme:
 *   Background : #008673  COL_BG
 *   Line 1     : #FFCD05  COL_LINE1  (gold)
 *   Line 2     : #FFFF80  COL_LINE2  (pale yellow)
 *
 * All colours are C2D_Color32(r, g, b, a) — RGBA8.
 * Screen dimensions follow libctru conventions:
 *   Top    : 400 × 240
 *   Bottom : 320 × 240
 *
 * IMPORTANT — clear/scene ordering:
 *   C2D_TargetClear MUST happen before C2D_SceneBegin.
 *   Use the pattern:
 *     ui_clear_target(ui_get_target(GFX_TOP), COL_BG);
 *     ui_target(GFX_TOP);
 *     ... draw calls ...
 *     ui_clear_target(ui_get_target(GFX_BOTTOM), COL_BG);
 *     ui_target(GFX_BOTTOM);
 *     ... draw calls ...
 */

#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>

/* -------------------------------------------------------------------------
 * Pronote colour palette
 * ---------------------------------------------------------------------- */
#define COL_BG        C2D_Color32(0x00, 0x86, 0x73, 0xFF)  /* #008673 */
#define COL_LINE1     C2D_Color32(0xFF, 0xCD, 0x05, 0xFF)  /* #FFCD05 gold  */
#define COL_LINE2     C2D_Color32(0xFF, 0xFF, 0x80, 0xFF)  /* #FFFF80 pale  */
#define COL_WHITE     C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF)
#define COL_DIMTEXT   C2D_Color32(0xCC, 0xFF, 0xF0, 0xB0)
#define COL_SELECTED  C2D_Color32(0x00, 0x50, 0x40, 0xA0)  /* highlight bg */

/* -------------------------------------------------------------------------
 * Screen geometry
 * ---------------------------------------------------------------------- */
#define SCREEN_TOP_W  400.0f
#define SCREEN_BOT_W  320.0f
#define SCREEN_H      240.0f

/* -------------------------------------------------------------------------
 * Lifecycle
 * ---------------------------------------------------------------------- */
void  ui_init(void);
void  ui_exit(void);

/* -------------------------------------------------------------------------
 * Frame
 * ---------------------------------------------------------------------- */
void  ui_frame_begin(void);
void  ui_frame_end(void);

/* -------------------------------------------------------------------------
 * Target selection  (call after ui_clear_target, before draw calls)
 * ---------------------------------------------------------------------- */
void               ui_target(gfxScreen_t screen);
C3D_RenderTarget  *ui_get_target(gfxScreen_t screen);

/* -------------------------------------------------------------------------
 * Clear — MUST be called before ui_target() for the same screen
 * ---------------------------------------------------------------------- */
void  ui_clear_target(C3D_RenderTarget *t, u32 colour);
void  ui_clear(u32 colour);   /* clears current target; prefer ui_clear_target */

/* -------------------------------------------------------------------------
 * Primitives
 * ---------------------------------------------------------------------- */
void  ui_rect(float x, float y, float w, float h, u32 colour);
void  ui_hline(float x, float y, float len, u32 colour);

/* Text — size 1.0f ≈ system-font native size (~24 px)
 * Typical usage: 0.45f (small labels), 0.55f (values), 0.65f (headers). */
void  ui_text(float x, float y, float size, u32 colour, const char *str);
float ui_text_width(float size, const char *str);
void  ui_text_centred(float x, float w, float y, float size, u32 colour,
                      const char *str);
