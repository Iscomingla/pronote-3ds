/*
 * qr.c — QR code scanner: 3DS outer camera + quirc + citro2d UI.
 *
 * Camera display is intentionally omitted here (deferred to a later
 * citro2d/citro3d pass — see TODO: camera preview).
 *
 * Event model: RESET_STICKY, re-armed after every received frame.
 * Matches FBI-NH capturecam and avoids missing frames between
 * CAMU_SetReceiving and svcWaitSynchronization.
 *
 * Step 3: bottom-screen instructions now drawn via citro2d (ui.c)
 * instead of consoleInit/printf.
 */

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qr.h"
#include "ui.h"
#include "../lib/quirc/quirc.h"

#define CAM_WIDTH    400
#define CAM_HEIGHT   240
#define CAM_BUF_SZ   (CAM_WIDTH * CAM_HEIGHT * sizeof(u16))

/* ~2 s receive timeout at 30 fps */
#define RECV_TIMEOUT_NS  2000000000LL

/* -------------------------------------------------------------------------
 * draw_qr_screen — both screens, called once before the capture loop
 *                  and whenever B is held (no per-frame redraw needed).
 * ---------------------------------------------------------------------- */
static void draw_qr_screen(void) {
    ui_frame_begin();

    /* TOP: plain background, title, scanning hint */
    ui_target(GFX_TOP);
    ui_clear(COL_BG);

    /* Header */
    ui_rect(0, 0, SCREEN_TOP_W, 36.0f, C2D_Color32(0x00, 0x60, 0x52, 0xFF));
    ui_text_centred(0, SCREEN_TOP_W, 8.0f, 0.65f, COL_WHITE, "notApro");

    /* Stripes */
    ui_rect(0, 36.0f, SCREEN_TOP_W, 4.0f, COL_LINE1);
    ui_rect(0, 40.0f, SCREEN_TOP_W, 2.0f, COL_LINE2);

    /* Scanning message centred on the dark area */
    ui_text_centred(0, SCREEN_TOP_W, 110.0f, 0.55f, COL_LINE2,
                    "Point outer camera at QR code");
    ui_text_centred(0, SCREEN_TOP_W, 132.0f, 0.50f, COL_DIMTEXT,
                    "Top screen will stay black during scan");
    ui_text_centred(0, SCREEN_TOP_W, 152.0f, 0.50f, COL_DIMTEXT,
                    "(camera preview coming later)");

    /* Bottom accent */
    ui_hline(0, SCREEN_H - 3.0f, SCREEN_TOP_W, COL_LINE2);
    ui_hline(0, SCREEN_H - 1.0f, SCREEN_TOP_W, COL_LINE1);

    /* BOTTOM: instructions */
    ui_target(GFX_BOTTOM);
    ui_clear(COL_BG);

    /* Header */
    ui_rect(0, 0, SCREEN_BOT_W, 36.0f, C2D_Color32(0x00, 0x60, 0x52, 0xFF));
    ui_text_centred(0, SCREEN_BOT_W, 8.0f, 0.65f, COL_WHITE, "QR Scanner");
    ui_hline(0, 36.0f, SCREEN_BOT_W, COL_LINE1);

    float cy = 52.0f;
    float sz = 0.50f;
    float lg = 20.0f;

    ui_text_centred(0, SCREEN_BOT_W, cy, sz, COL_WHITE,
                    "Scanning for QR code...");
    cy += lg * 1.5f;

    ui_text(16.0f,  cy, sz, COL_LINE1,  "B");
    ui_text(36.0f,  cy, sz, COL_WHITE,  "Cancel");
    cy += lg;

    /* Decorative separator */
    ui_hline(16.0f, cy + lg * 0.4f, SCREEN_BOT_W - 32.0f, COL_LINE2);
    cy += lg;

    ui_text_centred(0, SCREEN_BOT_W, cy, 0.42f, COL_DIMTEXT,
                    "Auto-detected when in frame");

    /* Bottom accent */
    ui_hline(0, SCREEN_H - 3.0f, SCREEN_BOT_W, COL_LINE2);
    ui_hline(0, SCREEN_H - 1.0f, SCREEN_BOT_W, COL_LINE1);

    ui_frame_end();
}

/* -------------------------------------------------------------------------
 * qr_scan
 * ---------------------------------------------------------------------- */
int qr_scan(char *out_buf, size_t out_len) {
    /* Tear down citro2d so the camera has full GPU access during capture.
     * main.c will call ui_init() again after we return. */
    ui_exit();
    gfxInitDefault();   /* ensure GFX is still up after ui_exit */

    /* --- quirc -------------------------------------------------------- */
    struct quirc *qrc = quirc_new();
    if (!qrc) {
        ui_init();
        return QR_ERROR;
    }
    if (quirc_resize(qrc, CAM_WIDTH, CAM_HEIGHT) < 0) {
        quirc_destroy(qrc);
        ui_init();
        return QR_ERROR;
    }

    /* --- frame buffer ------------------------------------------------- */
    u16 *cam_buf = (u16 *)calloc(1, CAM_BUF_SZ);
    if (!cam_buf) {
        quirc_destroy(qrc);
        ui_init();
        return QR_ERROR;
    }

    /* --- Re-init citro2d just for the scanner UI ---------------------- */
    ui_init();
    draw_qr_screen();

    /* --- camera ------------------------------------------------------- */
    Result res = camInit();
    if (R_FAILED(res)) {
        free(cam_buf);
        quirc_destroy(qrc);
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

    /* RESET_STICKY: stays signalled until we clear it — never miss a frame */
    Handle recv_event = 0;
    svcCreateEvent(&recv_event, RESET_STICKY);

    CAMU_ClearBuffer(PORT_CAM1);
    CAMU_SetReceiving(&recv_event, cam_buf, PORT_CAM1,
                      CAM_BUF_SZ, (s16)transferUnit);
    CAMU_StartCapture(PORT_CAM1);

    int result = QR_CANCELLED;

    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_B) break;

        /* Wait for the next DMA'd frame */
        res = svcWaitSynchronization(recv_event, RECV_TIMEOUT_NS);
        if (R_FAILED(res)) continue;   /* timeout — retry */
        svcClearEvent(recv_event);     /* re-arm for next frame */

        GSPGPU_InvalidateDataCache(cam_buf, CAM_BUF_SZ);

        /* Re-arm camera immediately so it captures the next frame
           while we're busy processing this one */
        CAMU_SetReceiving(&recv_event, cam_buf, PORT_CAM1,
                          CAM_BUF_SZ, (s16)transferUnit);

        /* RGB565 -> greyscale for quirc */
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
                size_t copy_len = data.payload_len;
                if (copy_len >= out_len) copy_len = out_len - 1;
                memcpy(out_buf, data.payload, copy_len);
                out_buf[copy_len] = '\0';
                result = QR_SUCCESS;
                goto done;
            }
        }

        gspWaitForVBlank();
    }

done:
    CAMU_StopCapture(PORT_CAM1);
    CAMU_Activate(SELECT_NONE);
    svcCloseHandle(recv_event);
    free(cam_buf);
    camExit();
    quirc_destroy(qrc);

    /* Tear down our local citro2d; main.c will re-init */
    ui_exit();

    return result;
}
