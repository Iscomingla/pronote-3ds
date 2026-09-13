/*
 * qr.c -- QR code scanner: 3DS outer camera + quirc + citro2d preview.
 *
 * Architecture (modelled on FBI-NH's remoteinstall.c + capturecam.c):
 *
 *   Main thread:  citro2d render loop -- uploads camera frame as GPU texture,
 *                 decodes with quirc, handles B-cancel.
 *
 *   Camera thread: CAMU DMA loop -- captures frames into shared_buf
 *                  (linearAlloc) protected by a mutex + frame_seq counter.
 *
 * Decode history:
 *
 *   Session 1: DATA_ECC at 2x.
 *   Session 2: Mutex held during processing -> DMA cascade.
 *   Session 3: Version v13/v14/v15 jumps -> unsharp added.
 *   Session 4: shared_buf calloc -> stale CPU cache -> torn frames. Fixed
 *              with linearAlloc + __dsb() + InvalidateDataCache.
 *   Session 5: DATA_ECC persists. Unsharp creates module-edge halos.
 *              Dropped unsharp, added 1x full-res path.
 *   Session 6: Version rock-solid v13. DATA_ECC every frame.
 *              Added corner logging to rule out unstable perspective.
 *   Session 7: Corners stable. DATA_ECC root cause identified:
 *              CAMU outputs RGB565 big-endian; ARM reads u16 little-endian
 *              -> bytes swapped -> wrong channel extraction -> garbage luma.
 *              Fix: __builtin_bswap16 in rgb565_luma().
 *              Also added Otsu binarisation -- WRONG, broke detection.
 *   Session 8: QR no longer detected at all with Otsu.
 *              Root cause: quirc's region-growing in identify.c needs
 *              continuous greyscale for its own internal adaptive threshold.
 *              Pre-binarising to {0,255} defeats finder pattern detection.
 *              Fix: remove Otsu, keep only the byteswap.
 *
 * Static buffer layout (BSS, not stack):
 *   s_frame_buf  -- 192000 B  u16[400*240]
 *   s_grey1      --  96000 B  u8[400*240]   1x greyscale
 *   s_grey2      --  24000 B  u8[200*120]   2x greyscale (2x2 average)
 *   s_qr_code    --   ~3940 B
 *   s_qr_data    --   ~8910 B
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

#define W2  (CAM_WIDTH  / 2)
#define H2  (CAM_HEIGHT / 2)

#define EV_CANCEL  0
#define EV_RECV    1
#define EV_BUFERR  2
#define EV_COUNT   3

typedef struct {
    u16             *shared_buf;
    Handle           mutex;
    Handle           cancel_event;
    volatile u32     frame_seq;
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
            __dsb();
            GSPGPU_FlushDataCache(ctx->shared_buf, CAM_BUF_SZ);
            ctx->frame_seq++;
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
// Static buffers (BSS — never on the stack)
// ---------------------------------------------------------------------------
static u16 s_frame_buf[CAM_WIDTH * CAM_HEIGHT];
static u8  s_grey1[CAM_WIDTH * CAM_HEIGHT];
static u8  s_grey2[W2 * H2];

static struct quirc_code s_qr_code;
static struct quirc_data s_qr_data;

// ---------------------------------------------------------------------------
// rgb565_luma: byteswap then luminance.
//
// CAMU outputs RGB565 big-endian (high byte first in DMA memory).
// ARM reads u16 little-endian, so the bytes are swapped on arrival.
//
// Raw u16 as read by ARM (little-endian read of big-endian bytes):
//   bits[15:8] = second DMA byte = G[2:0] B[4:0]
//   bits[ 7:0] = first  DMA byte = R[4:0] G[5:3]
//
// After __builtin_bswap16:
//   bits[15:11] = R[4:0]
//   bits[10: 5] = G[5:0]
//   bits[ 4: 0] = B[4:0]
//
// Note: do NOT pre-binarise before feeding to quirc. quirc's region-growing
// (identify.c) needs a continuous greyscale for its internal adaptive
// threshold. Pre-binarising to {0,255} breaks finder pattern detection.
// ---------------------------------------------------------------------------
static inline u8 rgb565_luma(u16 raw) {
    u16 px = __builtin_bswap16(raw);
    u32 r8 = ((px >> 11) & 0x1F) << 3;
    u32 g8 = ((px >>  5) & 0x3F) << 2;
    u32 b8 =  (px        & 0x1F) << 3;
    return (u8)((r8 * 77 + g8 * 150 + b8 * 29) >> 8);
}

// ---------------------------------------------------------------------------
// try_decode: feed greyscale buffer to quirc and attempt decode + flip
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

        LOG("qr_scan: frame %d scale %dx code %d size=%d (v%d) "
            "TL(%d,%d) TR(%d,%d) BR(%d,%d) BL(%d,%d)",
            frame, scale, i,
            s_qr_code.size, (s_qr_code.size - 17) / 4,
            s_qr_code.corners[0].x, s_qr_code.corners[0].y,
            s_qr_code.corners[1].x, s_qr_code.corners[1].y,
            s_qr_code.corners[2].x, s_qr_code.corners[2].y,
            s_qr_code.corners[3].x, s_qr_code.corners[3].y);

        quirc_decode_error_t err = quirc_decode(&s_qr_code, &s_qr_data);
        if (err == QUIRC_SUCCESS) {
            LOG("qr_scan: SUCCESS frame %d scale %dx v%d ecc=%d mask=%d dtype=%d len=%d",
                frame, scale, s_qr_data.version, s_qr_data.ecc_level,
                s_qr_data.mask, s_qr_data.data_type, s_qr_data.payload_len);
            return true;
        }
        LOG("qr_scan: normal err '%s' (scale %dx)", quirc_strerror(err), scale);

        quirc_flip(&s_qr_code);
        err = quirc_decode(&s_qr_code, &s_qr_data);
        if (err == QUIRC_SUCCESS) {
            LOG("qr_scan: SUCCESS (flipped) frame %d scale %dx v%d ecc=%d mask=%d len=%d",
                frame, scale, s_qr_data.version, s_qr_data.ecc_level,
                s_qr_data.mask, s_qr_data.payload_len);
            return true;
        }
        LOG("qr_scan: flipped err '%s' (scale %dx)", quirc_strerror(err), scale);
    }
    return false;
}

// ---------------------------------------------------------------------------
// qr_scan
// ---------------------------------------------------------------------------
int qr_scan(char *out_buf, size_t out_len) {
    cam_ctx_t *ctx = (cam_ctx_t *)calloc(1, sizeof(cam_ctx_t));
    if (!ctx) return QR_ERROR;

    ctx->shared_buf = (u16 *)linearAlloc(CAM_BUF_SZ);
    if (!ctx->shared_buf) { free(ctx); return QR_ERROR; }
    memset(ctx->shared_buf, 0, CAM_BUF_SZ);

    if (R_FAILED(svcCreateMutex(&ctx->mutex, false))) {
        linearFree(ctx->shared_buf); free(ctx); return QR_ERROR;
    }
    if (R_FAILED(svcCreateEvent(&ctx->cancel_event, RESET_STICKY))) {
        svcCloseHandle(ctx->mutex);
        linearFree(ctx->shared_buf); free(ctx); return QR_ERROR;
    }
    ctx->finished  = false;
    ctx->frame_seq = 0;

    struct quirc *qrc1 = quirc_new();
    if (!qrc1 || quirc_resize(qrc1, CAM_WIDTH, CAM_HEIGHT) < 0) {
        if (qrc1) quirc_destroy(qrc1);
        svcCloseHandle(ctx->cancel_event); svcCloseHandle(ctx->mutex);
        linearFree(ctx->shared_buf); free(ctx);
        return QR_ERROR;
    }

    struct quirc *qrc2 = quirc_new();
    if (!qrc2 || quirc_resize(qrc2, W2, H2) < 0) {
        if (qrc2) quirc_destroy(qrc2);
        quirc_destroy(qrc1);
        svcCloseHandle(ctx->cancel_event); svcCloseHandle(ctx->mutex);
        linearFree(ctx->shared_buf); free(ctx);
        return QR_ERROR;
    }

    ui_cam_tex_init();

    Thread cam_thread = threadCreate(cam_thread_fn, ctx, 0x10000, 0x1A, 0, true);
    if (!cam_thread) {
        LOG("qr_scan: threadCreate failed");
        ui_cam_tex_free();
        quirc_destroy(qrc2); quirc_destroy(qrc1);
        svcCloseHandle(ctx->cancel_event); svcCloseHandle(ctx->mutex);
        linearFree(ctx->shared_buf); free(ctx);
        return QR_ERROR;
    }

    int result   = QR_CANCELLED;
    int frames   = 0;
    u32 last_seq = 0xFFFFFFFF;

    while (aptMainLoop() && !ctx->finished) {
        hidScanInput();
        if (hidKeysHeld() & KEY_B) {
            svcSignalEvent(ctx->cancel_event);
            break;
        }

        // ------ citro2d frame ------
        ui_frame_begin();
        ui_clear_target(ui_get_target(GFX_TOP),    COL_BLACK);
        ui_clear_target(ui_get_target(GFX_BOTTOM), COL_BG);

        svcWaitSynchronization(ctx->mutex, U64_MAX);
        u32 cur_seq = ctx->frame_seq;
        GSPGPU_InvalidateDataCache(ctx->shared_buf, CAM_BUF_SZ);
        ui_cam_tex_upload(ctx->shared_buf, CAM_WIDTH, CAM_HEIGHT);
        if (cur_seq != last_seq)
            memcpy(s_frame_buf, ctx->shared_buf, CAM_BUF_SZ);
        svcReleaseMutex(ctx->mutex);

        ui_target(GFX_TOP);
        ui_cam_tex_draw(0.0f, 0.0f, (float)SCREEN_TOP_W, (float)SCREEN_H);

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
        // ------ end frame ------

        if (cur_seq == last_seq) continue;
        last_seq = cur_seq;

        // 1x full-res greyscale
        for (int i = 0; i < CAM_WIDTH * CAM_HEIGHT; i++)
            s_grey1[i] = rgb565_luma(s_frame_buf[i]);

        if (try_decode(qrc1, s_grey1, CAM_WIDTH, CAM_HEIGHT, frames, 1))
            goto success;

        // 2x downsampled fallback (2x2 box average)
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

        if (try_decode(qrc2, s_grey2, W2, H2, frames, 2))
            goto success;

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
    quirc_destroy(qrc2); quirc_destroy(qrc1);
    svcCloseHandle(ctx->cancel_event); svcCloseHandle(ctx->mutex);
    linearFree(ctx->shared_buf); free(ctx);

    LOG("qr_scan: done (result=%d, frames=%d)", result, frames);
    return result;
}
