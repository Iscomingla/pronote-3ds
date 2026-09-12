#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qr.h"
#include "../lib/quirc/quirc.h"

// QR scanner using 3DS camera and quirc library.
// Camera display is intentionally omitted here — it will be re-added
// once the UI switches to citro2d/citro3d (see TODO: camera preview).

#define CAM_WIDTH  400
#define CAM_HEIGHT 240
// Total buffer size: width * height * sizeof(u16).
// transferUnit from CAMU_GetMaxBytes is the line pitch only, not the total.
#define CAM_BUF_SIZE (CAM_WIDTH * CAM_HEIGHT * sizeof(u16))

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

int qr_scan(char *out_buf, size_t out_len) {
    if (init_qr_scanner() != 0) return QR_ERROR;

    // Instructions on bottom screen
    consoleInit(GFX_BOTTOM, NULL);
    consoleClear();
    printf("\x1b[2;0H");
    printf("=== QR CODE SCANNER ===\n\n");
    printf("Point camera at your\n");
    printf("Pronote QR code.\n\n");
    printf("B: Cancel\n");
    gfxFlushBuffers();
    gfxSwapBuffers();

    // Top screen: plain black while scanning (no preview yet)
    consoleClear();
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

    int result = QR_CANCELLED;

    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_B) {
            result = QR_CANCELLED;
            break;
        }

        Handle cam_event = 0;
        svcCreateEvent(&cam_event, RESET_ONESHOT);

        // Flush before DMA write, invalidate after — required for cache coherency.
        GSPGPU_FlushDataCache(cam_buf, CAM_BUF_SIZE);
        CAMU_SetReceiving(&cam_event, cam_buf, PORT_CAM1, CAM_BUF_SIZE, (s16)transferUnit);
        svcWaitSynchronization(cam_event, 400000000LL);
        svcCloseHandle(cam_event);
        GSPGPU_InvalidateDataCache(cam_buf, CAM_BUF_SIZE);

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

    // Restore top screen for login UI
    consoleInit(GFX_TOP, NULL);

    return result;
}
