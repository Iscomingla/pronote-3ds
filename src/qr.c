/*
 * qr.c -- QR code scanner: 3DS outer camera + quirc + citro2d preview.
 *
 * Architecture (modelled on FBI-NH's remoteinstall.c + capturecam.c):
 *
 *   Main thread:  citro2d render loop -- uploads camera frame as GPU texture,
 *                 decodes with quirc, handles B-cancel.
 *
 *   Camera thread: CAMU DMA loop -- captures frames into shared_buf protected
 *                  by a mutex. Increments frame_seq on every new frame so the
 *                  main thread can detect whether the buffer changed.
 *
 * Decode strategy (lessons from four log sessions):
 *
 *   Session 1: DATA_ECC at 2x -- module pitch too small, Otsu threshold fails.
 *   Session 2: Adaptive threshold -> FORMAT_ECC; mutex held during processing
 *              -> DMA buffer error cascade.
 *   Session 3: Mutex fixed, buffer errors gone. Version still v13/v14/v15 on
 *              same physical QR. Unsharp mask added.
 *   Session 4: Version STILL jumps (v13/v14/v15) within 700ms. QR cannot
 *              regenerate that fast -> quirc is genuinely misreading the
 *              version. Root cause: ARM11 @ 268 MHz is too slow to finish
 *              unsharp on 400x240 (864k multiply-adds) AND render within
 *              33 ms. The main loop overruns, decoding the same stale frame
 *              multiple times. Also, memcpy(shared_buf) is not atomic vs the
 *              camera thread DMA copy -> occasional torn frames -> version
 *              misread.
 *
 *   Fix (this version):
 *     1. Frame sequence counter (frame_seq, u32 volatile) -- incremented by
 *        cam thread under mutex after every memcpy. Main thread skips decode
 *        if seq hasn't changed since last decode. Eliminates stale re-decodes.
 *     2. Drop 1x full-res path -- 400x240 unsharp is too expensive and the
 *        1x path never succeeded in any session. Saves ~96 KB static RAM.
 *     3. Keep 2x (200x120) + unsharp. At ~216k ops this is fast enough.
 *     4. Unsharp amount reduced from 1.5 to 1.0 (out = 2*v - blur) to avoid
 *        occasional FORMAT_ECC caused by over-sharpening the format strips.
 *
 * Stack note: large buffers are static.
 *   s_frame_buf -- 192000 bytes (local copy of camera frame, u16)
 *   s_grey2     --  24000 bytes (200x120 greyscale)
 *   s_sharp2    --  24000 bytes (200x120 sharpened)
 *   s_qr_code   --   ~3940 bytes
 *   s_qr_data   --   ~8910 bytes
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

#define W2  (CAM_WIDTH  / 2)   /* 200 */
#define H2  (CAM_HEIGHT / 2)   /* 120 */

#define EV_CANCEL  0
#define EV_RECV    1
#define EV_BUFERR  2
#define EV_COUNT   3

typedef struct {
    u16             *shared_buf;
    Handle           mutex;
    Handle           cancel_event;
    volatile u32     frame_seq;     /* incremented each time shared_buf is updated */
    volatile bool    finished;
    Result           result;
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
        ctx->result = (Result)-1; ctx->finished = true; return;
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
                                          PORT_CAM1, CAM_BUF_SZ, (s16)transfer_unit)))
        goto cleanup_cam;

    CAMU_StartCapture(PORT_CAM1);
    LOG("cam_thread: capture started, transfer_unit=%lu", transfer_unit);

    while (true) {
        s32 idx = 0;
        res = svcWaitSynchronizationN(&idx, events, EV_COUNT, false, U64_MAX);
        if (R_FAILED(res)) break;
        if (idx == EV_CANCEL) { res = 0; break; }

        if (idx == EV_RECV) {
            svcCloseHandle(events[EV_RECV]); events[EV_RECV] = 0;
            GSPGPU_InvalidateDataCache(dma_buf, CAM_BUF_SZ);
            svcWaitSynchronization(ctx->mutex, U64_MAX);
            memcpy(ctx->shared_buf, dma_buf, CAM_BUF_SZ);
            GSPGPU_FlushDataCache(ctx->shared_buf, CAM_BUF_SZ);
            ctx->frame_seq++;   /* signal new frame available */
            svcReleaseMutex(ctx->mutex);
            res = CAMU_SetReceiving(&events[EV_RECV], dma_buf,
                                     PORT_CAM1, CAM_BUF_SZ, (s16)transfer_unit);
            if (R_FAILED(res)) break;
        }

        if (idx == EV_BUFERR) {
            LOG("cam_thread: buffer error, resetting");
            svcCloseHandle(events[EV_RECV]); events[EV_RECV] = 0;
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
    ctx->result = res; ctx->finished = true;
    LOG("cam_thread: exited (0x%08lX)", res);
}

// ---------------------------------------------------------------------------
// Static buffers -- off the stack
// ---------------------------------------------------------------------------
static u16 s_frame_buf[CAM_WIDTH * CAM_HEIGHT];   /* local frame copy */
static u8  s_grey2[W2 * H2];                      /* 200x120 greyscale */
static u8  s_sharp2[W2 * H2];                     /* 200x120 sharpened */

static struct quirc_code s_qr_code;
static struct quirc_data s_qr_data;

// ---------------------------------------------------------------------------
// rgb565_luma: BT.601 luma from one RGB565 pixel
// ---------------------------------------------------------------------------
static inline u8 rgb565_luma(u16 px) {
    u32 r8 = ((px >> 11) & 0x1F) << 3;
    u32 g8 = ((px >>  5) & 0x3F) << 2;
    u32 b8 =  (px        & 0x1F) << 3;
    return (u8)((r8 * 77 + g8 * 150 + b8 * 29) >> 8);
}

// ---------------------------------------------------------------------------
// unsharp_mask: 3x3 box-blur unsharp, amount=1.0 (out = 2*v - blur).
// Amount reduced from 1.5 to avoid over-sharpening QR format strips.
// ---------------------------------------------------------------------------
static void unsharp_mask(const u8 *src, u8 *dst, int w, int h) {
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int sum = 0;
            for (int dy = -1; dy <= 1; dy++) {
                int sy = y + dy < 0 ? 0 : y + dy >= h ? h-1 : y + dy;
                for (int dx = -1; dx <= 1; dx++) {
                    int sx = x + dx < 0 ? 0 : x + dx >= w ? w-1 : x + dx;
                    sum += src[sy * w + sx];
                }
            }
            int v     = src[y * w + x];
            int blur  = sum / 9;
            int sharp = 2 * v - blur;   /* amount=1.0 */
            if (sharp < 0)   sharp = 0;
            if (sharp > 255) sharp = 255;
            dst[y * w + x] = (u8)sharp;
        }
    }
}

// ---------------------------------------------------------------------------
// try_decode: feed buf to qrc, attempt normal + flipped decode
// ---------------------------------------------------------------------------
static bool try_decode(struct quirc *qrc, const u8 *buf, int w, int h,
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
    ctx->finished  = false;
    ctx->frame_seq = 0;

    struct quirc *qrc2 = quirc_new();
    if (!qrc2 || quirc_resize(qrc2, W2, H2) < 0) {
        LOG("qr_scan: quirc init failed");
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
        quirc_destroy(qrc2);
        svcCloseHandle(ctx->cancel_event);
        svcCloseHandle(ctx->mutex);
        free(ctx->shared_buf); free(ctx);
        return QR_ERROR;
    }

    int result       = QR_CANCELLED;
    int frames       = 0;
    u32 last_seq     = 0xFFFFFFFF;   /* sentinel: decode only on new frames */

    while (aptMainLoop() && !ctx->finished) {
        hidScanInput();
        if (hidKeysHeld() & KEY_B) {
            svcSignalEvent(ctx->cancel_event);
            break;
        }

        /* ---- Render ---- */
        ui_frame_begin();
        ui_clear_target(ui_get_target(GFX_TOP),    COL_BLACK);
        ui_clear_target(ui_get_target(GFX_BOTTOM), COL_BG);

        svcWaitSynchronization(ctx->mutex, U64_MAX);
        u32 cur_seq = ctx->frame_seq;
        ui_cam_tex_upload(ctx->shared_buf, CAM_WIDTH, CAM_HEIGHT);
        if (cur_seq != last_seq)
            memcpy(s_frame_buf, ctx->shared_buf, CAM_BUF_SZ);
        svcReleaseMutex(ctx->mutex);

        ui_target(GFX_TOP);
        ui_cam_tex_draw(0.0f, 0.0f, (float)SCREEN_TOP_W, (float)SCREEN_H);

        /* Crosshair */
        float cx = SCREEN_TOP_W / 2.0f, cfy = SCREEN_H / 2.0f;
        float arm = 28.0f, gap = 7.0f;
        ui_rect(cx - arm,  cfy - 1.0f, (arm - gap) * 2.0f, 2.0f, COL_LINE1);
        ui_rect(cx - 1.0f, cfy - arm,  2.0f, (arm - gap) * 2.0f, COL_LINE1);

        ui_target(GFX_BOTTOM);
        ui_rect(0, 0, SCREEN_BOT_W, 36.0f, C2D_Color32(0x00, 0x60, 0x52, 0xFF));
        ui_text_centred(0, SCREEN_BOT_W,  8.0f, 0.65f, COL_WHITE, "QR Scanner");
        ui_hline(0, 36.0f, SCREEN_BOT_W, COL_LINE1);
        float iy = 52.0f;
        ui_text_centred(0, SCREEN_BOT_W, iy,   0.50f, COL_WHITE,   "Scanning for QR code...");
        iy += 30.0f;
        ui_text(16.0f, iy, 0.50f, COL_LINE1, "Hold B");
        ui_text(80.0f, iy, 0.50f, COL_WHITE,  "to cancel");
        iy += 28.0f;
        ui_text_centred(0, SCREEN_BOT_W, iy, 0.42f, COL_DIMTEXT, "Auto-detected when in frame");
        ui_hline(0, SCREEN_H - 3.0f, SCREEN_BOT_W, COL_LINE2);
        ui_hline(0, SCREEN_H - 1.0f, SCREEN_BOT_W, COL_LINE1);

        ui_frame_end();

        /* ---- Decode only on new frames ---- */
        if (cur_seq == last_seq) continue;
        last_seq = cur_seq;

        /* 2x downsample -> greyscale -> unsharp */
        for (int qy = 0; qy < H2; qy++) {
            for (int qx = 0; qx < W2; qx++) {
                u32 sum = 0;
                sum += rgb565_luma(s_frame_buf[(qy*2+0)*CAM_WIDTH + (qx*2+0)]);
                sum += rgb565_luma(s_frame_buf[(qy*2+0)*CAM_WIDTH + (qx*2+1)]);
                sum += rgb565_luma(s_frame_buf[(qy*2+1)*CAM_WIDTH + (qx*2+0)]);
                sum += rgb565_luma(s_frame_buf[(qy*2+1)*CAM_WIDTH + (qx*2+1)]);
                s_grey2[qy * W2 + qx] = (u8)(sum / 4);
            }
        }
        unsharp_mask(s_grey2, s_sharp2, W2, H2);

        if (try_decode(qrc2, s_sharp2, W2, H2, frames, 2)) goto success;

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
    quirc_destroy(qrc2);
    svcCloseHandle(ctx->cancel_event);
    svcCloseHandle(ctx->mutex);
    free(ctx->shared_buf);
    free(ctx);

    LOG("qr_scan: done (result=%d, frames=%d)", result, frames);
    return result;
}
