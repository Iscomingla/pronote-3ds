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
// Lifecycle — call once at startup / shutdown
// ---------------------------------------------------------------------------
void ui_init(void);
void ui_exit(void);

// ---------------------------------------------------------------------------
// Frame helpers
// Call ui_frame_begin() at the start of each render pass.
// Call ui_frame_end()   to flush both screens.
// ---------------------------------------------------------------------------
void ui_frame_begin(void);
void ui_frame_end(void);

// ---------------------------------------------------------------------------
// Primitive drawing (must be called between ui_frame_begin / ui_frame_end)
// ---------------------------------------------------------------------------

// Switch active render target.  screen = GFX_TOP or GFX_BOTTOM.
void ui_target(gfxScreen_t screen);

// Get the underlying C3D_RenderTarget for a screen (for ui_clear_target).
C3D_RenderTarget *ui_get_target(gfxScreen_t screen);

// Clear a specific render target (call BEFORE ui_target for that screen,
// because C2D_TargetClear must come before C2D_SceneBegin).
void ui_clear_target(C3D_RenderTarget *t, u32 colour);

// Fill the whole current target with a solid colour (legacy, prefer ui_clear_target).
void ui_clear(u32 colour);

// Filled rectangle.
void ui_rect(float x, float y, float w, float h, u32 colour);

// Horizontal line (1 px tall).
void ui_hline(float x, float y, float len, u32 colour);

// Draw UTF-8 text using the built-in system font.
// size: 0.5f = small, 0.6f = normal, 0.7f = large
void ui_text(float x, float y, float size, u32 colour, const char *str);

// Measure text width (for centering).
float ui_text_width(float size, const char *str);

// Draw text centred in [x, x+w].
void ui_text_centred(float x, float w, float y, float size, u32 colour, const char *str);
