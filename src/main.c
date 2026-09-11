#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "network.h"

#define MAX_USERNAME_LEN 64
#define MAX_JETON_LEN 256
#define MAX_PIN 5  // 4 digits + null

typedef struct {
    char username[MAX_USERNAME_LEN];
    char jeton[MAX_JETON_LEN];
    char pin[MAX_PIN];
    char uuid[37];
    int current_field;  // 0: username, 1: jeton, 2: pin
    int logged_in;
    char status_message[128];
    int needs_redraw;
} AppState;

AppState app_state;

void safe_strncpy(char *dest, const char *src, size_t maxlen) {
    if (!dest || !src || maxlen == 0) return;
    size_t len = strlen(src);
    if (len >= maxlen) len = maxlen - 1;
    memcpy(dest, src, len);
    dest[len] = '\0';
}

void draw_ui() {
    consoleClear();
    printf("\x1b[2;0H");
    printf("=== PRONOTE 3DS LOGIN ===\n\n");

    if (app_state.current_field == 0)
        printf("> Username: [%s]\n", app_state.username);
    else
        printf("  Username: [%s]\n", app_state.username);
    printf("  (from QR code)\n\n");

    if (app_state.current_field == 1)
        printf("> Jeton:    [%s]\n", strlen(app_state.jeton) > 0 ? "***" : "");
    else
        printf("  Jeton:    [%s]\n", strlen(app_state.jeton) > 0 ? "***" : "");
    printf("  (encryption token)\n\n");

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
    printf("UP/DOWN:  Navigate fields\n");
    printf("A:        Edit field\n");
    printf("B:        Delete character\n");
    printf("Y:        Clear field\n");
    printf("X:        Login\n");
    printf("START:    Exit\n\n");
    printf("Status: %s\n", app_state.status_message);

    gfxFlushBuffers();
    gfxSwapBuffers();
}

void open_keyboard_for_field() {
    SwkbdState swkbd;
    char temp_buffer[512] = {0};
    const char *hint = "";

    if (app_state.current_field == 0) {
        swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, 64);
        hint = "Enter username from QR code";
        safe_strncpy(temp_buffer, app_state.username, sizeof(temp_buffer));
    } else if (app_state.current_field == 1) {
        swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, 256);
        hint = "Enter jeton (encryption token) from QR";
        safe_strncpy(temp_buffer, app_state.jeton, sizeof(temp_buffer));
    } else {
        swkbdInit(&swkbd, SWKBD_TYPE_NUMPAD, 1, 4);
        swkbdSetPasswordMode(&swkbd, SWKBD_PASSWORD_HIDE_DELAY);
        swkbdSetValidation(&swkbd, SWKBD_ANYTHING, 0, 0);
        swkbdSetFeatures(&swkbd, SWKBD_FIXED_WIDTH);
        hint = "Enter 4-digit PIN";
        safe_strncpy(temp_buffer, app_state.pin, sizeof(temp_buffer));
    }

    swkbdSetHintText(&swkbd, hint);
    SwkbdButton button = swkbdInputText(&swkbd, temp_buffer, sizeof(temp_buffer));

    if (button == SWKBD_BUTTON_CONFIRM) {
        if (app_state.current_field == 0)
            safe_strncpy(app_state.username, temp_buffer, sizeof(app_state.username));
        else if (app_state.current_field == 1)
            safe_strncpy(app_state.jeton, temp_buffer, sizeof(app_state.jeton));
        else
            safe_strncpy(app_state.pin, temp_buffer, sizeof(app_state.pin));
        app_state.needs_redraw = 1;
    }
}

int main(int argc, char *argv[]) {
    gfxInitDefault();
    consoleInit(GFX_TOP, NULL);

    memset(&app_state, 0, sizeof(AppState));
    safe_strncpy(app_state.status_message, "Scan QR code and enter data", sizeof(app_state.status_message));
    safe_strncpy(app_state.uuid, "3DS-Pronote-Device", sizeof(app_state.uuid));
    app_state.needs_redraw = 1;

    while (aptMainLoop()) {
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
        }
        if (kdown & KEY_B) {
            char *field = (app_state.current_field == 0) ? app_state.username :
                          (app_state.current_field == 1) ? app_state.jeton : app_state.pin;
            if (strlen(field) > 0) {
                field[strlen(field) - 1] = '\0';
                app_state.needs_redraw = 1;
            }
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
                safe_strncpy(app_state.status_message, "Enter jeton!", sizeof(app_state.status_message));
            else if (strlen(app_state.pin) != 4)
                safe_strncpy(app_state.status_message, "PIN must be 4 digits!", sizeof(app_state.status_message));
            else {
                safe_strncpy(app_state.status_message, "Decrypting... (coming soon)", sizeof(app_state.status_message));
                app_state.logged_in = 1;
            }
            app_state.needs_redraw = 1;
        }

        if (app_state.needs_redraw) {
            draw_ui();
            app_state.needs_redraw = 0;
        }

        gspWaitForVBlank();
    }

    gfxExit();
    return 0;
}
