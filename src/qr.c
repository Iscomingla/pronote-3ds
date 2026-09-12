/*
 * qr.c — QR code scanner: 3DS outer camera + quirc + citro2d UI.
 *
 * citro2d stays initialised for the full lifetime of the app.
 * qr_scan() draws its UI with a normal ui_frame_begin/end before starting
 * the camera, then simply stops issuing citro2d frames during capture.
 * There is no ui_exit/ui_init cycle here — that was causing the camera
 * sysmodule kernel panic by disturbing GSP/GPU state while DMA was live.
 *
 * Event model: RESET_STICKY, re-armed after every received frame.
 */

#include <3ds.h>
#include <stdlib.h>
#include <string.h>
#include "qr.h"
#include "ui.h"
#include "../lib/quirc/quirc.h"

#define CAM_WIDTH         400
#define CAM_HEIGHT        240
#define CAM_BUF_SZ        (CAM_WIDTH * CAM_HEIGHT * sizeof(u16))
#define RECV_TIMEOUT_NS   2000000000LL   /* 2 s — generous at 30 fps */

/* -----------------------------------------------------------------------
 * draw_qr_screen — draws once before starting the camera capture loop.
 * Called with citro2d fully alive; camera not yet started.
 * -------------------------------------------------------------------- */
static void draw_qr_screen(void) {
    ui_frame_begin();

    /* TOP: header + stripes + hint text */
    ui_target(GFX_TOP);
    ui_clear(COL_BG);
    ui_rect(0, 0, SCREEN_TOP_W, 36.0f, C2D_Color32(0x00, 0x60, 0x52, 0xFF));
    ui_text_centred(0, SCREEN_TOP_W, 8.0f,   0.65f, COL_WHITE,   "notApro");
    ui_rect(0, 36.0f, SCREEN_TOP_W, 4.0f, COL_LINE1);
    ui_rect(0, 40.0f, SCREEN_TOP_W, 2.0f, COL_LINE2);
    ui_text_centred(0, SCREEN_TOP_W, 110.0f, 0.55f, COL_LINE2,
                    "Point outer camera at QR code");
    ui_text_centred(0, SCREEN_TOP_W, 132.0f, 0.50f, COL_DIMTEXT,
                    "Screen stays dark during scan");
    ui_text_centred(0, SCREEN_TOP_W, 152.0f, 0.50f, COL_DIMTEXT,
                    "(camera preview coming later)");
    ui_hline(0, SCREEN_H - 3.0f, SCREEN_TOP_W, COL_LINE2);
    ui_hline(0, SCREEN_H - 1.0f, SCREEN_TOP_W, COL_LINE1);

    /* BOTTOM: instructions */
    ui_target(GFX_BOTTOM);
    ui_clear(COL_BG);
    ui_rect(0, 0, SCREEN_BOT_W, 36.0f, C2D_Color32(0x00, 0x60, 0x52, 0xFF));
    ui_text_centred(0, SCREEN_BOT_W, 8.0f, 0.65f, COL_WHITE, "QR Scanner");
    ui_hline(0, 36.0f, SCREEN_BOT_W, COL_LINE1);

    float cy = 52.0f;
    ui_text_centred(0, SCREEN_BOT_W, cy,        0.50f, COL_WHITE,   "Scanning for QR code...");
    cy += 30.0f;
    ui_text(16.0f, cy, 0.50f, COL_LINE1, "B");
    ui_text(36.0f, cy, 0.50f, COL_WHITE, "Cancel");
    cy += 20.0f;
    ui_hline(16.0f, cy + 8.0f, SCREEN_BOT_W - 32.0f, COL_LINE2);
    cy += 20.0f;
    ui_text_centred(0, SCREEN_BOT_W, cy, 0.42f, COL_DIMTEXT,
                    "Auto-detected when in frame");
    ui_hline(0, SCREEN_H - 3.0f, SCREEN_BOT_W, COL_LINE2);
    ui_hline(0, SCREEN_H - 1.0f, SCREEN_BOT_W, COL_LINE1);

    ui_frame_end();
    /* citro2d is now idle — no more frames until camera is done */
}

/* -----------------------------------------------------------------------
 * qr_scan
 * -------------------------------------------------------------------- */
int qr_scan(char *out_buf, size_t out_len) {
    /* Draw the scanner UI first, while citro2d is still the only GPU user */
    draw_qr_screen();

    /* --- quirc -------------------------------------------------------- */
    struct quirc *qrc = quirc_new();
    if (!qrc) return QR_ERROR;
    if (quirc_resize(qrc, CAM_WIDTH, CAM_HEIGHT) < 0) {
        quirc_destroy(qrc);
        return QR_ERROR;
    }

    /* --- camera frame buffer ------------------------------------------ */
    u16 *cam_buf = (u16 *)calloc(1, CAM_BUF_SZ);
    if (!cam_buf) {
        quirc_destroy(qrc);
        return QR_ERROR;
    }

    /* --- camera init -------------------------------------------------- */
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

    /* RESET_STICKY: stays signalled until cleared — never miss a frame */
    Handle recv_event = 0;
    svcCreateEvent(&recv_event, RESET_STICKY);
    CAMU_ClearBuffer(PORT_CAM1);
    CAMU_SetReceiving(&recv_event, cam_buf, PORT_CAM1,
                      CAM_BUF_SZ, (s16)transferUnit);
    CAMU_StartCapture(PORT_CAM1);

    /* --- capture loop ------------------------------------------------- */
    int result = QR_CANCELLED;

    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_B) break;

        res = svcWaitSynchronization(recv_event, RECV_TIMEOUT_NS);
        if (R_FAILED(res)) continue;        /* timeout — retry */
        svcClearEvent(recv_event);          /* re-arm */
        GSPGPU_InvalidateDataCache(cam_buf, CAM_BUF_SZ);

        /* Re-arm camera for the next frame immediately */
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
                size_t len = data.payload_len;
                if (len >= out_len) len = out_len - 1;
                memcpy(out_buf, data.payload, len);
                out_buf[len] = '\0';
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
    /* citro2d is still alive — main.c does NOT need to reinit it */
    return result;
}
