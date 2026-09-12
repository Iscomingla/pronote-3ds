#pragma once
#include <3ds.h>
#include <citro2d.h>

// ---------------------------------------------------------------------------
// Pronote colour palette — C2D_Color32(r, g, b, a)
// ---------------------------------------------------------------------------
#define COL_BG       C2D_Color32(0x00, 0x86, 0x73, 0xFF)  // #008673 teal
#define COL_LINE1    C2D_Color32(0xFF, 0xCD, 0x05, 0xFF)  // #FFCD05 gold
#define COL_LINE2    C2D_Color32(0x94, 0xCC, 0x7A, 0xFF)  // #94CC7A light green
#define COL_WHITE    C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF)
#define COL_BLACK    C2D_Color32(0x00, 0x00, 0x00, 0xFF)
#define COL_SELECTED C2D_Color32(0xFF, 0xFF, 0xFF, 0x30)
#define COL_DIMTEXT  C2D_Color32(0x94, 0xCC, 0x7A, 0xAA)

// Screen dimensions
#define SCREEN_TOP_W    400
#define SCREEN_BOT_W    320
#define SCREEN_H        240

// ---------------------------------------------------------------------------
// Camera preview texture
// The GPU requires textures to be power-of-two and Morton (tile) encoded.
// CAM_TEX_W/H are the next pow2 >= CAM_WIDTH/HEIGHT (400->512, 240->256).
// ---------------------------------------------------------------------------
#define CAM_TEX_W  512
#define CAM_TEX_H  256

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
void ui_init(void);
void ui_exit(void);

// ui_suspend / ui_resume are kept for any caller that still needs them,
// but qr.c no longer uses them — the camera thread approach does not
// conflict with citro3d.
void ui_suspend(void);
void ui_resume(void);

// ---------------------------------------------------------------------------
// Frame helpers
// ---------------------------------------------------------------------------
void ui_frame_begin(void);
void ui_frame_end(void);

// ---------------------------------------------------------------------------
// Target helpers
// ui_clear_target MUST be called before ui_target for the same screen.
// ---------------------------------------------------------------------------
C3D_RenderTarget *ui_get_target(gfxScreen_t screen);
void ui_clear_target(C3D_RenderTarget *t, u32 colour);
void ui_target(gfxScreen_t screen);
void ui_clear(u32 colour);  // legacy

// ---------------------------------------------------------------------------
// Camera preview texture
//
// ui_cam_tex_upload(src, w, h)
//   Upload an RGB565 row-major buffer (width*height*2 bytes) into the camera
//   preview texture, applying Morton (tile) encoding as the GPU requires.
//   Must be called inside a ui_frame_begin / ui_frame_end pair.
//
// ui_cam_tex_draw(x, y, w, h)
//   Draw the previously uploaded camera texture at (x,y) scaled to (w x h).
//   Must be called inside a ui_frame_begin / ui_frame_end pair, after
//   ui_target() has been called for the desired screen.
// ---------------------------------------------------------------------------
void ui_cam_tex_init(void);
void ui_cam_tex_free(void);
void ui_cam_tex_upload(const u16 *src, int src_w, int src_h);
void ui_cam_tex_draw(float x, float y, float w, float h);

// ---------------------------------------------------------------------------
// Primitives (call between ui_frame_begin / ui_frame_end)
// ---------------------------------------------------------------------------
void  ui_rect(float x, float y, float w, float h, u32 colour);
void  ui_hline(float x, float y, float len, u32 colour);
void  ui_text(float x, float y, float size, u32 colour, const char *str);
float ui_text_width(float size, const char *str);
void  ui_text_centred(float x, float w, float y, float size, u32 colour, const char *str);
