#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qr.h"
#include "../lib/quirc/quirc.h"

// QR scanner using 3DS camera and quirc library.
// Camera setup and buffer handling matches FBI-NH's capturecam.c exactly.

#define CAM_WIDTH  400
#define CAM_HEIGHT 240
// Total buffer size: width * height * sizeof(u16).
// transferUnit from CAMU_GetMaxBytes is the line pitch only, not the total.
#define CAM_BUF_SIZE (CAM_WIDTH * CAM_HEIGHT * sizeof(u16))
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

// Blit RGB565 camera buffer (row-major) to the top-screen framebuffer
// (column-major BGR8, portrait orientation).
// fb_h = GSP_SCREEN_WIDTH = 240 (column height, from gfxGetFramebuffer).
// pixel (cx, cy) -> fb offset: (cx * fb_h + (fb_h - 1 - cy)) * FB_BPP
static void blit_camera_to_fb(u8 *fb, const u16 *cam, u16 fb_h) {
    for (int cy = 0; cy < CAM_HEIGHT; cy++) {
        for (int cx = 0; cx < CAM_WIDTH; cx++) {
            u16 px = cam[cy * CAM_WIDTH + cx];
            u8 r = ((px >> 11) & 0x1F) << 3;
            u8 g = ((px >>  5) & 0x3F) << 2;
            u8 b =  (px        & 0x1F) << 3;
            u32 off = ((u32)cx * fb_h + (fb_h - 1 - cy)) * FB_BPP;
            fb[off + 0] = b;  // libctru BGR8
            fb[off + 1] = g;
            fb[off + 2] = r;
        }
    }
}

static void draw_crosshair(u8 *fb, u16 fb_h) {
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
            u32 off = ((u32)px * fb_h + (fb_h - 1 - py)) * FB_BPP;
            fb[off + 0] = 0;
            fb[off + 1] = 255;
            fb[off + 2] = 0;
        }
    }
}

int qr_scan(char *out_buf, size_t out_len) {
    if (init_qr_scanner() != 0) return QR_ERROR;

    gfxSetDoubleBuffering(GFX_TOP, true);

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

    // transferUnit is the line pitch in bytes (CAMU_GetMaxBytes).
    // Only used as the last argument to CAMU_SetReceiving, matching FBI.
    u32 transferUnit = 0;
    CAMU_GetMaxBytes(&transferUnit, CAM_WIDTH, CAM_HEIGHT);
    CAMU_SetTransferBytes(PORT_CAM1, transferUnit, CAM_WIDTH, CAM_HEIGHT);

    u16 *cam_buf = (u16*)calloc(1, CAM_BUF_SIZE);
    if (!cam_buf) {
        camExit();
        destroy_qr_scanner();
        return QR_ERROR;
    }

    CAMU_ClearBuffer(PORT_CAM1);
    CAMU_StartCapture(PORT_CAM1);

    // fb_h = GSP_SCREEN_WIDTH = 240 (portrait column height)
    u16 fb_h = 0;
    gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &fb_h, NULL);

    int result = QR_CANCELLED;

    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_B) {
            result = QR_CANCELLED;
            break;
        }

        Handle cam_event = 0;
        svcCreateEvent(&cam_event, RESET_ONESHOT);

        // Flush CPU cache before handing buffer to DMA so the DMA engine
        // doesn't see dirty cache lines being written back over its data.
        GSPGPU_FlushDataCache(cam_buf, CAM_BUF_SIZE);

        CAMU_SetReceiving(&cam_event, cam_buf, PORT_CAM1, CAM_BUF_SIZE, (s16)transferUnit);
        svcWaitSynchronization(cam_event, 400000000LL);
        svcCloseHandle(cam_event);

        // Invalidate CPU cache after DMA completes so the CPU reads the
        // freshly DMA-written data instead of stale cached zeros.
        GSPGPU_InvalidateDataCache(cam_buf, CAM_BUF_SIZE);

        u8 *fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
        blit_camera_to_fb(fb, cam_buf, fb_h);
        draw_crosshair(fb, fb_h);
        gfxFlushBuffers();
        gfxSwapBuffers();

        // Feed grayscale to quirc
        int w, h;
        uint8_t *img = quirc_begin(qr_ctx, &w, &h);
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                u16 px = cam_buf[y * CAM_WIDTH + x];
                img[y * w + x] = (u8)((((px >> 11) & 0x1F) << 3) +
                                       (((px >>  5) & 0x3F) << 2) +
                                       ((px & 0x1F) << 3)) / 3;
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
