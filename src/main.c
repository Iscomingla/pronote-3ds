#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "ui.h"
#include "network.h"
#include "crypto.h"
#include "log.h"

#define USER_JSON_PATH  "sdmc:/3ds/notApro/user.json"
#define TOKEN_PATH      "sdmc:/3ds/notApro/token.txt"

#define MAX_LOGIN_LEN    256
#define MAX_JETON_LEN    513
#define MAX_PIN            5
#define MAX_PLAIN_LEN    513
#define MAX_TOKEN_LEN    513
#define UUID_LEN          37

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
    char  login[MAX_LOGIN_LEN];
    char  jeton[MAX_JETON_LEN];
    char  url[256];
    char  pin[MAX_PIN];
    char  uuid[UUID_LEN];
    char  token[MAX_TOKEN_LEN];
    int   logged_in;
    int   user_loaded;
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

static int json_extract(const char *json, const char *key,
                        char *out, size_t out_len) {
    char needle[72];
    snprintf(needle, sizeof(needle), "\"%s\":\"", key);
    const char *p = strstr(json, needle);
    if (!p) return 0;
    p += strlen(needle);
    const char *end = p;
    while (*end && *end != '"') {
        if (*end == '\\' && *(end + 1)) end++;
        end++;
    }
    if (*end != '"') return 0;
    size_t n = (size_t)(end - p);
    if (n >= out_len) n = out_len - 1;
    memcpy(out, p, n);
    out[n] = '\0';
    return 1;
}

static void generate_uuid(char *buf, size_t buf_sz) {
    if (buf_sz < UUID_LEN) return;
    u64 t1 = svcGetSystemTick();
    u64 t2 = t1 ^ (t1 >> 17) ^ (t1 << 3);
    snprintf(buf, buf_sz,
        "%08llx-%04llx-4%03llx-%04llx-%012llx",
        (unsigned long long)(t1 & 0xFFFFFFFF),
        (unsigned long long)((t1 >> 32) & 0xFFFF),
        (unsigned long long)((t2 >> 16) & 0xFFF),
        (unsigned long long)((t2 & 0x3FFF) | 0x8000),
        (unsigned long long)(t1 ^ t2));
}

static void load_user_json(void) {
    FILE *f = fopen(USER_JSON_PATH, "r");
    if (!f) {
        LOG("user.json not found at %s", USER_JSON_PATH);
        safe_strncpy(app.status_message, "No user.json - see README",
                     sizeof(app.status_message));
        return;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz <= 0 || sz > 4096) {
        LOG("user.json size %ld out of range", sz);
        fclose(f);
        safe_strncpy(app.status_message, "user.json too large or empty",
                     sizeof(app.status_message));
        return;
    }
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = '\0';
    LOG("user.json raw: %s", buf);

    int ok_l = json_extract(buf, "login", app.login, sizeof(app.login));
    int ok_j = json_extract(buf, "jeton", app.jeton, sizeof(app.jeton));
    int ok_u = json_extract(buf, "url",   app.url,   sizeof(app.url));
    free(buf);
    (void)ok_u;

    if (ok_l && ok_j) {
        app.user_loaded = 1;
        LOG("loaded: login='%s' jeton_len=%zu url='%s'",
            app.login, strlen(app.jeton), app.url);
        safe_strncpy(app.status_message, "Loaded - enter your PIN",
                     sizeof(app.status_message));
    } else {
        LOG("missing fields (ok_l=%d ok_j=%d ok_u=%d)", ok_l, ok_j, ok_u);
        safe_strncpy(app.status_message, "user.json: missing login or jeton",
                     sizeof(app.status_message));
    }
}

static void save_token(const char *token) {
    mkdir("sdmc:/3ds", 0777);
    mkdir("sdmc:/3ds/notApro", 0777);
    FILE *f = fopen(TOKEN_PATH, "w");
    if (!f) { LOG("save_token: cannot write %s", TOKEN_PATH); return; }
    fprintf(f, "{\"token\":\"%s\"}\n", token);
    fclose(f);
    LOG("token saved to %s", TOKEN_PATH);
}

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
        safe_strncpy(display, "(empty)", sizeof(display));
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

static void draw_login_screen(void) {
    ui_frame_begin();
    ui_clear_target(ui_get_target(GFX_TOP),    COL_BG);
    ui_clear_target(ui_get_target(GFX_BOTTOM), COL_BG);

    ui_target(GFX_TOP);
    ui_rect(0, 0, SCREEN_TOP_W, HEADER_H, C2D_Color32(0x00, 0x60, 0x52, 0xFF));
    ui_text_centred(0, SCREEN_TOP_W, 8.0f, 0.65f, COL_WHITE, "notApro");
    ui_rect(0, STRIPE1_Y, SCREEN_TOP_W, STRIPE1_H, COL_LINE1);
    ui_rect(0, STRIPE2_Y, SCREEN_TOP_W, STRIPE2_H, COL_LINE2);

    float fy = CONTENT_Y;
    draw_field(fy, app.user_loaded ? "Login (from user.json)"
                                   : "Login (no user.json found)",
               app.login, 0, 0);
    fy += FIELD_H + FIELD_GAP;
    draw_field(fy, "Jeton (from user.json)",
               strlen(app.jeton) > 0 ? "OK" : "(empty)", 0, 0);
    fy += FIELD_H + FIELD_GAP;
    draw_field(fy, "PIN (4 digits)  [A: enter]", app.pin, 1, 1);

    ui_rect(0, STATUS_Y, SCREEN_TOP_W, STATUS_H,
            C2D_Color32(0x00, 0x50, 0x40, 0xCC));
    ui_hline(0, STATUS_Y, SCREEN_TOP_W, COL_LINE2);
    ui_text(8.0f, STATUS_Y + 2.0f, 0.45f, COL_WHITE, app.status_message);

    ui_target(GFX_BOTTOM);
    ui_rect(0, 0, SCREEN_BOT_W, HEADER_H, C2D_Color32(0x00, 0x60, 0x52, 0xFF));
    ui_text_centred(0, SCREEN_BOT_W, 8.0f, 0.65f, COL_WHITE, "Controls");
    ui_hline(0, HEADER_H, SCREEN_BOT_W, COL_LINE1);
    float cy = HEADER_H + 10.0f;
    const float lsz = 0.50f, lg = 18.0f;
    const char *keys[]  = { "A", "Y", "X", "START" };
    const char *descs[] = { "Enter PIN", "Clear PIN", "Login", "Exit app" };
    for (int i = 0; i < 4; i++) {
        ui_text(CTRL_KEY_X,  cy, lsz, COL_LINE1, keys[i]);
        ui_text(CTRL_DESC_X, cy, lsz, COL_WHITE,  descs[i]);
        cy += lg;
    }
    ui_hline(0, SCREEN_H - 3.0f, SCREEN_BOT_W, COL_LINE2);
    ui_hline(0, SCREEN_H - 1.0f, SCREEN_BOT_W, COL_LINE1);
    ui_frame_end();
}

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

static void do_login(void) {
    if (!app.user_loaded) {
        safe_strncpy(app.status_message, "No user.json - see README",
                     sizeof(app.status_message));
        return;
    }
    if (strlen(app.pin) != 4) {
        safe_strncpy(app.status_message, "PIN must be 4 digits!",
                     sizeof(app.status_message));
        return;
    }

    safe_strncpy(app.status_message, "Decrypting...", sizeof(app.status_message));
    draw_login_screen();

    char plain_login[MAX_PLAIN_LEN] = {0};
    if (pronote_decrypt(app.login, app.pin, plain_login, sizeof(plain_login)) != 0) {
        safe_strncpy(app.status_message, "Decryption failed - wrong PIN?",
                     sizeof(app.status_message));
        return;
    }
    LOG("plain_login='%s'", plain_login);

    char plain_jeton[MAX_PLAIN_LEN] = {0};
    if (pronote_decrypt(app.jeton, app.pin, plain_jeton, sizeof(plain_jeton)) != 0) {
        safe_strncpy(app.status_message, "Decryption failed - wrong PIN?",
                     sizeof(app.status_message));
        return;
    }
    LOG("plain_jeton obtained (len=%zu)", strlen(plain_jeton));

    safe_strncpy(app.status_message, "Connecting...", sizeof(app.status_message));
    draw_login_screen();

    char new_token[MAX_TOKEN_LEN] = {0};
    int result = pronote_login(
        app.url, plain_login, plain_jeton,
        app.uuid, new_token, sizeof(new_token)
    );

    if (result == 0) {
        safe_strncpy(app.token, new_token, sizeof(app.token));
        save_token(new_token);
        safe_strncpy(app.status_message, "Logged in!", sizeof(app.status_message));
        LOG("login OK, token saved");
        app.logged_in = 1;
    } else {
        char err[64];
        snprintf(err, sizeof(err), "Login failed (err %d)", result);
        safe_strncpy(app.status_message, err, sizeof(app.status_message));
        LOG("login failed: %d", result);
    }
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    gfxInitDefault();
    log_init();
    ui_init();
    LOG("notApro started");

    memset(&app, 0, sizeof(AppState));
    generate_uuid(app.uuid, sizeof(app.uuid));
    LOG("uuid=%s", app.uuid);
    app.needs_redraw = 1;
    load_user_json();

    while (aptMainLoop()) {
        hidScanInput();
        u32 kdown = hidKeysDown();
        if (kdown & KEY_START) { LOG("exit requested"); break; }
        if (kdown & KEY_A) { open_pin_keyboard(); app.needs_redraw = 1; }
        if (kdown & KEY_Y) {
            memset(app.pin, 0, sizeof(app.pin));
            LOG("PIN cleared");
            app.needs_redraw = 1;
        }
        if (kdown & KEY_X) { do_login(); app.needs_redraw = 1; }
        if (app.needs_redraw) { draw_login_screen(); app.needs_redraw = 0; }
        gspWaitForVBlank();
    }

    LOG("notApro exiting");
    ui_exit();
    log_close();
    gfxExit();
    return 0;
}
