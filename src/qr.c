/*
 * qr.c -- QR code scanner: 3DS outer camera + quirc + citro2d preview.
 *
 * Architecture (modelled on FBI-NH's remoteinstall.c + capturecam.c):
 *
 *   Main thread:  citro2d render loop — uploads camera frame as GPU texture
 *                 each frame, decodes with quirc, handles B-cancel.
 *
 *   Camera thread: CAMU DMA loop — captures frames into a shared u16* buffer
 *                  protected by a mutex. Uses svcWaitSynchronizationN across
 *                  three events (cancel, recv, buffer_error) like FBI does,
 *                  so CAMU_SetReceiving is only re-armed after the recv event
 *                  fires — never while the previous DMA is in flight.
 *
 * Because citro3d and the camera sysmodule run in separate threads sharing
 * different hardware resources (GPU vs camera DMA), there is no GSP conflict
 * and no need for ui_suspend/ui_resume.
 *
 * Buffer layout note:
 *   SIZE_CTR_TOP_LCD with OUTPUT_RGB_565 gives a 400×240 buffer, BUT the
 *   camera hardware outputs it column-major (rotated 90° CCW relative to the
 *   physical scene). Pixel at screen position (x, y) is stored at:
 *     buf[x * CAM_HEIGHT + (CAM_HEIGHT - 1 - y)]
 *   We un-rotate into s_grey_buf before handing to quirc so that the QR code
 *   is axis-aligned. Without this, quirc finds the three finder squares but
 *   the perspective matrix is wrong and quirc_decode always returns an error.
 *
 * Stack note: the following are declared static to avoid stack overflow:
 *   s_grey_buf   — 96000 bytes (400*240 grayscale)
 *   s_qr_code    — ~3940 bytes (quirc_code with cell_bitmap[3929])
 *   s_qr_data    — ~8910 bytes (quirc_data with payload[8896])
 * All three are only ever accessed from the main-thread side of qr_scan.
 */

#include <3ds.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include "qr.h"
#include "ui.h"
#include "log.h"
#include "../lib/quirc/quirc.h"

#define CAM_WIDTH   400
#define CAM_HEIGHT  240
#define CAM_BUF_SZ  (CAM_WIDTH * CAM_HEIGHT * sizeof(u16))

// Events passed to svcWaitSynchronizationN
#define EV_CANCEL  0
#define EV_RECV    1
#define EV_BUFERR  2
#define EV_COUNT   3

typedef struct {
    u16    *shared_buf;
    Handle  mutex;
    Handle  cancel_event;
    volatile bool finished;
    Result        result;
} cam_ctx_t;

// ---------------------------------------------------------------------------
// Camera thread
// ---------------------------------------------------------------------------
static void cam_thread_fn(void *arg) {
    cam_ctx_t *ctx = (cam_ctx_t *)arg;

    Handle events[EV_COUNT] = {0};
    events[EV_CANCEL] = ctx->cancel_event;

    Result res = 0;

    u16 *dma_buf = (u16 *)linearAlloc(CAM_BUF_SZ);
    if (!dma_buf) {
        LOG("cam_thread: linearAlloc failed");
        ctx->result   = (Result)-1;
        ctx->finished = true;
        return;
    }

    if (R_FAILED(res = camInit())) {
        LOG("cam_thread: camInit failed 0x%08lX", res);
        goto cleanup_linear;
    }

    CAMU_SetSize(SELECT_OUT1, SIZE_CTR_TOP_LCD, CONTEXT_A);
    CAMU_SetOutputFormat(SELECT_OUT1, OUTPUT_RGB_565, CONTEXT_A);
    CAMU_SetFrameRate(SELECT_OUT1, FRAME_RATE_30);
    CAMU_SetNoiseFilter(SELECT_OUT1, true);
    CAMU_SetAutoExposure(SELECT_OUT1, true);
    CAMU_SetAutoWhiteBalance(SELECT_OUT1, true);
    CAMU_Activate(SELECT_OUT1);

    u32 transfer_unit = 0;
    if (R_FAILED(res = CAMU_GetBufferErrorInterruptEvent(&events[EV_BUFERR], PORT_CAM1)))
        goto cleanup_cam;
    CAMU_SetTrimming(PORT_CAM1, false);
    CAMU_GetMaxBytes(&transfer_unit, CAM_WIDTH, CAM_HEIGHT);
    CAMU_SetTransferBytes(PORT_CAM1, transfer_unit, CAM_WIDTH, CAM_HEIGHT);
    CAMU_ClearBuffer(PORT_CAM1);

    if (R_FAILED(res = CAMU_SetReceiving(&events[EV_RECV], dma_buf,
                                          PORT_CAM1, CAM_BUF_SZ,
                                          (s16)transfer_unit)))
        goto cleanup_cam;

    CAMU_StartCapture(PORT_CAM1);
    LOG("cam_thread: capture started, transfer_unit=%lu", transfer_unit);

    while (true) {
        s32 idx = 0;
        res = svcWaitSynchronizationN(&idx, events, EV_COUNT, false, U64_MAX);
        if (R_FAILED(res)) break;

        if (idx == EV_CANCEL) { res = 0; break; }

        if (idx == EV_RECV) {
            svcCloseHandle(events[EV_RECV]);
            events[EV_RECV] = 0;

            GSPGPU_InvalidateDataCache(dma_buf, CAM_BUF_SZ);

            svcWaitSynchronization(ctx->mutex, U64_MAX);
            memcpy(ctx->shared_buf, dma_buf, CAM_BUF_SZ);
            GSPGPU_FlushDataCache(ctx->shared_buf, CAM_BUF_SZ);
            svcReleaseMutex(ctx->mutex);

            res = CAMU_SetReceiving(&events[EV_RECV], dma_buf,
                                     PORT_CAM1, CAM_BUF_SZ, (s16)transfer_unit);
            if (R_FAILED(res)) break;
        }

        if (idx == EV_BUFERR) {
            LOG("cam_thread: buffer error — resetting");
            svcCloseHandle(events[EV_RECV]);
            events[EV_RECV] = 0;
            if (R_FAILED(res = CAMU_ClearBuffer(PORT_CAM1))) break;
            if (R_FAILED(res = CAMU_SetReceiving(&events[EV_RECV], dma_buf,
                                                  PORT_CAM1, CAM_BUF_SZ,
                                                  (s16)transfer_unit))) break;
            if (R_FAILED(res = CAMU_StartCapture(PORT_CAM1))) break;
        }
    }

    CAMU_StopCapture(PORT_CAM1);
    bool busy = false;
    while (R_SUCCEEDED(CAMU_IsBusy(&busy, PORT_CAM1)) && busy)
        svcSleepThread(1000000);
    CAMU_ClearBuffer(PORT_CAM1);

cleanup_cam:
    CAMU_Activate(SELECT_NONE);
    camExit();

cleanup_linear:
    linearFree(dma_buf);
    for (int i = 1; i < EV_COUNT; i++)
        if (events[i]) svcCloseHandle(events[i]);

    ctx->result   = res;
    ctx->finished = true;
    LOG("cam_thread: exited (0x%08lX)", res);
}

// ---------------------------------------------------------------------------
// Static buffers — must not be on the stack (sizes below):
//   s_grey_buf : 96 000 bytes
//   s_qr_code  :  ~3 940 bytes
//   s_qr_data  :  ~8 910 bytes
// ---------------------------------------------------------------------------
static u8                s_grey_buf[CAM_WIDTH * CAM_HEIGHT];
static struct quirc_code s_qr_code;
static struct quirc_data s_qr_data;

// ---------------------------------------------------------------------------
// rgb565_to_grey_unrotate
//
// The 3DS camera outputs SIZE_CTR_TOP_LCD frames column-major (90° CCW).
// Physical scene pixel (sx, sy) is stored at src[sx * CAM_HEIGHT + (CAM_HEIGHT-1-sy)].
// We want dst[sy * CAM_WIDTH + sx] = grey(src[sx * CAM_HEIGHT + (CAM_HEIGHT-1-sy)]).
// Result: a normal row-major 400×240 greyscale image suitable for quirc.
// ---------------------------------------------------------------------------
static void rgb565_to_grey_unrotate(const u16 *src, u8 *dst) {
    for (int sx = 0; sx < CAM_WIDTH; sx++) {
        for (int sy = 0; sy < CAM_HEIGHT; sy++) {
            u16 px = src[sx * CAM_HEIGHT + (CAM_HEIGHT - 1 - sy)];
            u8  r  = (px >> 11) & 0x1F;
            u8  g  = (px >>  5) & 0x3F;
            u8  b  =  px        & 0x1F;
            dst[sy * CAM_WIDTH + sx] = (u8)((r * 8 + g * 4 + b * 8) / 3);
        }
    }
}

// ---------------------------------------------------------------------------
// qr_scan
// ---------------------------------------------------------------------------
int qr_scan(char *out_buf, size_t out_len) {
    cam_ctx_t *ctx = (cam_ctx_t *)calloc(1, sizeof(cam_ctx_t));
    if (!ctx) return QR_ERROR;

    ctx->shared_buf = (u16 *)calloc(1, CAM_BUF_SZ);
    if (!ctx->shared_buf) { free(ctx); return QR_ERROR; }

    if (R_FAILED(svcCreateMutex(&ctx->mutex, false))) {
        free(ctx->shared_buf); free(ctx); return QR_ERROR;
    }
    if (R_FAILED(svcCreateEvent(&ctx->cancel_event, RESET_STICKY))) {
        svcCloseHandle(ctx->mutex);
        free(ctx->shared_buf); free(ctx); return QR_ERROR;
    }
    ctx->finished = false;

    struct quirc *qrc = quirc_new();
    if (!qrc || quirc_resize(qrc, CAM_WIDTH, CAM_HEIGHT) < 0) {
        if (qrc) quirc_destroy(qrc);
        svcCloseHandle(ctx->cancel_event);
        svcCloseHandle(ctx->mutex);
        free(ctx->shared_buf); free(ctx);
        return QR_ERROR;
    }

    ui_cam_tex_init();

    Thread cam_thread = threadCreate(cam_thread_fn, ctx, 0x10000, 0x1A, 0, true);
    if (!cam_thread) {
        LOG("qr_scan: threadCreate failed");
        ui_cam_tex_free();
        quirc_destroy(qrc);
        svcCloseHandle(ctx->cancel_event);
        svcCloseHandle(ctx->mutex);
        free(ctx->shared_buf); free(ctx);
        return QR_ERROR;
    }

    int result = QR_CANCELLED;
    int frames  = 0;

    while (aptMainLoop() && !ctx->finished) {
        hidScanInput();
        if (hidKeysHeld() & KEY_B) {
            svcSignalEvent(ctx->cancel_event);
            break;
        }

        ui_frame_begin();
        ui_clear_target(ui_get_target(GFX_TOP),    COL_BLACK);
        ui_clear_target(ui_get_target(GFX_BOTTOM), COL_BG);

        svcWaitSynchronization(ctx->mutex, U64_MAX);
        ui_cam_tex_upload(ctx->shared_buf, CAM_WIDTH, CAM_HEIGHT);
        svcReleaseMutex(ctx->mutex);

        ui_target(GFX_TOP);
        ui_cam_tex_draw(0.0f, 0.0f, (float)SCREEN_TOP_W, (float)SCREEN_H);

        // Crosshair overlay
        float cx  = SCREEN_TOP_W / 2.0f;
        float cy  = SCREEN_H      / 2.0f;
        float arm = 28.0f, gap = 7.0f;
        ui_rect(cx - arm, cy - 1.0f, (arm - gap) * 2.0f, 2.0f, COL_LINE1);
        ui_rect(cx - 1.0f, cy - arm, 2.0f, (arm - gap) * 2.0f, COL_LINE1);

        ui_target(GFX_BOTTOM);
        ui_rect(0, 0, SCREEN_BOT_W, 36.0f, C2D_Color32(0x00, 0x60, 0x52, 0xFF));
        ui_text_centred(0, SCREEN_BOT_W, 8.0f, 0.65f, COL_WHITE, "QR Scanner");
        ui_hline(0, 36.0f, SCREEN_BOT_W, COL_LINE1);
        float iy = 52.0f;
        ui_text_centred(0, SCREEN_BOT_W, iy,   0.50f, COL_WHITE,   "Scanning for QR code...");
        iy += 30.0f;
        ui_text(16.0f, iy, 0.50f, COL_LINE1, "Hold B");
        ui_text(72.0f, iy, 0.50f, COL_WHITE,  "to cancel");
        iy += 28.0f;
        ui_text_centred(0, SCREEN_BOT_W, iy, 0.42f, COL_DIMTEXT, "Auto-detected when in frame");
        ui_hline(0, SCREEN_H - 3.0f, SCREEN_BOT_W, COL_LINE2);
        ui_hline(0, SCREEN_H - 1.0f, SCREEN_BOT_W, COL_LINE1);

        ui_frame_end();

        // Un-rotate camera buffer and feed to quirc
        svcWaitSynchronization(ctx->mutex, U64_MAX);
        rgb565_to_grey_unrotate(ctx->shared_buf, s_grey_buf);
        svcReleaseMutex(ctx->mutex);

        int w = 0, h = 0;
        uint8_t *qimg = quirc_begin(qrc, &w, &h);
        memcpy(qimg, s_grey_buf, (size_t)w * h);
        quirc_end(qrc);

        int n = quirc_count(qrc);
        if (n > 0) LOG("qr_scan: frame %d — %d code(s) detected", frames, n);

        for (int i = 0; i < n; i++) {
            quirc_extract(qrc, i, &s_qr_code);
            quirc_decode_error_t err = quirc_decode(&s_qr_code, &s_qr_data);
            if (err == QUIRC_SUCCESS) {
                LOG("qr_scan: decoded on frame %d, len=%d", frames, s_qr_data.payload_len);
                size_t len = s_qr_data.payload_len;
                if (len >= out_len) len = out_len - 1;
                memcpy(out_buf, s_qr_data.payload, len);
                out_buf[len] = '\0';
                result = QR_SUCCESS;
                svcSignalEvent(ctx->cancel_event);
                goto done;
            } else {
                LOG("qr_scan: frame %d code %d decode error: %d", frames, i, (int)err);
            }
        }

        frames++;
    }

done:
    svcSignalEvent(ctx->cancel_event);
    while (!ctx->finished)
        svcSleepThread(1000000);

    if (cam_thread) threadJoin(cam_thread, U64_MAX);

    ui_cam_tex_free();
    quirc_destroy(qrc);
    svcCloseHandle(ctx->cancel_event);
    svcCloseHandle(ctx->mutex);
    free(ctx->shared_buf);
    free(ctx);

    LOG("qr_scan: done (result=%d, frames=%d)", result, frames);
    return result;
}
