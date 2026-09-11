#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "network.h"

#define MAX_USERNAME_LEN 64
#define MAX_PASSWORD_LEN 64
#define MAX_JETON_LEN 256
#define MAX_URL_LEN 256
#define MAX_PIN 5  // 4 digits + null

typedef struct {
    char username[MAX_USERNAME_LEN];
    char password[MAX_PASSWORD_LEN];
    char jeton_moyen[MAX_JETON_LEN];    // For decrypting username
    char jeton_super[MAX_JETON_LEN];    // For decrypting password
    char url[MAX_URL_LEN];
    char pin[MAX_PIN];
    char uuid[37];  // UUID format: 8-4-4-4-12 = 36 chars + null
    int current_field;  // 0: waiting for QR, 1: PIN entry
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

    if (app_state.current_field == 0) {
        // QR code scan screen
        printf("> QR Code: [");
        if (strlen(app_state.url) > 0) {
            printf("SCANNED]\n");
            printf("  School: %s\n", app_state.url);
        } else {
            printf("NOT SCANNED]\n");
        }
        printf("\n");
        printf("SCAN QR CODE from Pronote:\n");
        printf("Note: Need to manually enter PIN\n");
    } else {
        // PIN entry screen
        printf("> QR Code: [SCANNED]\n");
        printf("  School: %s\n\n", app_state.url);
        printf("> PIN Code: [");
        for (int i = 0; i < strlen(app_state.pin); i++) printf("*");
        printf("]\n");
        printf("  (4 digits)\n");
    }

    printf("\n--- CONTROLS ---\n");
    if (app_state.current_field == 0) {
        printf("A: Paste QR data\n");
        printf("Y: Manual entry\n");
    } else {
        printf("0-9: Enter PIN digit\n");
        printf("B: Delete digit\n");
        printf("X: Login\n");
    }
    printf("START: Exit\n\n");
    printf("Status: %s\n", app_state.status_message);
    
    gfxFlushBuffers();
    gfxSwapBuffers();
}

void open_keyboard_for_qr() {
    SwkbdState swkbd;
    char temp_buffer[512] = {0};
    
    swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, 80);
    swkbdSetHintText(&swkbd, "Paste QR JSON data (url, login, jeton fields)");
    
    SwkbdButton button = swkbdInputText(&swkbd, temp_buffer, sizeof(temp_buffer));
    
    if (button == SWKBD_BUTTON_CONFIRM && strlen(temp_buffer) > 0) {
        // Parse JSON - simplified parsing for URL, login, jeton
        // Expecting format like: {"url":"https://...","login":"...","jeton":"..."}
        
        char *url_start = strstr(temp_buffer, "\"url\"");
        char *login_start = strstr(temp_buffer, "\"login\"");
        char *jeton_start = strstr(temp_buffer, "\"jeton\"");
        
        if (url_start && login_start && jeton_start) {
            // Extract values (simplified)
            // Real implementation would need proper JSON parsing
            safe_strncpy(app_state.status_message, "QR data loaded! Enter PIN", sizeof(app_state.status_message));
            app_state.current_field = 1;  // Move to PIN entry
            app_state.needs_redraw = 1;
        } else {
            safe_strncpy(app_state.status_message, "Invalid QR format", sizeof(app_state.status_message));
            app_state.needs_redraw = 1;
        }
    }
}

int main(int argc, char *argv[]) {
    gfxInitDefault();
    consoleInit(GFX_TOP, NULL);

    memset(&app_state, 0, sizeof(AppState));
    safe_strncpy(app_state.status_message, "Ready to scan QR code", sizeof(app_state.status_message));
    app_state.needs_redraw = 1;

    // Generate UUID for this device
    // In real implementation, would use random generator
    safe_strncpy(app_state.uuid, "3DS-Pronote-Device", sizeof(app_state.uuid));

    while (aptMainLoop()) {
        hidScanInput();
        u32 kdown = hidKeysDown();

        if (kdown & KEY_START) break;

        if (app_state.current_field == 0) {
            // QR code entry mode
            if (kdown & KEY_A) {
                open_keyboard_for_qr();
            }
            if (kdown & KEY_Y) {
                safe_strncpy(app_state.status_message, "Manual mode not yet implemented", sizeof(app_state.status_message));
                app_state.needs_redraw = 1;
            }
        } else {
            // PIN entry mode
            // Handle 0-9 keys for PIN input
            for (int i = 0; i < 10; i++) {
                if (kdown & (KEY_0 << i)) {
                    if (strlen(app_state.pin) < 4) {
                        app_state.pin[strlen(app_state.pin)] = '0' + i;
                        app_state.needs_redraw = 1;
                    }
                }
            }

            if (kdown & KEY_B) {
                if (strlen(app_state.pin) > 0) {
                    app_state.pin[strlen(app_state.pin) - 1] = '\0';
                    app_state.needs_redraw = 1;
                }
            }

            if (kdown & KEY_X) {
                if (strlen(app_state.pin) == 4) {
                    safe_strncpy(app_state.status_message, "Decrypting credentials...", sizeof(app_state.status_message));
                    app_state.needs_redraw = 1;
                    
                    // TODO: Decrypt credentials using PIN
                    // Would need AES/MD5 implementation
                    
                    safe_strncpy(app_state.status_message, "PIN accepted! (full auth coming)", sizeof(app_state.status_message));
                    app_state.logged_in = 1;
                    app_state.needs_redraw = 1;
                } else {
                    safe_strncpy(app_state.status_message, "Enter 4-digit PIN", sizeof(app_state.status_message));
                    app_state.needs_redraw = 1;
                }
            }
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
