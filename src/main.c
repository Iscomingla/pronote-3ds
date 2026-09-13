#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui.h"
#include "network.h"
#include "log.h"

#define USER_JSON_PATH  "sdmc:/3ds/notApro/user.json"

#define MAX_USERNAME_LEN  64
#define MAX_JETON_LEN    513
#define MAX_PIN            5

#define HEADER_H    36.0f
#define STRIPE1_Y   HEADER_H
#define STRIPE1_H    4.0f
#define STRIPE2_Y   (STRIPE1_Y + STRIPE1_H)
#define STRIPE2_H    2.0f
#define CONTENT_Y   (STRIPE2_Y + STRIPE2_H + 12.0f)
#define FIELD_H     34.0f
#define FIELD_GAP    8.0f
#define FIELD_X     16.0f
#define FIELD_W     (SCREEN_TOP_W - FIELD_X * 2.0f)
#define STATUS_H    18.0f
#define STATUS_Y    (SCREEN_H - STATUS_H)
#define CTRL_KEY_X   12.0f
#define CTRL_DESC_X  64.0f

typedef struct {
    char  username[MAX_USERNAME_LEN];
    char  jeton[MAX_JETON_LEN];
    char  pin[MAX_PIN];
    char  uuid[37];
    int   current_field;   /* 0: pin only — username/jeton come from file */
    int   logged_in;
    int   user_loaded;     /* 1 if user.json was read successfully */
    char  status_message[128];
    int   needs_redraw;
} AppState;

static AppState app;

static void safe_strncpy(char *dest, const char *src, size_t maxlen) {
    if (!dest || !src || maxlen == 0) return;
    size_t len = strlen(src);
    if (len >= maxlen) len = maxlen - 1;
    memcpy(dest, src, len);
    dest[len] = '\0';
}

/* ---------------------------------------------------------------------------
 * json_extract: pull the value of "key" from a JSON string.
 * Handles both "key":"value" (string) and "key":value (bare).
 * Returns 1 on success, 0 on failure.
 * --------------------------------------------------------------------------- */
static int json_extract(const char *json, const char *key,
                        char *out, size_t out_len) {
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\":", key);
    const char *p = strstr(json, needle);
    if (!p) return 0;
    p += strlen(needle);
    while (*p == ' ') p++;

    if (*p == '"') {
        /* quoted string */
        p++;
        const char *end = strchr(p, '"');
        if (!end) return 0;
        size_t n = (size_t)(end - p);
        if (n >= out_len) n = out_len - 1;
        memcpy(out, p, n);
        out[n] = '\0';
    } else {
        /* bare value — read until , } or end */
        const char *end = p;
        while (*end && *end != ',' && *end != '}' && *end != '\n') end++;
        size_t n = (size_t)(end - p);
        if (n >= out_len) n = out_len - 1;
        memcpy(out, p, n);
        out[n] = '\0';
    }
    return 1;
}

/* ---------------------------------------------------------------------------
 * load_user_json: read sdmc:/3ds/notApro/user.json and populate
 * app.username and app.jeton.
 * --------------------------------------------------------------------------- */
static void load_user_json(void) {
    FILE *f = fopen(USER_JSON_PATH, "r");
    if (!f) {
        LOG("user.json not found at %s", USER_JSON_PATH);
        safe_strncpy(app.status_message,
                     "No user.json — see README",
                     sizeof(app.status_message));
        return;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);

    if (sz <= 0 || sz > 4096) {
        LOG("user.json size %ld out of range", sz);
        fclose(f);
        safe_strncpy(app.status_message,
                     "user.json too large or empty",
                     sizeof(app.status_message));
        return;
    }

    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = '\0';

    int ok_u = json_extract(buf, "login",  app.username, sizeof(app.username));
    int ok_j = json_extract(buf, "jeton",  app.jeton,    sizeof(app.jeton));
    free(buf);

    if (ok_u && ok_j) {
        app.user_loaded = 1;
        LOG("user.json loaded: login=%s, jeton_len=%zu",
            app.username, strlen(app.jeton));
        safe_strncpy(app.status_message,
                     "Loaded — enter your PIN",
                     sizeof(app.status_message));
    } else {
        LOG("user.json missing 'login' or 'jeton' field (ok_u=%d ok_j=%d)",
            ok_u, ok_j);
        safe_strncpy(app.status_message,
                     "user.json: missing login or jeton",
                     sizeof(app.status_message));
    }
}

/* ---------------------------------------------------------------------------
 * draw_field
 * --------------------------------------------------------------------------- */
static void draw_field(float y, const char *label, const char *value,
                       int is_masked, int selected) {
    u32 bg   = selected ? COL_SELECTED : C2D_Color32(0x00, 0x00, 0x00, 0x28);
    u32 bord = selected ? COL_LINE1    : COL_LINE2;

    ui_rect(FIELD_X, y, FIELD_W, FIELD_H, bg);
    ui_hline(FIELD_X, y + FIELD_H - 1.0f, FIELD_W, bord);
    ui_text(FIELD_X + 8.0f, y + 4.0f, 0.45f, COL_DIMTEXT, label);

    char display[128] = {0};
    if (is_masked && strlen(value) > 0) {
        for (int i = 0; i < (int)strlen(value) && i < 4; i++)
            display[i] = '*';
    } else if (strlen(value) == 0) {
        safe_strncpy(display, "\xe2\x80\x94", sizeof(display));
    } else if (strlen(value) > 24) {
        memcpy(display, value, 21);
        strcat(display, "...");
    } else {
        safe_strncpy(display, value, sizeof(display));
    }

    u32 val_col = strlen(value) == 0 ? COL_DIMTEXT : COL_WHITE;
    ui_text(FIELD_X + 8.0f, y + 16.0f, 0.55f, val_col, display);

    if (selected)
        ui_rect(FIELD_X, y + FIELD_H * 0.25f, 3.0f, FIELD_H * 0.5f, COL_LINE1);
}

/* ---------------------------------------------------------------------------
 * draw_login_screen
 * --------------------------------------------------------------------------- */
static void draw_login_screen(void) {
    ui_frame_begin();

    ui_clear_target(ui_get_target(GFX_TOP),    COL_BG);
    ui_clear_target(ui_get_target(GFX_BOTTOM), COL_BG);

    /* ---- TOP ---- */
    ui_target(GFX_TOP);
    ui_rect(0, 0, SCREEN_TOP_W, HEADER_H, C2D_Color32(0x00, 0x60, 0x52, 0xFF));
    ui_text_centred(0, SCREEN_TOP_W, 8.0f, 0.65f, COL_WHITE, "notApro");
    ui_rect(0, STRIPE1_Y, SCREEN_TOP_W, STRIPE1_H, COL_LINE1);
    ui_rect(0, STRIPE2_Y, SCREEN_TOP_W, STRIPE2_H, COL_LINE2);

    float fy = CONTENT_Y;

    /* Username — read-only, from file */
    const char *user_label = app.user_loaded ? "Username (from user.json)"
                                             : "Username (no user.json)";
    draw_field(fy, user_label, app.username, 0, 0);
    fy += FIELD_H + FIELD_GAP;

    /* Jeton — read-only, from file */
    const char *jeton_val = strlen(app.jeton) > 0 ? "Loaded \xe2\x9c\x93"
                                                   : "Missing";
    draw_field(fy, "Jeton (from user.json)", jeton_val, 0, 0);
    fy += FIELD_H + FIELD_GAP;

    /* PIN — the only interactive field */
    draw_field(fy, "PIN code (4 digits)  [A: enter]", app.pin, 1, 1);

    /* Status bar */
    ui_rect(0, STATUS_Y, SCREEN_TOP_W, STATUS_H,
            C2D_Color32(0x00, 0x50, 0x40, 0xCC));
    ui_hline(0, STATUS_Y, SCREEN_TOP_W, COL_LINE2);
    ui_text(8.0f, STATUS_Y + 2.0f, 0.45f, COL_WHITE, app.status_message);

    /* ---- BOTTOM ---- */
    ui_target(GFX_BOTTOM);
    ui_rect(0, 0, SCREEN_BOT_W, HEADER_H, C2D_Color32(0x00, 0x60, 0x52, 0xFF));
    ui_text_centred(0, SCREEN_BOT_W, 8.0f, 0.65f, COL_WHITE, "Controls");
    ui_hline(0, HEADER_H, SCREEN_BOT_W, COL_LINE1);

    float cy = HEADER_H + 10.0f;
    const float lsz = 0.50f, lg = 18.0f;
    const char *keys[]  = { "A", "Y", "X", "START" };
    const char *descs[] = { "Enter PIN", "Clear PIN", "Login", "Exit" };
    for (int i = 0; i < 4; i++) {
        ui_text(CTRL_KEY_X,  cy, lsz, COL_LINE1, keys[i]);
        ui_text(CTRL_DESC_X, cy, lsz, COL_WHITE,  descs[i]);
        cy += lg;
    }
    ui_hline(0, SCREEN_H - 3.0f, SCREEN_BOT_W, COL_LINE2);
    ui_hline(0, SCREEN_H - 1.0f, SCREEN_BOT_W, COL_LINE1);

    ui_frame_end();
}

/* ---------------------------------------------------------------------------
 * open_pin_keyboard
 * --------------------------------------------------------------------------- */
static void open_pin_keyboard(void) {
    SwkbdState swkbd;
    char tmp[MAX_PIN] = {0};
    safe_strncpy(tmp, app.pin, sizeof(tmp));

    swkbdInit(&swkbd, SWKBD_TYPE_NUMPAD, 1, 4);
    swkbdSetPasswordMode(&swkbd, SWKBD_PASSWORD_HIDE_DELAY);
    swkbdSetHintText(&swkbd, "Enter 4-digit PIN");
    if (tmp[0] != '\0') swkbdSetInitialText(&swkbd, tmp);

    memset(tmp, 0, sizeof(tmp));
    if (swkbdInputText(&swkbd, tmp, sizeof(tmp)) == SWKBD_BUTTON_CONFIRM) {
        safe_strncpy(app.pin, tmp, sizeof(app.pin));
        LOG("PIN entered (%zu digits)", strlen(app.pin));
        app.needs_redraw = 1;
    }
}

/* ---------------------------------------------------------------------------
 * main
 * --------------------------------------------------------------------------- */
int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    gfxInitDefault();
    log_init();
    ui_init();

    LOG("notApro started");

    memset(&app, 0, sizeof(AppState));
    safe_strncpy(app.uuid, "3DS-Pronote-Device", sizeof(app.uuid));
    app.needs_redraw = 1;

    /* Load credentials from SD card on startup */
    load_user_json();

    while (aptMainLoop()) {
        hidScanInput();
        u32 kdown = hidKeysDown();

        if (kdown & KEY_START) {
            LOG("exit requested");
            break;
        }

        if (kdown & KEY_A) {
            open_pin_keyboard();
            app.needs_redraw = 1;
        }
        if (kdown & KEY_Y) {
            memset(app.pin, 0, sizeof(app.pin));
            LOG("PIN cleared");
            app.needs_redraw = 1;
        }
        if (kdown & KEY_X) {
            if (!app.user_loaded) {
                safe_strncpy(app.status_message,
                             "No user.json — see README",
                             sizeof(app.status_message));
                LOG("login attempt: no user.json");
            } else if (strlen(app.pin) != 4) {
                safe_strncpy(app.status_message,
                             "PIN must be 4 digits!",
                             sizeof(app.status_message));
                LOG("login attempt: PIN length %zu", strlen(app.pin));
            } else {
                LOG("login attempt: user=%s, jeton_len=%zu",
                    app.username, strlen(app.jeton));
                safe_strncpy(app.status_message,
                             "Decrypting... (coming soon)",
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

    LOG("notApro exiting");
    ui_exit();
    log_close();
    gfxExit();
    return 0;
}
