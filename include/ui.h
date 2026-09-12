#pragma once
#include <3ds.h>
#include <citro2d.h>

// ---------------------------------------------------------------------------
// Pronote colour palette
// citro2d colours are 0xAABBGGRR (little-endian RGBA packed as u32)
// ---------------------------------------------------------------------------
#define COL_BG       C2D_Color32(0x00, 0x86, 0x73, 0xFF)  // #008673 teal
#define COL_LINE1    C2D_Color32(0x05, 0xCD, 0xFF, 0xFF)  // #FFCD05 gold
#define COL_LINE2    C2D_Color32(0x80, 0xFF, 0xFF, 0xFF)  // #FFFF80 pale yellow
#define COL_WHITE    C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF)
#define COL_BLACK    C2D_Color32(0x00, 0x00, 0x00, 0xFF)
#define COL_SELECTED C2D_Color32(0xFF, 0xFF, 0xFF, 0x30)  // translucent white highlight
#define COL_DIMTEXT  C2D_Color32(0xCC, 0xFF, 0xF0, 0xFF)  // muted light teal for labels

// Top screen: 400x240   Bottom screen: 320x240
#define SCREEN_TOP_W    400
#define SCREEN_BOT_W    320
#define SCREEN_H        240

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
void ui_init(void);
void ui_exit(void);

// ---------------------------------------------------------------------------
// Frame helpers
// ---------------------------------------------------------------------------
void ui_frame_begin(void);
void ui_frame_end(void);

// ---------------------------------------------------------------------------
// Target helpers
// ui_clear_target MUST be called before ui_target for the same screen,
// because C2D_TargetClear must come before C2D_SceneBegin.
// ---------------------------------------------------------------------------
C3D_RenderTarget *ui_get_target(gfxScreen_t screen);
void ui_clear_target(C3D_RenderTarget *t, u32 colour);

// Switch active render target.
void ui_target(gfxScreen_t screen);

// Fill current target (legacy — prefer ui_clear_target).
void ui_clear(u32 colour);

// ---------------------------------------------------------------------------
// Primitives (call between ui_frame_begin / ui_frame_end)
// ---------------------------------------------------------------------------
void  ui_rect(float x, float y, float w, float h, u32 colour);
void  ui_hline(float x, float y, float len, u32 colour);
void  ui_text(float x, float y, float size, u32 colour, const char *str);
float ui_text_width(float size, const char *str);
void  ui_text_centred(float x, float w, float y, float size, u32 colour, const char *str);
