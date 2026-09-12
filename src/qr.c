/*
 * qr.c -- QR code scanner: 3DS outer camera + quirc + citro2d preview.
 *
 * Architecture (modelled on FBI-NH's remoteinstall.c + capturecam.c):
 *
 *   Main thread:  citro2d render loop -- uploads camera frame as GPU texture
 *                 each frame, decodes with quirc, handles B-cancel.
 *
 *   Camera thread: CAMU DMA loop -- captures frames into a shared u16* buffer
 *                  protected by a mutex. Uses svcWaitSynchronizationN across
 *                  three events (cancel, recv, buffer_error) like FBI does,
 *                  so CAMU_SetReceiving is only re-armed after the recv event
 *                  fires -- never while the previous DMA is in flight.
 *
 * Decode strategy (DATA_ECC fix):
 *
 *   The Pronote QR is version 13-14 (69-73 modules). At 400x240 the module
 *   pitch is only ~3px -- too small for quirc's global Otsu threshold to
 *   reliably binarise the image. Error 4 = QUIRC_ERROR_DATA_ECC means quirc
 *   finds the finder patterns and grid fine, but sampling noise fills the ECC
 *   correction budget before the data is recovered.
 *
 *   Fix -- two layers:
 *
 *   1. Multi-scale: each frame is fed to quirc at three resolutions:
 *        4x4 box-avg  -> 100x60  (~6px/module for v13) -- best for small QR
 *        3x3 box-avg  -> 133x80  (~4.7px/module)
 *        2x2 box-avg  -> 200x120 (~3.5px/module) -- original
 *      quirc is run three times; whichever succeeds first wins.
 *
 *   2. Local adaptive threshold (mean-based, 15x15 window, bias -10):
 *      Applied after box-averaging. Replaces quirc's internal global Otsu,
 *      which fails when the QR only covers part of the frame (background
 *      brightness skews the histogram). We binarise ourselves and hand
 *      quirc a clean black/white image so its thresholder is a no-op.
 *      (quirc thresholds any image handed to it, but if it's already
 *      binarised to 0/255 Otsu will just pick 128 and preserve it.)
 *
 * Stack note: large buffers are declared static to avoid stack overflow.
 *   s_grey_*   -- greyscale buffers for each scale
 *   s_qr_code  -- ~3940 bytes
 *   s_qr_data  -- ~8910 bytes
 */

#include <3ds.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include "qr.h"
#include "ui.h"
#include "log.h"
#include "../lib/quirc/quirc.h"

#define CAM_WIDTH    400
#define CAM_HEIGHT   240
#define CAM_BUF_SZ   (CAM_WIDTH * CAM_HEIGHT * sizeof(u16))

/* Downsample scales tried per frame, largest-to-smallest */
#define SCALE_4  4    /* 100 x  60 */
#define SCALE_3  3    /* 133 x  80 */
#define SCALE_2  2    /* 200 x 120 */

#define W4  (CAM_WIDTH  / SCALE_4)   /* 100 */
#define H4  (CAM_HEIGHT / SCALE_4)   /*  60 */
#define W3  (CAM_WIDTH  / SCALE_3)   /* 133 */
#define H3  (CAM_HEIGHT / SCALE_3)   /*  80 */
#define W2  (CAM_WIDTH  / SCALE_2)   /* 200 */
#define H2  (CAM_HEIGHT / SCALE_2)   /* 120 */

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
// Camera thread (unchanged from before)
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
            LOG("cam_thread: buffer error, resetting");
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
// Static buffers -- keep large structs off the stack
// ---------------------------------------------------------------------------
static u8 s_grey4[W4 * H4];   /* 100x60  -- scale 4x4 */
static u8 s_grey3[W3 * H3];   /* 133x80  -- scale 3x3 */
static u8 s_grey2[W2 * H2];   /* 200x120 -- scale 2x2 */

static struct quirc_code s_qr_code;
static struct quirc_data s_qr_data;

// ---------------------------------------------------------------------------
// box_avg_luma: downsample RGB565 src (srcW x srcH) into dst (dstW x dstH)
// using SxS box averaging + BT.601 luma.
// S = srcW/dstW = srcH/dstH (must be integer).
// ---------------------------------------------------------------------------
static void box_avg_luma(const u16 *src, int srcW, u8 *dst, int dstW, int dstH, int S) {
    int S2 = S * S;
    for (int qy = 0; qy < dstH; qy++) {
        for (int qx = 0; qx < dstW; qx++) {
            u32 sum = 0;
            for (int dy = 0; dy < S; dy++) {
                for (int dx = 0; dx < S; dx++) {
                    u16 px = src[(qy * S + dy) * srcW + (qx * S + dx)];
                    u32 r8 = ((px >> 11) & 0x1F) << 3;
                    u32 g8 = ((px >>  5) & 0x3F) << 2;
                    u32 b8 =  (px        & 0x1F) << 3;
                    sum += (r8 * 77 + g8 * 150 + b8 * 29) >> 8;
                }
            }
            dst[qy * dstW + qx] = (u8)(sum / S2);
        }
    }
}

// ---------------------------------------------------------------------------
// local_adaptive_threshold: mean-based adaptive binarisation in-place.
// window = WxW neighbourhood, bias subtracted from local mean before compare.
// Output: 0 (black) or 255 (white).
// W must be odd. Using W=15, bias=10 works well for camera-captured QRs.
// ---------------------------------------------------------------------------
static void local_adaptive_threshold(u8 *img, int w, int h, int W, int bias) {
    int half = W / 2;
    /* Use a simple sliding-window sum; good enough for small images. */
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int x0 = x - half < 0     ? 0     : x - half;
            int x1 = x + half >= w    ? w - 1 : x + half;
            int y0 = y - half < 0     ? 0     : y - half;
            int y1 = y + half >= h    ? h - 1 : y + half;
            u32 sum = 0, count = 0;
            for (int sy = y0; sy <= y1; sy++) {
                for (int sx = x0; sx <= x1; sx++) {
                    sum += img[sy * w + sx];
                    count++;
                }
            }
            u8 mean = (u8)(sum / count);
            img[y * w + x] = img[y * w + x] < (int)mean - bias ? 0 : 255;
        }
    }
}

// ---------------------------------------------------------------------------
// try_decode: feed buf (w x h) to qrc, try normal + flipped, return true on success
// ---------------------------------------------------------------------------
static bool try_decode(struct quirc *qrc, u8 *buf, int w, int h,
                       int frame, int scale) {
    int qw = 0, qh = 0;
    uint8_t *qimg = quirc_begin(qrc, &qw, &qh);
    memcpy(qimg, buf, (size_t)qw * qh);
    quirc_end(qrc);

    int n = quirc_count(qrc);
    if (n == 0) return false;

    LOG("qr_scan: frame %d scale %dx -- %d code(s)", frame, scale, n);

    for (int i = 0; i < n; i++) {
        quirc_extract(qrc, i, &s_qr_code);
        LOG("qr_scan: frame %d scale %dx code %d size=%d (v%d)",
            frame, scale, i, s_qr_code.size, (s_qr_code.size - 17) / 4);

        quirc_decode_error_t err = quirc_decode(&s_qr_code, &s_qr_data);
        if (err == QUIRC_SUCCESS) {
            LOG("qr_scan: decoded (normal) frame %d scale %dx", frame, scale);
            return true;
        }
        LOG("qr_scan: normal decode err %d (scale %dx)", (int)err, scale);

        quirc_flip(&s_qr_code);
        err = quirc_decode(&s_qr_code, &s_qr_data);
        if (err == QUIRC_SUCCESS) {
            LOG("qr_scan: decoded (flipped) frame %d scale %dx", frame, scale);
            return true;
        }
        LOG("qr_scan: flipped decode err %d (scale %dx)", (int)err, scale);
    }
    return false;
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

    /* Three quirc instances -- one per scale */
    struct quirc *qrc4 = quirc_new();
    struct quirc *qrc3 = quirc_new();
    struct quirc *qrc2 = quirc_new();
    if (!qrc4 || !qrc3 || !qrc2
     || quirc_resize(qrc4, W4, H4) < 0
     || quirc_resize(qrc3, W3, H3) < 0
     || quirc_resize(qrc2, W2, H2) < 0) {
        LOG("qr_scan: quirc init failed");
        if (qrc4) quirc_destroy(qrc4);
        if (qrc3) quirc_destroy(qrc3);
        if (qrc2) quirc_destroy(qrc2);
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
        quirc_destroy(qrc4); quirc_destroy(qrc3); quirc_destroy(qrc2);
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

        /* Render frame */
        ui_frame_begin();
        ui_clear_target(ui_get_target(GFX_TOP),    COL_BLACK);
        ui_clear_target(ui_get_target(GFX_BOTTOM), COL_BG);

        svcWaitSynchronization(ctx->mutex, U64_MAX);
        ui_cam_tex_upload(ctx->shared_buf, CAM_WIDTH, CAM_HEIGHT);
        svcReleaseMutex(ctx->mutex);

        ui_target(GFX_TOP);
        ui_cam_tex_draw(0.0f, 0.0f, (float)SCREEN_TOP_W, (float)SCREEN_H);

        /* Crosshair */
        float cx = SCREEN_TOP_W / 2.0f, cy_f = SCREEN_H / 2.0f;
        float arm = 28.0f, gap = 7.0f;
        ui_rect(cx - arm,  cy_f - 1.0f, (arm - gap) * 2.0f, 2.0f, COL_LINE1);
        ui_rect(cx - 1.0f, cy_f - arm,  2.0f, (arm - gap) * 2.0f, COL_LINE1);

        ui_target(GFX_BOTTOM);
        ui_rect(0, 0, SCREEN_BOT_W, 36.0f, C2D_Color32(0x00, 0x60, 0x52, 0xFF));
        ui_text_centred(0, SCREEN_BOT_W, 8.0f,  0.65f, COL_WHITE, "QR Scanner");
        ui_hline(0, 36.0f, SCREEN_BOT_W, COL_LINE1);
        float iy = 52.0f;
        ui_text_centred(0, SCREEN_BOT_W, iy,  0.50f, COL_WHITE,   "Scanning for QR code...");
        iy += 30.0f;
        ui_text(16.0f, iy, 0.50f, COL_LINE1, "Hold B");
        ui_text(80.0f, iy, 0.50f, COL_WHITE,  "to cancel");
        iy += 28.0f;
        ui_text_centred(0, SCREEN_BOT_W, iy, 0.42f, COL_DIMTEXT, "Auto-detected when in frame");
        ui_hline(0, SCREEN_H - 3.0f, SCREEN_BOT_W, COL_LINE2);
        ui_hline(0, SCREEN_H - 1.0f, SCREEN_BOT_W, COL_LINE1);

        ui_frame_end();

        /*
         * Decode: lock frame buffer once, build all three scales,
         * apply adaptive threshold to each, then try quirc per scale.
         */
        svcWaitSynchronization(ctx->mutex, U64_MAX);
        const u16 *src = ctx->shared_buf;

        box_avg_luma(src, CAM_WIDTH, s_grey4, W4, H4, SCALE_4);
        box_avg_luma(src, CAM_WIDTH, s_grey3, W3, H3, SCALE_3);
        box_avg_luma(src, CAM_WIDTH, s_grey2, W2, H2, SCALE_2);

        svcReleaseMutex(ctx->mutex);

        /* Apply local adaptive threshold to each scale */
        local_adaptive_threshold(s_grey4, W4, H4, 15, 10);
        local_adaptive_threshold(s_grey3, W3, H3, 15, 10);
        local_adaptive_threshold(s_grey2, W2, H2, 15, 10);

        /* Try largest-downscale first (most px/module) */
        if (try_decode(qrc4, s_grey4, W4, H4, frames, SCALE_4)) goto success;
        if (try_decode(qrc3, s_grey3, W3, H3, frames, SCALE_3)) goto success;
        if (try_decode(qrc2, s_grey2, W2, H2, frames, SCALE_2)) goto success;

        frames++;
        continue;

success:
        LOG("qr_scan: decoded on frame %d, len=%d", frames, s_qr_data.payload_len);
        {
            size_t len = (size_t)s_qr_data.payload_len;
            if (len >= out_len) len = out_len - 1;
            memcpy(out_buf, s_qr_data.payload, len);
            out_buf[len] = '\0';
        }
        result = QR_SUCCESS;
        svcSignalEvent(ctx->cancel_event);
        goto done;
    }

done:
    svcSignalEvent(ctx->cancel_event);
    while (!ctx->finished)
        svcSleepThread(1000000);

    threadJoin(cam_thread, U64_MAX);

    ui_cam_tex_free();
    quirc_destroy(qrc4);
    quirc_destroy(qrc3);
    quirc_destroy(qrc2);
    svcCloseHandle(ctx->cancel_event);
    svcCloseHandle(ctx->mutex);
    free(ctx->shared_buf);
    free(ctx);

    LOG("qr_scan: done (result=%d, frames=%d)", result, frames);
    return result;
}
