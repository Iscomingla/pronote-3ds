#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qr.h"
#include <quirc.h>

// QR scanner using 3DS camera and quirc library
// Based on FBI-NH's QR scanning implementation

#define CAM_WIDTH  400
#define CAM_HEIGHT 240
#define CAM_SIZE   (CAM_WIDTH * CAM_HEIGHT * 2)  // YUV422

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

    // Init camera
    camInit();
    CAMU_SetSize(SELECT_OUT1, SIZE_CTR_TOP_LCD, CONTEXT_A);
    CAMU_SetOutputFormat(SELECT_OUT1, OUTPUT_YUV_422, CONTEXT_A);
    CAMU_SetFrameRate(SELECT_OUT1, FRAME_RATE_30);
    CAMU_SetNoiseFilter(SELECT_OUT1, true);
    CAMU_SetAutoExposure(SELECT_OUT1, true);
    CAMU_SetAutoWhiteBalance(SELECT_OUT1, true);
    CAMU_Activate(SELECT_OUT1);

    Handle cam_event = 0;
    u8 *cam_buf = (u8*)malloc(CAM_SIZE);
    if (!cam_buf) {
        camExit();
        destroy_qr_scanner();
        return QR_ERROR;
    }

    CAMU_ClearBuffer(PORT_CAM1);
    CAMU_StartCapture(PORT_CAM1);
    CAMU_SetTransferBytes(PORT_CAM1, CAM_SIZE, CAM_WIDTH, CAM_HEIGHT);

    int result = QR_CANCELLED;

    while (aptMainLoop()) {
        hidScanInput();
        u32 kdown = hidKeysDown();

        if (kdown & KEY_B) {
            result = QR_CANCELLED;
            break;
        }

        // Capture frame
        svcCreateEvent(&cam_event, RESET_ONESHOT);
        CAMU_SetReceiving(&cam_event, cam_buf, PORT_CAM1, CAM_SIZE, (s16)CAM_WIDTH);
        svcWaitSynchronization(cam_event, 400000000LL);
        svcCloseHandle(cam_event);
        cam_event = 0;

        // Feed to quirc (convert YUV to grayscale)
        int w, h;
        uint8_t *img = quirc_begin(qr_ctx, &w, &h);
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                // YUV422: Y is every 2nd byte
                img[y * w + x] = cam_buf[(y * w + x) * 2];
            }
        }
        quirc_end(qr_ctx);

        // Check for decoded QR codes
        int num = quirc_count(qr_ctx);
        if (num > 0) {
            struct quirc_code code;
            struct quirc_data data;

            quirc_extract(qr_ctx, 0, &code);
            if (quirc_decode(&code, &data) == QUIRC_SUCCESS) {
                // Got a valid QR code!
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

    return result;
}
