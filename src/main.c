#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui.h"
#include "network.h"
#include "qr.h"

/*
 * The jeton is a hex-encoded AES-CBC ciphertext.
 * The Pronote protocol imposes no documented upper bound on its length.
 * 512 chars gives us comfortable headroom beyond any observed real-world value.
 */
#define MAX_USERNAME_LEN  64
#define MAX_JETON_LEN    513   /* 512 usable chars + null */
#define MAX_PIN            5   /* 4 digits + null */

typedef enum {
    SCREEN_LOGIN,
    SCREEN_QR_SCAN,
} Screen;

typedef struct {
    char username[MAX_USERNAME_LEN];
    char jeton[MAX_JETON_LEN];
    char pin[MAX_PIN];
    char uuid[37];
    int  current_field;  /* 0: username, 1: jeton (QR), 2: pin */
    int  logged_in;
    char status_message[128];
    int  needs_redraw;
    Screen screen;
} AppState;

static AppState app;

static void safe_strncpy(char *dest, const char *src, size_t maxlen) {
    if (!dest || !src || maxlen == 0) return;
    size_t len = strlen(src);
    if (len >= maxlen) len = maxlen - 1;
    memcpy(dest, src, len);
    dest[len] = '\0';
}

static void draw_login_ui(void) {
    consoleClear();
    printf("\x1b[2;0H");
    printf("=== PRONOTE 3DS LOGIN ===\n\n");

    /* Username */
    printf(app_state.current_field == 0 ? "> " : "  ");
    printf("Username: [%s]\n", app_state.username);
    printf("  (from QR code)\n\n");

    /* Jeton */
    printf(app_state.current_field == 1 ? "> " : "  ");
    printf("Jeton:    [%s]\n",
           strlen(app_state.jeton) > 0 ? "OK" : "NOT SCANNED");
    printf("  (scan QR code)\n\n");

    /* PIN */
    printf(app_state.current_field == 2 ? "> " : "  ");
    printf("PIN Code: [");
    for (int i = 0; i < (int)strlen(app_state.pin); i++) printf("*");
    printf("]\n");
    if (app_state.current_field == 2)
        printf("  (4 digits)\n");

    u32 val_col = strlen(value) == 0 ? COL_DIMTEXT : COL_WHITE;
    ui_text(FIELD_X + 8.0f, y + 16.0f, 0.55f, val_col, display);

    if (selected)
        ui_rect(FIELD_X, y + FIELD_H * 0.25f, 3.0f, FIELD_H * 0.5f, COL_LINE1);
}

static void draw_login_screen(void) {
    ui_frame_begin();

    /*
     * C2D_TargetClear MUST come before C2D_SceneBegin (ui_target).
     * Clear both targets upfront, then enter each scene to draw into it.
     */
    ui_clear_target(ui_get_target(GFX_TOP),    COL_BG);
    ui_clear_target(ui_get_target(GFX_BOTTOM), COL_BG);

    /* ---- TOP SCREEN ---- */
    ui_target(GFX_TOP);

    ui_rect(0, 0, SCREEN_TOP_W, HEADER_H, C2D_Color32(0x00, 0x60, 0x52, 0xFF));
    ui_text_centred(0, SCREEN_TOP_W, 8.0f, 0.65f, COL_WHITE, "notApro");
    ui_rect(0, STRIPE1_Y, SCREEN_TOP_W, STRIPE1_H, COL_LINE1);
    ui_rect(0, STRIPE2_Y, SCREEN_TOP_W, STRIPE2_H, COL_LINE2);

    float fy = CONTENT_Y;
    char lbl[80];

    snprintf(lbl, sizeof(lbl), "Username%s",
             app.current_field == 0 ? "  [A: edit]" : "");
    draw_field(fy, lbl, app.username, 0, app.current_field == 0);
    fy += FIELD_H + FIELD_GAP;

    snprintf(lbl, sizeof(lbl), "Pronote QR%s",
             app.current_field == 1 ? "  [A: scan]" : "");
    const char *qr_val = strlen(app.jeton) > 0 ? "Scanned \xe2\x9c\x93" : "Not scanned";
    draw_field(fy, lbl, qr_val, 0, app.current_field == 1);
    fy += FIELD_H + FIELD_GAP;

    snprintf(lbl, sizeof(lbl), "PIN code (4 digits)%s",
             app.current_field == 2 ? "  [A: enter]" : "");
    draw_field(fy, lbl, app.pin, 1, app.current_field == 2);

    ui_rect(0, STATUS_Y, SCREEN_TOP_W, STATUS_H, C2D_Color32(0x00, 0x50, 0x40, 0xCC));
    ui_hline(0, STATUS_Y, SCREEN_TOP_W, COL_LINE2);
    ui_text(8.0f, STATUS_Y + 2.0f, 0.45f, COL_WHITE, app.status_message);

    /* ---- BOTTOM SCREEN — controls ---- */
    ui_target(GFX_BOTTOM);

    ui_rect(0, 0, SCREEN_BOT_W, HEADER_H, C2D_Color32(0x00, 0x60, 0x52, 0xFF));
    ui_text_centred(0, SCREEN_BOT_W, 8.0f, 0.65f, COL_WHITE, "Controls");
    ui_hline(0, HEADER_H, SCREEN_BOT_W, COL_LINE1);

    float cy  = HEADER_H + 10.0f;
    float lsz = 0.50f;
    float lg  = 18.0f;
    const char *keys[] = { "\xe2\x86\x91\xe2\x86\x93", "A", "Y", "X", "START" };
    const char *desc[] = { "Navigate fields", "Edit / Scan QR",
                           "Clear field", "Login", "Exit" };
    for (int i = 0; i < 5; i++) {
        ui_text(12.0f, cy, lsz, COL_LINE1, keys[i]);
        ui_text(44.0f, cy, lsz, COL_WHITE, desc[i]);
        cy += lg;
    }
    ui_hline(0, SCREEN_H - 3.0f, SCREEN_BOT_W, COL_LINE2);
    ui_hline(0, SCREEN_H - 1.0f, SCREEN_BOT_W, COL_LINE1);

    ui_frame_end();
}

/* Open swkbd for the current field, pre-filled with its existing value. */
static void open_keyboard_for_field(void) {
    if (app_state.current_field == 1) {
        /* Jeton field — trigger QR scan */
        app_state.screen = SCREEN_QR_SCAN;
        return;
    }

    SwkbdState swkbd;
    char tmp[512] = {0};

    if (app_state.current_field == 0) {
        swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, MAX_USERNAME_LEN - 1);
        swkbdSetHintText(&swkbd, "Enter username from QR code");
        safe_strncpy(tmp, app_state.username, sizeof(tmp));
    } else {
        /* PIN */
        swkbdInit(&swkbd, SWKBD_TYPE_NUMPAD, 1, 4);
        swkbdSetPasswordMode(&swkbd, SWKBD_PASSWORD_HIDE_DELAY);
        swkbdSetValidation(&swkbd, SWKBD_ANYTHING, 0, 0);
        swkbdSetFeatures(&swkbd, SWKBD_FIXED_WIDTH);
        swkbdSetHintText(&swkbd, "Enter 4-digit PIN");
        safe_strncpy(tmp, app_state.pin, sizeof(tmp));
    }

    /* Pre-fill keyboard with existing content if any */
    if (tmp[0] != '\0')
        swkbdSetInitialText(&swkbd, tmp);

    memset(tmp, 0, sizeof(tmp));
    SwkbdButton btn = swkbdInputText(&swkbd, tmp, sizeof(tmp));

    if (btn == SWKBD_BUTTON_CONFIRM) {
        if (app_state.current_field == 0)
            safe_strncpy(app_state.username, tmp, sizeof(app_state.username));
        else
            safe_strncpy(app_state.pin, tmp, sizeof(app_state.pin));
        app_state.needs_redraw = 1;
    }
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    /*
     * gfxInitDefault() brings up the LCD and GSP service — required first.
     * C3D_Init() (inside ui_init()) then takes over the GPU command pipe
     * on top of that. Both are needed; gfxExit() cleans up on exit.
     * Every official citro2d/citro3d example follows this same order.
     */
    gfxInitDefault();
    ui_init();

    memset(&app, 0, sizeof(AppState));
    safe_strncpy(app.status_message, "Scan QR code, then enter PIN",
                 sizeof(app.status_message));
    safe_strncpy(app.uuid, "3DS-Pronote-Device", sizeof(app.uuid));
    app.screen       = SCREEN_LOGIN;
    app.needs_redraw = 1;

    while (aptMainLoop()) {
        if (app_state.screen == SCREEN_QR_SCAN) {
            char qr_result[MAX_JETON_LEN + MAX_USERNAME_LEN + 64] = {0};

            int scan_result = qr_scan(qr_result, sizeof(qr_result));

            if (scan_result == QR_SUCCESS) {
                /* Parse JSON: {"login":"...","jeton":"..."} */
                char *login_ptr = strstr(qr_result, "\"login\":");
                char *jeton_ptr = strstr(qr_result, "\"jeton\":");

                if (login_ptr && jeton_ptr) {
                    login_ptr += 9; /* skip "login":" */
                    char *end = strchr(login_ptr, '"');
                    if (end) {
                        size_t len = (size_t)(end - login_ptr);
                        if (len >= MAX_USERNAME_LEN) len = MAX_USERNAME_LEN - 1;
                        memcpy(app_state.username, login_ptr, len);
                        app_state.username[len] = '\0';
                    }

                    jeton_ptr += 9; /* skip "jeton":" */
                    end = strchr(jeton_ptr, '"');
                    if (end) {
                        size_t len = (size_t)(end - jeton_ptr);
                        if (len >= MAX_JETON_LEN) len = MAX_JETON_LEN - 1;
                        memcpy(app_state.jeton, jeton_ptr, len);
                        app_state.jeton[len] = '\0';
                    }

                    safe_strncpy(app_state.status_message, "QR scanned! Enter PIN", sizeof(app_state.status_message));
                    app_state.current_field = 2;
                } else {
                    safe_strncpy(app_state.status_message, "Invalid QR format", sizeof(app_state.status_message));
                }
            } else if (rc == QR_CANCELLED) {
                safe_strncpy(app.status_message, "Scan cancelled.",
                             sizeof(app.status_message));
            } else {
                safe_strncpy(app.status_message, "QR scan failed.",
                             sizeof(app.status_message));
            }

            app.screen       = SCREEN_LOGIN;
            app.needs_redraw = 1;
            continue;
        }

        /* LOGIN screen */
        hidScanInput();
        u32 kdown = hidKeysDown();

        if (kdown & KEY_START) break;

        if (kdown & KEY_UP) {
            app.current_field = (app.current_field - 1 + 3) % 3;
            app.needs_redraw  = 1;
        }
        if (kdown & KEY_DOWN) {
            app.current_field = (app.current_field + 1) % 3;
            app.needs_redraw  = 1;
        }
        if (kdown & KEY_A) {
            open_keyboard();
            app.needs_redraw = 1;
        }
        if (kdown & KEY_Y) {
            if      (app_state.current_field == 0) memset(app_state.username, 0, sizeof(app_state.username));
            else if (app_state.current_field == 1) memset(app_state.jeton,    0, sizeof(app_state.jeton));
            else                                   memset(app_state.pin,       0, sizeof(app_state.pin));
            app_state.needs_redraw = 1;
        }
        if (kdown & KEY_X) {
            if (strlen(app.username) == 0)
                safe_strncpy(app.status_message, "Enter username first!",
                             sizeof(app.status_message));
            else if (strlen(app.jeton) == 0)
                safe_strncpy(app.status_message, "Scan QR code first!",
                             sizeof(app.status_message));
            else if (strlen(app.pin) != 4)
                safe_strncpy(app.status_message, "PIN must be 4 digits!",
                             sizeof(app.status_message));
            else {
                safe_strncpy(app.status_message, "Decrypting... (coming soon)",
                             sizeof(app.status_message));
                app.logged_in = 1;
            }
            app.needs_redraw = 1;
        }

        if (app.needs_redraw) {
            draw_login_screen();
            app.needs_redraw = 0;
        }

        gspWaitForVBlank();
    }

    ui_exit();
    gfxExit();
    return 0;
}
