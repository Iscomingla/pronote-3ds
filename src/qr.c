#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qr.h"
#include "../lib/quirc/quirc.h"

// QR scanner using 3DS camera and quirc library

#define CAM_WIDTH  400
#define CAM_HEIGHT 240

// The 3DS framebuffer is portrait (rotated 90° CCW).
// From gfx.c: stride = GSP_SCREEN_WIDTH * bytesPerPixel = 240 * 3 = 720
// gfxGetFramebuffer returns height = GSP_SCREEN_HEIGHT_TOP = 400 (the tall dimension).
// So the column-major stride multiplier is FB_STRIDE = 400, NOT 240.
// pixel (cam_x, cam_y) -> fb offset: (cam_x * FB_STRIDE + (FB_STRIDE - 1 - cam_y)) * 3
// BUT: the devkitPro example passes HEIGHT=240 and uses (draw_y + draw_x * height)*3
// which works because their fb height param matches GSP_SCREEN_WIDTH=240.
// The real stride in bytes per column = GSP_SCREEN_HEIGHT_TOP * 3 = 400 * 3 = 1200? No.
// Actually stride = GSP_SCREEN_WIDTH * bpp = 240 * 3 bytes, which is the PITCH per scanline.
// Column-major means one column = GSP_SCREEN_WIDTH pixels tall = 240 pixels = 720 bytes.
// So: offset of pixel (x, y) = (x * GSP_SCREEN_WIDTH + (GSP_SCREEN_WIDTH - 1 - y)) * bpp
//                             = (x * 240 + (239 - y)) * 3
// The devkitPro example uses `height` as the column pitch (240), which is correct.
// Our bug: we pass CAM_HEIGHT=240 as `height` but CAM_HEIGHT IS 240 — same value.
// So the formula was correct but R/B swap in the devkitPro example is actually BGR not RGB.
// Let the framebuffer tell us the actual fb height via gfxGetFramebuffer width param.

#define FB_BPP 3

static struct quirc *qr_ctx = NULL;

static int init_qr_scanner() {
    qr_ctx = quirc_new();
    if (!qr_ctx) return -1;
    if (quirc_resize(qr_ctx, CAM_WIDTH, CAM_HEIGHT) < 0) {
        quirc_destroy(qr_ctx);
        qr_ctx = NULL;
        return -1;
    }
    return 0;
}

static void destroy_qr_scanner() {
    if (qr_ctx) {
        quirc_destroy(qr_ctx);
        qr_ctx = NULL;
    }
}

// Blit RGB565 camera frame to the top-screen framebuffer.
// fb_height: the value returned by gfxGetFramebuffer in the *width* param
//            (libctru calls it width because the screen is portrait — it's 240).
// The column stride in the framebuffer is fb_height pixels = fb_height * FB_BPP bytes.
// pixel (cam_x, cam_y) maps to: (cam_x * fb_height + (fb_height - 1 - cam_y)) * FB_BPP
static void blit_camera_to_fb(u8 *fb, const u16 *cam, u16 fb_height) {
    for (int cy = 0; cy < CAM_HEIGHT; cy++) {
        for (int cx = 0; cx < CAM_WIDTH; cx++) {
            u16 px = cam[cy * CAM_WIDTH + cx];

            u8 r = ((px >> 11) & 0x1F) << 3;
            u8 g = ((px >>  5) & 0x3F) << 2;
            u8 b =  (px        & 0x1F) << 3;

            // Column-major, y-flipped to correct portrait rotation
            u32 off = (cx * fb_height + (fb_height - 1 - cy)) * FB_BPP;
            fb[off + 0] = b;  // libctru BGR8 format: blue first
            fb[off + 1] = g;
            fb[off + 2] = r;
        }
    }
}

// Green crosshair using the same offset formula as blit_camera_to_fb.
static void draw_crosshair(u8 *fb, u16 fb_height) {
    const int cx  = CAM_WIDTH  / 2;
    const int cy  = CAM_HEIGHT / 2;
    const int arm = 28;
    const int gap = 7;

    for (int i = gap; i <= arm; i++) {
        int coords[4][2] = {
            {cx + i, cy}, {cx - i, cy},
            {cx, cy + i}, {cx, cy - i},
        };
        for (int k = 0; k < 4; k++) {
            int px = coords[k][0], py = coords[k][1];
            if (px < 0 || px >= CAM_WIDTH || py < 0 || py >= CAM_HEIGHT) continue;
            u32 off = (px * fb_height + (fb_height - 1 - py)) * FB_BPP;
            fb[off + 0] = 0;
            fb[off + 1] = 255;
            fb[off + 2] = 0;
        }
    }
}

int qr_scan(char *out_buf, size_t out_len) {
    if (init_qr_scanner() != 0) return QR_ERROR;

    gfxSetDoubleBuffering(GFX_TOP, true);

    // Instructions on the bottom screen
    consoleInit(GFX_BOTTOM, NULL);
    consoleClear();
    printf("\x1b[2;0H");
    printf("=== QR CODE SCANNER ===\n\n");
    printf("Point camera at Pronote QR\n");
    printf("code shown on TOP screen.\n\n");
    printf("B: Cancel\n");
    gfxFlushBuffers();
    gfxSwapBuffers();

    camInit();
    CAMU_SetSize(SELECT_OUT1, SIZE_CTR_TOP_LCD, CONTEXT_A);
    CAMU_SetOutputFormat(SELECT_OUT1, OUTPUT_RGB_565, CONTEXT_A);
    CAMU_SetFrameRate(SELECT_OUT1, FRAME_RATE_30);
    CAMU_SetNoiseFilter(SELECT_OUT1, true);
    CAMU_SetAutoExposure(SELECT_OUT1, true);
    CAMU_SetAutoWhiteBalance(SELECT_OUT1, true);
    CAMU_Activate(SELECT_OUT1);

    u32 bufSize = 0;
    CAMU_GetMaxBytes(&bufSize, CAM_WIDTH, CAM_HEIGHT);
    CAMU_SetTransferBytes(PORT_CAM1, bufSize, CAM_WIDTH, CAM_HEIGHT);

    u16 *cam_buf = (u16*)malloc(CAM_WIDTH * CAM_HEIGHT * sizeof(u16));
    if (!cam_buf) {
        camExit();
        destroy_qr_scanner();
        return QR_ERROR;
    }

    CAMU_ClearBuffer(PORT_CAM1);
    CAMU_StartCapture(PORT_CAM1);

    // Ask libctru for the real framebuffer height (= GSP_SCREEN_WIDTH = 240)
    // so our stride calculation always matches what gfx.c actually allocated.
    u16 fb_w = 0, fb_h = 0;
    gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &fb_w, &fb_h);
    // fb_w = GSP_SCREEN_WIDTH = 240 (the portrait "width" = column height in pixels)

    int result = QR_CANCELLED;

    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_B) {
            result = QR_CANCELLED;
            break;
        }

        Handle cam_event = 0;
        svcCreateEvent(&cam_event, RESET_ONESHOT);
        CAMU_SetReceiving(&cam_event, cam_buf, PORT_CAM1,
                          CAM_WIDTH * CAM_HEIGHT * sizeof(u16), (s16)bufSize);
        svcWaitSynchronization(cam_event, 400000000LL);
        svcCloseHandle(cam_event);

        u8 *fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
        blit_camera_to_fb(fb, cam_buf, fb_w);
        draw_crosshair(fb, fb_w);
        gfxFlushBuffers();
        gfxSwapBuffers();

        // Feed grayscale to quirc
        int w, h;
        uint8_t *img = quirc_begin(qr_ctx, &w, &h);
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                u16 px = cam_buf[y * CAM_WIDTH + x];
                u8 r = ((px >> 11) & 0x1F) << 3;
                u8 g = ((px >>  5) & 0x3F) << 2;
                u8 b =  (px        & 0x1F) << 3;
                img[y * w + x] = (r + g + b) / 3;
            }
        }
        quirc_end(qr_ctx);

        int num = quirc_count(qr_ctx);
        if (num > 0) {
            struct quirc_code code;
            struct quirc_data data;
            quirc_extract(qr_ctx, 0, &code);
            if (quirc_decode(&code, &data) == QUIRC_SUCCESS) {
                size_t copy_len = data.payload_len;
                if (copy_len >= out_len) copy_len = out_len - 1;
                memcpy(out_buf, data.payload, copy_len);
                out_buf[copy_len] = '\0';
                result = QR_SUCCESS;
                break;
            }
        }

        gspWaitForVBlank();
    }

    CAMU_StopCapture(PORT_CAM1);
    CAMU_Activate(SELECT_NONE);
    free(cam_buf);
    camExit();
    destroy_qr_scanner();

    consoleInit(GFX_TOP, NULL);

    return result;
}
