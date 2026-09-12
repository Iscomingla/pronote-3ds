/*
 * qr.c -- QR code scanner: 3DS outer camera + quirc.
 *
 * Two hardware requirements on real 3DS:
 *
 * 1. GSP conflict: citro3d holds a GSP session the camera sysmodule cannot
 *    share. ui_suspend() tears down C3D/C2D before camInit(); ui_resume()
 *    brings them back after camExit().
 *
 * 2. Linear heap: the camera DMA engine can only write to physical linear
 *    memory. calloc/malloc give standard heap — unusable for DMA on hardware.
 *    cam_buf must be allocated with linearAlloc() and freed with linearFree().
 */

#include <3ds.h>
#include <stdlib.h>
#include <string.h>
#include "qr.h"
#include "ui.h"
#include "../lib/quirc/quirc.h"

#define CAM_WIDTH        400
#define CAM_HEIGHT       240
#define CAM_BUF_SZ       (CAM_WIDTH * CAM_HEIGHT * sizeof(u16))
#define RECV_TIMEOUT_NS  2000000000LL

static void draw_qr_screen(void) {
    ui_frame_begin();

    ui_clear_target(ui_get_target(GFX_TOP),    COL_BG);
    ui_clear_target(ui_get_target(GFX_BOTTOM), COL_BG);

    ui_target(GFX_TOP);
    ui_rect(0, 0, SCREEN_TOP_W, 36.0f, C2D_Color32(0x00, 0x60, 0x52, 0xFF));
    ui_text_centred(0, SCREEN_TOP_W, 8.0f,   0.65f, COL_WHITE, "notApro");
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

    ui_target(GFX_BOTTOM);
    ui_rect(0, 0, SCREEN_BOT_W, 36.0f, C2D_Color32(0x00, 0x60, 0x52, 0xFF));
    ui_text_centred(0, SCREEN_BOT_W, 8.0f, 0.65f, COL_WHITE, "QR Scanner");
    ui_hline(0, 36.0f, SCREEN_BOT_W, COL_LINE1);
    float cy = 52.0f;
    ui_text_centred(0, SCREEN_BOT_W, cy,   0.50f, COL_WHITE,   "Scanning for QR code...");
    cy += 30.0f;
    ui_text(16.0f, cy, 0.50f, COL_LINE1, "B");
    ui_text(36.0f, cy, 0.50f, COL_WHITE, "Cancel");
    cy += 28.0f;
    ui_text_centred(0, SCREEN_BOT_W, cy, 0.42f, COL_DIMTEXT,
                    "Auto-detected when in frame");
    ui_hline(0, SCREEN_H - 3.0f, SCREEN_BOT_W, COL_LINE2);
    ui_hline(0, SCREEN_H - 1.0f, SCREEN_BOT_W, COL_LINE1);

    ui_frame_end();
}

int qr_scan(char *out_buf, size_t out_len) {
    /* 1. Draw UI while citro3d is still alive */
    draw_qr_screen();

    /* 2. Release GSP before camera init */
    ui_suspend();

    /* 3. quirc */
    struct quirc *qrc = quirc_new();
    if (!qrc) { ui_resume(); return QR_ERROR; }
    if (quirc_resize(qrc, CAM_WIDTH, CAM_HEIGHT) < 0) {
        quirc_destroy(qrc);
        ui_resume();
        return QR_ERROR;
    }

    /* 4. Camera buffer in LINEAR heap — required for DMA on real hardware */
    u16 *cam_buf = (u16 *)linearAlloc(CAM_BUF_SZ);
    if (!cam_buf) {
        quirc_destroy(qrc);
        ui_resume();
        return QR_ERROR;
    }
    memset(cam_buf, 0, CAM_BUF_SZ);

    /* 5. Camera init */
    Result res = camInit();
    if (R_FAILED(res)) {
        linearFree(cam_buf);
        quirc_destroy(qrc);
        ui_resume();
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

    Handle recv_event = 0;
    svcCreateEvent(&recv_event, RESET_STICKY);
    CAMU_ClearBuffer(PORT_CAM1);
    CAMU_SetReceiving(&recv_event, cam_buf, PORT_CAM1, CAM_BUF_SZ, (s16)transferUnit);
    CAMU_StartCapture(PORT_CAM1);

    /* 6. Capture loop */
    int result = QR_CANCELLED;

    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_B) break;

        res = svcWaitSynchronization(recv_event, RECV_TIMEOUT_NS);
        if (R_FAILED(res)) continue;
        svcClearEvent(recv_event);
        GSPGPU_InvalidateDataCache(cam_buf, CAM_BUF_SZ);

        CAMU_SetReceiving(&recv_event, cam_buf, PORT_CAM1, CAM_BUF_SZ, (s16)transferUnit);

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
    linearFree(cam_buf);
    camExit();
    quirc_destroy(qrc);

    /* 7. Bring citro3d back up */
    ui_resume();
    return result;
}
