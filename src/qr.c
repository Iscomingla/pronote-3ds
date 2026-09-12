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
 * The shared buffer uses standard heap (calloc), not linearAlloc, because
 * the camera thread immediately memcpy's the DMA output (which lands in the
 * DMA-private buffer) into the shared buffer under mutex. Only the private
 * DMA buffer passed to CAMU_SetReceiving needs to be in linear heap.
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
    // Shared between threads; protected by mutex
    u16    *shared_buf;  // CAM_WIDTH * CAM_HEIGHT * sizeof(u16), calloc
    Handle  mutex;
    // Signalled by main thread to stop the camera thread
    Handle  cancel_event;
    // Set true by camera thread when it exits
    volatile bool finished;
    Result        result;
} cam_ctx_t;

// ---------------------------------------------------------------------------
// Camera thread
// ---------------------------------------------------------------------------
static void cam_thread_fn(void *arg) {
    cam_ctx_t *ctx = (cam_ctx_t *)arg;

    Handle events[EV_COUNT] = {0};
    events[EV_CANCEL] = ctx->cancel_event;  // owned by main thread, not closed here

    Result res = 0;

    // Private DMA buffer — must be in linear heap
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

    // Camera configuration — outer camera, full top-screen resolution
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

    // Arm first receive
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

        if (idx == EV_CANCEL) {
            res = 0;
            break;
        }

        if (idx == EV_RECV) {
            // Close the old recv handle before re-arming
            svcCloseHandle(events[EV_RECV]);
            events[EV_RECV] = 0;

            GSPGPU_InvalidateDataCache(dma_buf, CAM_BUF_SZ);

            // Copy into shared buffer under mutex
            svcWaitSynchronization(ctx->mutex, U64_MAX);
            memcpy(ctx->shared_buf, dma_buf, CAM_BUF_SZ);
            GSPGPU_FlushDataCache(ctx->shared_buf, CAM_BUF_SZ);
            svcReleaseMutex(ctx->mutex);

            // Re-arm — safe because the previous recv event already fired
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
    // Drain — wait until camera is no longer busy
    bool busy = false;
    while (R_SUCCEEDED(CAMU_IsBusy(&busy, PORT_CAM1)) && busy)
        svcSleepThread(1000000);
    CAMU_ClearBuffer(PORT_CAM1);

cleanup_cam:
    CAMU_Activate(SELECT_NONE);
    camExit();

cleanup_linear:
    linearFree(dma_buf);

    // Close events owned by this thread
    for (int i = 1; i < EV_COUNT; i++) {   // skip EV_CANCEL (owned by main)
        if (events[i]) svcCloseHandle(events[i]);
    }

    ctx->result   = res;
    ctx->finished = true;
    LOG("cam_thread: exited (0x%08lX)", res);
}

// ---------------------------------------------------------------------------
// qr_scan
// ---------------------------------------------------------------------------
int qr_scan(char *out_buf, size_t out_len) {
    // Allocate context
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

    // quirc context
    struct quirc *qrc = quirc_new();
    if (!qrc || quirc_resize(qrc, CAM_WIDTH, CAM_HEIGHT) < 0) {
        if (qrc) quirc_destroy(qrc);
        svcCloseHandle(ctx->cancel_event);
        svcCloseHandle(ctx->mutex);
        free(ctx->shared_buf); free(ctx);
        return QR_ERROR;
    }

    // Initialise camera texture
    ui_cam_tex_init();

    // Start camera thread (priority 0x1A like FBI, stack 0x10000)
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

        // ------ citro2d frame ------
        ui_frame_begin();

        // Clear both targets
        ui_clear_target(ui_get_target(GFX_TOP),    COL_BLACK);
        ui_clear_target(ui_get_target(GFX_BOTTOM), COL_BG);

        // Upload + draw camera frame on top screen
        svcWaitSynchronization(ctx->mutex, U64_MAX);
        ui_cam_tex_upload(ctx->shared_buf, CAM_WIDTH, CAM_HEIGHT);
        svcReleaseMutex(ctx->mutex);

        ui_target(GFX_TOP);
        ui_cam_tex_draw(0.0f, 0.0f, (float)SCREEN_TOP_W, (float)SCREEN_H);

        // Crosshair
        float cx = SCREEN_TOP_W / 2.0f;
        float cy = SCREEN_H      / 2.0f;
        float arm = 28.0f;
        float gap  = 7.0f;
        ui_rect(cx - arm, cy - 1.0f, (arm - gap) * 2.0f, 2.0f, COL_LINE1);
        ui_rect(cx - 1.0f, cy - arm, 2.0f, (arm - gap) * 2.0f, COL_LINE1);

        // Bottom screen instructions
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
        // ------ end citro2d frame ------

        // quirc decode on the shared buffer (snapshot copy avoids holding mutex during decode)
        u8 grey_buf[CAM_WIDTH * CAM_HEIGHT];
        svcWaitSynchronization(ctx->mutex, U64_MAX);
        const u16 *src = ctx->shared_buf;
        for (int i = 0; i < CAM_WIDTH * CAM_HEIGHT; i++) {
            u16 px = src[i];
            grey_buf[i] = (u8)(
                (((px >> 11) & 0x1F) * 8 +
                 ((px >>  5) & 0x3F) * 4 +
                  (px        & 0x1F) * 8) / 3);
        }
        svcReleaseMutex(ctx->mutex);

        int w = 0, h = 0;
        uint8_t *qimg = quirc_begin(qrc, &w, &h);
        memcpy(qimg, grey_buf, (size_t)w * h);
        quirc_end(qrc);

        int n = quirc_count(qrc);
        if (n > 0) LOG("qr_scan: frame %d — %d code(s) detected", frames, n);

        for (int i = 0; i < n; i++) {
            struct quirc_code code;
            struct quirc_data data;
            quirc_extract(qrc, i, &code);
            if (quirc_decode(&code, &data) == QUIRC_SUCCESS) {
                LOG("qr_scan: decoded on frame %d, len=%d", frames, data.payload_len);
                size_t len = data.payload_len;
                if (len >= out_len) len = out_len - 1;
                memcpy(out_buf, data.payload, len);
                out_buf[len] = '\0';
                result = QR_SUCCESS;
                svcSignalEvent(ctx->cancel_event);
                goto done;
            }
        }

        frames++;
    }

done:
    // Wait for camera thread to finish
    svcSignalEvent(ctx->cancel_event);  // idempotent if already signalled
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
