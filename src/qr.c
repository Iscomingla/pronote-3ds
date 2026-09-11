#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qr.h"
#include "../lib/quirc/quirc.h"

// QR scanner using 3DS camera and quirc library
// Blit logic taken directly from devkitPro/3ds-examples camera/video example.

#define CAM_WIDTH  400
#define CAM_HEIGHT 240

#define FB_BPP 3  // framebuffer is 3 bytes per pixel

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

// Copied verbatim from devkitPro/3ds-examples camera/video/source/main.c
// fb  : pointer from gfxGetFramebuffer()
// img : RGB565 camera buffer, row-major [y * width + x]
// x, y: destination offset on screen (use 0, 0 for full screen)
static void writePictureToFramebufferRGB565(void *fb, void *img, u16 x, u16 y, u16 width, u16 height) {
    u8  *fb_8   = (u8  *) fb;
    u16 *img_16 = (u16 *) img;

    for (int j = 0; j < height; j++) {
        for (int i = 0; i < width; i++) {
            int draw_y = y + height - j;   // flip vertically (screen is rotated 90° CCW)
            int draw_x = x + i;

            u32 v = (draw_y + draw_x * height) * FB_BPP;

            u16 data = img_16[j * width + i];
            u8 b = ((data >> 11) & 0x1F) << 3;
            u8 g = ((data >>  5) & 0x3F) << 2;
            u8 r =  (data        & 0x1F) << 3;

            fb_8[v]     = r;
            fb_8[v + 1] = g;
            fb_8[v + 2] = b;
        }
    }
}

// Draw a green crosshair in the centre of the top screen.
// Uses the same column-major offset formula as writePictureToFramebufferRGB565.
static void draw_crosshair(u8 *fb) {
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
            // Same mapping: v = (draw_y + draw_x * height) * 3
            // draw_y = 0 + CAM_HEIGHT - py,  draw_x = 0 + px
            u32 v = ((CAM_HEIGHT - py) + px * CAM_HEIGHT) * FB_BPP;
            fb[v]     = 0;    // R
            fb[v + 1] = 255;  // G
            fb[v + 2] = 0;    // B
        }
    }
}

int qr_scan(char *out_buf, size_t out_len) {
    if (init_qr_scanner() != 0) return QR_ERROR;

    // Take over top screen for raw framebuffer rendering
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

    // Init camera — RGB565, same as devkitPro example
    camInit();
    CAMU_SetSize(SELECT_OUT1, SIZE_CTR_TOP_LCD, CONTEXT_A);
    CAMU_SetOutputFormat(SELECT_OUT1, OUTPUT_RGB_565, CONTEXT_A);
    CAMU_SetFrameRate(SELECT_OUT1, FRAME_RATE_30);
    CAMU_SetNoiseFilter(SELECT_OUT1, true);
    CAMU_SetAutoExposure(SELECT_OUT1, true);
    CAMU_SetAutoWhiteBalance(SELECT_OUT1, true);
    CAMU_Activate(SELECT_OUT1);

    // Use CAMU_GetMaxBytes for the correct transfer size, same as the example
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
        CAMU_SetReceiving(&cam_event, cam_buf, PORT_CAM1,
                          CAM_WIDTH * CAM_HEIGHT * sizeof(u16), (s16)bufSize);
        svcWaitSynchronization(cam_event, 400000000LL);
        svcCloseHandle(cam_event);

        // Blit frame then crosshair to top screen
        u8 *fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
        writePictureToFramebufferRGB565(fb, cam_buf, 0, 0, CAM_WIDTH, CAM_HEIGHT);
        draw_crosshair(fb);
        gfxFlushBuffers();
        gfxSwapBuffers();

        // Feed grayscale to quirc (luma from RGB565)
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
