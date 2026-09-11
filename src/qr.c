#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qr.h"
#include "../lib/quirc/quirc.h"

// QR scanner using 3DS camera and quirc library
// Based on FBI's QR scanning implementation (Steveice10/FBI)

#define CAM_WIDTH  400
#define CAM_HEIGHT 240

// RGB565: 2 bytes per pixel
#define CAM_SIZE   (CAM_WIDTH * CAM_HEIGHT * sizeof(u16))

// The 3DS top-screen framebuffer is BGR8 (3 bytes/pixel), stored column-major,
// rotated 90° CCW. Pixel (x, y) in camera space → framebuffer byte offset:
//   (x * CAM_HEIGHT + (CAM_HEIGHT - 1 - y)) * 3
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

// Blit an RGB565 camera frame to the top-screen framebuffer (BGR8, column-major).
// Camera buffer layout: buf[y * CAM_WIDTH + x] = RGB565 pixel (row-major).
static void blit_rgb565_to_top_screen(const u16 *rgb565) {
    u16 fb_w, fb_h;
    u8 *fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &fb_w, &fb_h);
    if (!fb) return;

    for (int y = 0; y < CAM_HEIGHT; y++) {
        for (int x = 0; x < CAM_WIDTH; x++) {
            u16 px = rgb565[y * CAM_WIDTH + x];

            // RGB565 → BGR8
            u8 r = ((px >> 11) & 0x1F) << 3;
            u8 g = ((px >>  5) & 0x3F) << 2;
            u8 b =  (px        & 0x1F) << 3;

            int off = (x * CAM_HEIGHT + (CAM_HEIGHT - 1 - y)) * FB_BPP;
            fb[off + 0] = b;
            fb[off + 1] = g;
            fb[off + 2] = r;
        }
    }
}

// Draw a green crosshair in the centre of the top screen to aid aiming.
static void draw_crosshair(void) {
    u16 fb_w, fb_h;
    u8 *fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &fb_w, &fb_h);
    if (!fb) return;

    const int cx  = CAM_WIDTH  / 2;
    const int cy  = CAM_HEIGHT / 2;
    const int arm = 28;
    const int gap = 7;

    for (int i = gap; i <= arm; i++) {
        int coords[4][2] = {
            {cx + i, cy},
            {cx - i, cy},
            {cx,     cy + i},
            {cx,     cy - i},
        };
        for (int k = 0; k < 4; k++) {
            int px = coords[k][0], py = coords[k][1];
            if (px < 0 || px >= CAM_WIDTH || py < 0 || py >= CAM_HEIGHT) continue;
            int off = (px * CAM_HEIGHT + (CAM_HEIGHT - 1 - py)) * FB_BPP;
            fb[off + 0] = 0;    // B
            fb[off + 1] = 255;  // G
            fb[off + 2] = 0;    // R
        }
    }
}

int qr_scan(char *out_buf, size_t out_len) {
    if (init_qr_scanner() != 0) return QR_ERROR;

    // Take over the top screen for raw framebuffer rendering
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

    // Init camera in RGB565 mode (same as FBI — no YUV conversion needed)
    camInit();
    CAMU_SetSize(SELECT_OUT1, SIZE_CTR_TOP_LCD, CONTEXT_A);
    CAMU_SetOutputFormat(SELECT_OUT1, OUTPUT_RGB_565, CONTEXT_A);
    CAMU_SetFrameRate(SELECT_OUT1, FRAME_RATE_30);
    CAMU_SetNoiseFilter(SELECT_OUT1, true);
    CAMU_SetAutoExposure(SELECT_OUT1, true);
    CAMU_SetAutoWhiteBalance(SELECT_OUT1, true);
    CAMU_Activate(SELECT_OUT1);

    u16 *cam_buf = (u16*)malloc(CAM_SIZE);
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
        if (hidKeysDown() & KEY_B) {
            result = QR_CANCELLED;
            break;
        }

        // Capture one frame
        Handle cam_event = 0;
        svcCreateEvent(&cam_event, RESET_ONESHOT);
        CAMU_SetReceiving(&cam_event, cam_buf, PORT_CAM1, CAM_SIZE, (s16)CAM_WIDTH);
        svcWaitSynchronization(cam_event, 400000000LL);
        svcCloseHandle(cam_event);

        // Render frame + crosshair to top screen
        blit_rgb565_to_top_screen(cam_buf);
        draw_crosshair();
        gfxFlushBuffers();
        gfxSwapBuffers();

        // Feed grayscale to quirc — luma from RGB565, same formula as FBI
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

        // Check for a decoded QR code
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
