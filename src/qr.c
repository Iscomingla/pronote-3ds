/*
 * qr.c — QR code scanner using 3DS outer camera + quirc.
 *
 * Camera display is intentionally omitted here — will be re-added
 * once the UI switches to citro2d/citro3d (see TODO: camera preview).
 *
 * Event model: RESET_STICKY, re-armed after every received frame.
 * This matches the FBI-NH capturecam approach and avoids the pitfalls
 * of creating a fresh RESET_ONESHOT event on every iteration.
 */

#include <3ds.h>
#include <stdlib.h>
#include <string.h>
#include "qr.h"
#include "ui.h"
#include "../lib/quirc/quirc.h"

#define CAM_WIDTH   400
#define CAM_HEIGHT  240
#define CAM_BUF_SZ  (CAM_WIDTH * CAM_HEIGHT * sizeof(u16))

/* Frame-receive timeout: ~2 s at 30 fps is generous. */
#define RECV_TIMEOUT_NS  2000000000LL

int qr_scan(char *out_buf, size_t out_len) {
    /* --- quirc init -------------------------------------------------- */
    struct quirc *qrc = quirc_new();
    if (!qrc) return QR_ERROR;
    if (quirc_resize(qrc, CAM_WIDTH, CAM_HEIGHT) < 0) {
        quirc_destroy(qrc);
        return QR_ERROR;
    }

    /* --- frame buffer ------------------------------------------------- */
    u16 *cam_buf = (u16 *)calloc(1, CAM_BUF_SZ);
    if (!cam_buf) {
        quirc_destroy(qrc);
        return QR_ERROR;
    }

    /* --- UI: instructions on bottom screen ---------------------------- */
    consoleInit(GFX_BOTTOM, NULL);
    consoleClear();
    printf("\x1b[2;0H");
    printf("=== QR CODE SCANNER ===\n\n");
    printf("Point the outer camera at\n");
    printf("your Pronote QR code.\n\n");
    printf("B: Cancel\n");
    gfxFlushBuffers();
    gfxSwapBuffers();

    /* --- camera init -------------------------------------------------- */
    Result res = camInit();
    if (R_FAILED(res)) {
        free(cam_buf);
        quirc_destroy(qrc);
        consoleInit(GFX_TOP, NULL);
        return QR_ERROR;
    }

    CAMU_SetSize(SELECT_OUT1, SIZE_CTR_TOP_LCD, CONTEXT_A);
    CAMU_SetOutputFormat(SELECT_OUT1, OUTPUT_RGB_565, CONTEXT_A);
    CAMU_SetFrameRate(SELECT_OUT1, FRAME_RATE_30);
    CAMU_SetNoiseFilter(SELECT_OUT1, true);
    CAMU_SetAutoExposure(SELECT_OUT1, true);
    CAMU_SetAutoWhiteBalance(SELECT_OUT1, true);
    CAMU_Activate(SELECT_OUT1);

    u32 transferUnit = 0;
    CAMU_GetMaxBytes(&transferUnit, CAM_WIDTH, CAM_HEIGHT);
    CAMU_SetTrimming(PORT_CAM1, false);
    CAMU_SetTransferBytes(PORT_CAM1, transferUnit, CAM_WIDTH, CAM_HEIGHT);

    /* RESET_STICKY: signal stays set until we clear it, so we never miss
       a frame between CAMU_SetReceiving and svcWaitSynchronization. */
    Handle recv_event = 0;
    svcCreateEvent(&recv_event, RESET_STICKY);

    CAMU_ClearBuffer(PORT_CAM1);
    CAMU_SetReceiving(&recv_event, cam_buf, PORT_CAM1, CAM_BUF_SZ, (s16)transferUnit);
    CAMU_StartCapture(PORT_CAM1);

    /* 6. Capture loop
     *
     * Key discipline: create a fresh RESET_ONESHOT event every iteration.
     * svcWaitSynchronization on a RESET_ONESHOT atomically clears it when it
     * returns, so the transfer is guaranteed complete before we touch cam_buf
     * or call CAMU_SetReceiving again. No svcClearEvent needed.
     *
     * We also flush the CPU cache before arming so the DMA engine sees a clean
     * buffer, then invalidate after the wait so the CPU sees fresh pixel data.
     */
    int result = QR_CANCELLED;

    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_B) break;

        /* Wait for next frame (or timeout) */
        res = svcWaitSynchronization(recv_event, RECV_TIMEOUT_NS);
        if (R_FAILED(res)) continue; /* timeout — try again */
        svcClearEvent(recv_event);   /* re-arm for the next frame */

        /* Flush cache so quirc reads the DMA'd data */
        GSPGPU_InvalidateDataCache(cam_buf, CAM_BUF_SZ);

        /* Re-arm camera for next frame immediately */
        CAMU_SetReceiving(&recv_event, cam_buf, PORT_CAM1, CAM_BUF_SZ, (s16)transferUnit);

        /* Convert RGB565 frame to greyscale for quirc */
        int w = 0, h = 0;
        uint8_t *img = quirc_begin(qrc, &w, &h);
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                u16 px = cam_buf[y * CAM_WIDTH + x];
                img[y * w + x] = (u8)(
                    (((px >> 11) & 0x1F) * 8 +
                     ((px >>  5) & 0x3F) * 4 +
                      (px        & 0x1F) * 8) / 3);
            }
        }
        quirc_end(qrc);

        int n = quirc_count(qrc);
        for (int i = 0; i < n; i++) {
            struct quirc_code code;
            struct quirc_data data;
            quirc_extract(qrc, i, &code);
            if (quirc_decode(&code, &data) == QUIRC_SUCCESS) {
                size_t len = data.payload_len;
                if (len >= out_len) len = out_len - 1;
                memcpy(out_buf, data.payload, len);
                out_buf[len] = '\0';
                result = QR_SUCCESS;
                goto done;
            }
        }
    }

done:
    CAMU_StopCapture(PORT_CAM1);
    CAMU_Activate(SELECT_NONE);
    svcCloseHandle(recv_event);
    free(cam_buf);
    camExit();
    quirc_destroy(qrc);

    /* Restore top screen for login UI */
    consoleInit(GFX_TOP, NULL);
    return result;
}
