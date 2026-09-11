#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qr.h"
#include "../lib/quirc/quirc.h"

// QR scanner using 3DS camera and quirc library
// Based on FBI-NH's QR scanning implementation

#define CAM_WIDTH  400
#define CAM_HEIGHT 240
#define CAM_SIZE   (CAM_WIDTH * CAM_HEIGHT * 2)  // YUV422

// Top screen framebuffer is 240 rows x 400 cols, stored column-major (rotated 90°).
// gfxGetFramebuffer returns a pointer to BGR8 (3 bytes per pixel).
#define FB_WIDTH   400
#define FB_HEIGHT  240
#define FB_BPP     3   // BGR8

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

// Convert a single YUV422 frame to BGR and blit it to the top screen framebuffer.
// The 3DS top-screen framebuffer is stored column-major (each column top-to-bottom),
// so pixel (x, y) maps to framebuffer offset: (x * FB_HEIGHT + (FB_HEIGHT - 1 - y)) * FB_BPP
static void blit_yuv422_to_top_screen(const u8 *yuv, u16 width, u16 height) {
    u16 fb_w, fb_h;
    u8 *fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &fb_w, &fb_h);
    if (!fb) return;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x += 2) {
            // YUV422 packs two pixels as: Y0 U0 Y1 V0
            int base = (y * width + x) * 2;
            u8 Y0 = yuv[base + 0];
            u8 U  = yuv[base + 1];
            u8 Y1 = yuv[base + 2];
            u8 V  = yuv[base + 3];

            // YUV -> RGB conversion (BT.601)
            int C0 = (int)Y0 - 16;
            int C1 = (int)Y1 - 16;
            int D  = (int)U  - 128;
            int E  = (int)V  - 128;

            // Pixel 0
            int r0 = (298 * C0           + 409 * E + 128) >> 8;
            int g0 = (298 * C0 - 100 * D - 208 * E + 128) >> 8;
            int b0 = (298 * C0 + 516 * D           + 128) >> 8;

            // Pixel 1
            int r1 = (298 * C1           + 409 * E + 128) >> 8;
            int g1 = (298 * C1 - 100 * D - 208 * E + 128) >> 8;
            int b1 = (298 * C1 + 516 * D           + 128) >> 8;

            // Clamp
            #define CLAMP(v) ((v) < 0 ? 0 : (v) > 255 ? 255 : (v))

            // Write pixel 0 — column-major: offset = (x * height + (height-1-y)) * bpp
            int off0 = (x * FB_HEIGHT + (FB_HEIGHT - 1 - y)) * FB_BPP;
            fb[off0 + 0] = CLAMP(b0);
            fb[off0 + 1] = CLAMP(g0);
            fb[off0 + 2] = CLAMP(r0);

            // Write pixel 1
            int off1 = ((x + 1) * FB_HEIGHT + (FB_HEIGHT - 1 - y)) * FB_BPP;
            fb[off1 + 0] = CLAMP(b1);
            fb[off1 + 1] = CLAMP(g1);
            fb[off1 + 2] = CLAMP(r1);

            #undef CLAMP
        }
    }
}

// Draw a targeting crosshair overlay in the centre of the top screen.
// Draws directly into the framebuffer after blit_yuv422_to_top_screen().
static void draw_crosshair() {
    u16 fb_w, fb_h;
    u8 *fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &fb_w, &fb_h);
    if (!fb) return;

    const int cx = FB_WIDTH  / 2;
    const int cy = FB_HEIGHT / 2;
    const int arm   = 30;   // half-length of each arm
    const int gap   = 8;    // gap around centre
    const u8  R = 0, G = 255, B = 0;  // green

    // Draw horizontal and vertical arms, skipping the centre gap
    for (int i = gap; i <= arm; i++) {
        // right arm
        int off = ((cx + i) * FB_HEIGHT + (FB_HEIGHT - 1 - cy)) * FB_BPP;
        fb[off] = B; fb[off+1] = G; fb[off+2] = R;
        // left arm
        off = ((cx - i) * FB_HEIGHT + (FB_HEIGHT - 1 - cy)) * FB_BPP;
        fb[off] = B; fb[off+1] = G; fb[off+2] = R;
        // bottom arm (y increases downward in camera space)
        off = (cx * FB_HEIGHT + (FB_HEIGHT - 1 - (cy + i))) * FB_BPP;
        fb[off] = B; fb[off+1] = G; fb[off+2] = R;
        // top arm
        off = (cx * FB_HEIGHT + (FB_HEIGHT - 1 - (cy - i))) * FB_BPP;
        fb[off] = B; fb[off+1] = G; fb[off+2] = R;
    }
}

int qr_scan(char *out_buf, size_t out_len) {
    if (init_qr_scanner() != 0) return QR_ERROR;

    // Switch top screen out of console mode so we can write raw framebuffer pixels
    gfxSetDoubleBuffering(GFX_TOP, true);

    // Show instructions on bottom screen
    consoleInit(GFX_BOTTOM, NULL);
    consoleClear();
    printf("\x1b[2;0H");
    printf("=== QR CODE SCANNER ===\n\n");
    printf("Point camera at Pronote QR\n");
    printf("code on the TOP screen.\n\n");
    printf("B: Cancel\n");

    // Init camera
    camInit();
    CAMU_SetSize(SELECT_OUT1, SIZE_CTR_TOP_LCD, CONTEXT_A);
    CAMU_SetOutputFormat(SELECT_OUT1, OUTPUT_YUV_422, CONTEXT_A);
    CAMU_SetFrameRate(SELECT_OUT1, FRAME_RATE_30);
    CAMU_SetNoiseFilter(SELECT_OUT1, true);
    CAMU_SetAutoExposure(SELECT_OUT1, true);
    CAMU_SetAutoWhiteBalance(SELECT_OUT1, true);
    CAMU_Activate(SELECT_OUT1);

    Handle cam_event = 0;
    u8 *cam_buf = (u8*)malloc(CAM_SIZE);
    if (!cam_buf) {
        camExit();
        destroy_qr_scanner();
        return QR_ERROR;
    }

    CAMU_ClearBuffer(PORT_CAM1);
    CAMU_StartCapture(PORT_CAM1);
    CAMU_SetTransferBytes(PORT_CAM1, CAM_SIZE, CAM_WIDTH, CAM_HEIGHT);

    int result = QR_CANCELLED;

    while (aptMainLoop()) {
        hidScanInput();
        u32 kdown = hidKeysDown();

        if (kdown & KEY_B) {
            result = QR_CANCELLED;
            break;
        }

        // Capture frame
        svcCreateEvent(&cam_event, RESET_ONESHOT);
        CAMU_SetReceiving(&cam_event, cam_buf, PORT_CAM1, CAM_SIZE, (s16)CAM_WIDTH);
        svcWaitSynchronization(cam_event, 400000000LL);
        svcCloseHandle(cam_event);
        cam_event = 0;

        // Blit camera frame to top screen, then overlay crosshair
        blit_yuv422_to_top_screen(cam_buf, CAM_WIDTH, CAM_HEIGHT);
        draw_crosshair();
        gfxFlushBuffers();
        gfxSwapBuffers();

        // Feed to quirc (convert YUV to grayscale)
        int w, h;
        uint8_t *img = quirc_begin(qr_ctx, &w, &h);
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                // YUV422: Y is every 2nd byte
                img[y * w + x] = cam_buf[(y * w + x) * 2];
            }
        }
        quirc_end(qr_ctx);

        // Check for decoded QR codes
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

    // Restore top screen to console mode for the login UI
    consoleInit(GFX_TOP, NULL);

    return result;
}
