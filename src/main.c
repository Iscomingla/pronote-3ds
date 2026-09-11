#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "network.h"
#include "qr.h"

#define MAX_USERNAME_LEN 64
#define MAX_JETON_LEN 256
#define MAX_PIN 5  // 4 digits + null

typedef enum {
    SCREEN_LOGIN,
    SCREEN_QR_SCAN,
} Screen;

typedef struct {
    char username[MAX_USERNAME_LEN];
    char jeton[MAX_JETON_LEN];
    char pin[MAX_PIN];
    char uuid[37];
    int current_field;  // 0: username, 1: scan QR, 2: pin
    int logged_in;
    char status_message[128];
    int needs_redraw;
    Screen screen;
} AppState;

AppState app_state;

void safe_strncpy(char *dest, const char *src, size_t maxlen) {
    if (!dest || !src || maxlen == 0) return;
    size_t len = strlen(src);
    if (len >= maxlen) len = maxlen - 1;
    memcpy(dest, src, len);
    dest[len] = '\0';
}

void draw_login_ui() {
    consoleClear();
    printf("\x1b[2;0H");
    printf("=== PRONOTE 3DS LOGIN ===\n\n");

    // Username
    if (app_state.current_field == 0)
        printf("> Username: [%s]\n", app_state.username);
    else
        printf("  Username: [%s]\n", app_state.username);
    printf("  (from QR code)\n\n");

    // QR / Jeton
    if (app_state.current_field == 1)
        printf("> Jeton:    [%s]\n", strlen(app_state.jeton) > 0 ? "OK" : "NOT SCANNED");
    else
        printf("  Jeton:    [%s]\n", strlen(app_state.jeton) > 0 ? "OK" : "NOT SCANNED");
    printf("  (scan QR code)\n\n");

    // PIN
    if (app_state.current_field == 2) {
        printf("> PIN Code: [");
        for (int i = 0; i < (int)strlen(app_state.pin); i++) printf("*");
        printf("]\n  (4 digits)\n");
    } else {
        printf("  PIN Code: [");
        for (int i = 0; i < (int)strlen(app_state.pin); i++) printf("*");
        printf("]\n");
    }

    printf("\n--- CONTROLS ---\n");
    printf("UP/DOWN: Navigate fields\n");
    if (app_state.current_field == 1)
        printf("A: Scan QR with camera\n");
    else
        printf("A: Edit field\n");
    printf("Y: Clear field\n");
    printf("X: Login\n");
    printf("START: Exit\n\n");
    printf("Status: %s\n", app_state.status_message);

    gfxFlushBuffers();
    gfxSwapBuffers();
}

void open_keyboard_for_field() {
    // Only username and PIN use keyboard; jeton is scanned
    if (app_state.current_field == 1) {
        // Trigger QR scan instead
        app_state.screen = SCREEN_QR_SCAN;
        return;
    }

    SwkbdState swkbd;
    char temp_buffer[512] = {0};
    const char *hint = "";

    if (app_state.current_field == 0) {
        swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, 64);
        hint = "Enter username from QR code";
        // Pre-fill with existing content
        safe_strncpy(temp_buffer, app_state.username, sizeof(temp_buffer));
    } else {
        swkbdInit(&swkbd, SWKBD_TYPE_NUMPAD, 1, 4);
        swkbdSetPasswordMode(&swkbd, SWKBD_PASSWORD_HIDE_DELAY);
        hint = "Enter 4-digit PIN";
        // Pre-fill with existing PIN
        safe_strncpy(temp_buffer, app_state.pin, sizeof(temp_buffer));
    }

    swkbdSetHintText(&swkbd, hint);
    SwkbdButton button = swkbdInputText(&swkbd, temp_buffer, sizeof(temp_buffer));

    if (button == SWKBD_BUTTON_CONFIRM) {
        if (app_state.current_field == 0)
            safe_strncpy(app_state.username, temp_buffer, sizeof(app_state.username));
        else
            safe_strncpy(app_state.pin, temp_buffer, sizeof(app_state.pin));
        app_state.needs_redraw = 1;
    }
}

int main(int argc, char *argv[]) {
    gfxInitDefault();
    consoleInit(GFX_TOP, NULL);

    memset(&app_state, 0, sizeof(AppState));
    safe_strncpy(app_state.status_message, "Scan QR code with camera", sizeof(app_state.status_message));
    safe_strncpy(app_state.uuid, "3DS-Pronote-Device", sizeof(app_state.uuid));
    app_state.screen = SCREEN_LOGIN;
    app_state.needs_redraw = 1;

    while (aptMainLoop()) {
        if (app_state.screen == SCREEN_QR_SCAN) {
            // QR scanning mode - use bottom screen camera
            char qr_result[MAX_JETON_LEN + MAX_USERNAME_LEN + 64] = {0};

            consoleClear();
            printf("\x1b[2;0H");
            printf("=== QR CODE SCANNER ===\n\n");
            printf("Point camera at Pronote QR code\n");
            printf("B: Cancel\n\n");
            printf("Scanning...\n");
            gfxFlushBuffers();
            gfxSwapBuffers();

            int scan_result = qr_scan(qr_result, sizeof(qr_result));

            if (scan_result == QR_SUCCESS) {
                // Parse JSON from QR: {"login":"...","jeton":"...","url":"..."}
                char *login_ptr = strstr(qr_result, "\"login\":");
                char *jeton_ptr = strstr(qr_result, "\"jeton\":");

                if (login_ptr && jeton_ptr) {
                    // Extract login
                    login_ptr += 9; // skip "login":"
                    char *end = strchr(login_ptr, '"');
                    if (end) {
                        size_t len = end - login_ptr;
                        if (len >= MAX_USERNAME_LEN) len = MAX_USERNAME_LEN - 1;
                        memcpy(app_state.username, login_ptr, len);
                        app_state.username[len] = '\0';
                    }

                    // Extract jeton
                    jeton_ptr += 9; // skip "jeton":"
                    end = strchr(jeton_ptr, '"');
                    if (end) {
                        size_t len = end - jeton_ptr;
                        if (len >= MAX_JETON_LEN) len = MAX_JETON_LEN - 1;
                        memcpy(app_state.jeton, jeton_ptr, len);
                        app_state.jeton[len] = '\0';
                    }

                    safe_strncpy(app_state.status_message, "QR scanned! Enter PIN", sizeof(app_state.status_message));
                    app_state.current_field = 2; // Move to PIN
                } else {
                    safe_strncpy(app_state.status_message, "Invalid QR code format", sizeof(app_state.status_message));
                }
            } else if (scan_result == QR_CANCELLED) {
                safe_strncpy(app_state.status_message, "Scan cancelled", sizeof(app_state.status_message));
            } else {
                safe_strncpy(app_state.status_message, "QR scan failed", sizeof(app_state.status_message));
            }

            app_state.screen = SCREEN_LOGIN;
            app_state.needs_redraw = 1;
            continue;
        }

        // LOGIN screen
        hidScanInput();
        u32 kdown = hidKeysDown();

        if (kdown & KEY_START) break;

        if (kdown & KEY_UP) {
            app_state.current_field = (app_state.current_field - 1 + 3) % 3;
            app_state.needs_redraw = 1;
        }
        if (kdown & KEY_DOWN) {
            app_state.current_field = (app_state.current_field + 1) % 3;
            app_state.needs_redraw = 1;
        }
        if (kdown & KEY_A) {
            open_keyboard_for_field();
            app_state.needs_redraw = 1;
        }
        if (kdown & KEY_Y) {
            if (app_state.current_field == 0) memset(app_state.username, 0, sizeof(app_state.username));
            else if (app_state.current_field == 1) memset(app_state.jeton, 0, sizeof(app_state.jeton));
            else memset(app_state.pin, 0, sizeof(app_state.pin));
            app_state.needs_redraw = 1;
        }
        if (kdown & KEY_X) {
            if (strlen(app_state.username) == 0)
                safe_strncpy(app_state.status_message, "Enter username!", sizeof(app_state.status_message));
            else if (strlen(app_state.jeton) == 0)
                safe_strncpy(app_state.status_message, "Scan QR code first!", sizeof(app_state.status_message));
            else if (strlen(app_state.pin) != 4)
                safe_strncpy(app_state.status_message, "PIN must be 4 digits!", sizeof(app_state.status_message));
            else {
                safe_strncpy(app_state.status_message, "Decrypting... (coming soon)", sizeof(app_state.status_message));
                app_state.logged_in = 1;
            }
            app_state.needs_redraw = 1;
        }

        if (app_state.needs_redraw) {
            draw_login_ui();
            app_state.needs_redraw = 0;
        }

        gspWaitForVBlank();
    }

    gfxExit();
    return 0;
}
