#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Constants
#define MAX_URL_LEN 256
#define MAX_USERNAME_LEN 64
#define MAX_PASSWORD_LEN 64
#define MAX_SCHOOL_LEN 32

// UI State
typedef struct {
    char school_number[MAX_SCHOOL_LEN];
    char username[MAX_USERNAME_LEN];
    char password[MAX_PASSWORD_LEN];
    int current_field; // 0: school, 1: username, 2: password
    int logged_in;
    char status_message[128];
} AppState;

AppState app_state;

// Safe string copy
void safe_strncpy(char *dest, const char *src, size_t maxlen) {
    if (!dest || !src || maxlen == 0) return;
    size_t len = strlen(src);
    if (len >= maxlen) len = maxlen - 1;
    memcpy(dest, src, len);
    dest[len] = '\0';
}

// Draw the UI
void draw_ui() {
    consoleClear();
    printf("\x1b[2;0H"); // Move to row 2
    printf("=== PRONOTE 3DS LOGIN ===\n\n");

    // School number field
    if (app_state.current_field == 0) {
        printf("> School Code: [%s]\n", app_state.school_number);
    } else {
        printf("  School Code: [%s]\n", app_state.school_number);
    }
    printf("  (e.g., 0260008t)\n\n");

    // Username field
    if (app_state.current_field == 1) {
        printf("> Username:    [%s]\n", app_state.username);
    } else {
        printf("  Username:    [%s]\n", app_state.username);
    }
    printf("\n");

    // Password field (masked)
    if (app_state.current_field == 2) {
        printf("> Password:    [");
        for (int i = 0; i < (int)strlen(app_state.password); i++) printf("*");
        printf("]\n");
    } else {
        printf("  Password:    [");
        for (int i = 0; i < (int)strlen(app_state.password); i++) printf("*");
        printf("]\n");
    }

    printf("\n--- CONTROLS ---\n");
    printf("UP/DOWN:  Navigate fields\n");
    printf("A:        Open keyboard\n");
    printf("B:        Delete character\n");
    printf("Y:        Clear field\n");
    printf("X:        Submit login\n");
    printf("START:    Exit\n\n");
    printf("Status: %s\n", app_state.status_message);
}

// Get pointer to current field
char* get_field_pointer(int field) {
    if (field == 0) return app_state.school_number;
    if (field == 1) return app_state.username;
    return app_state.password;
}

// Get max length for current field
int get_field_maxlen(int field) {
    if (field == 0) return MAX_SCHOOL_LEN - 1;
    if (field == 1) return MAX_USERNAME_LEN - 1;
    return MAX_PASSWORD_LEN - 1;
}

// Open software keyboard
void open_keyboard(int field) {
    SwkbdState swkbd;
    char temp_buffer[256] = {0};
    
    swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, 50);
    
    if (field == 0) {
        swkbdSetHintText(&swkbd, "Enter school code (e.g. 0260008t)");
        safe_strncpy(temp_buffer, app_state.school_number, sizeof(temp_buffer));
    } else if (field == 1) {
        swkbdSetHintText(&swkbd, "Enter username");
        safe_strncpy(temp_buffer, app_state.username, sizeof(temp_buffer));
    } else {
        swkbdSetFeatures(&swkbd, SWKBD_PASSWORD_HIDE);
        swkbdSetHintText(&swkbd, "Enter password");
        safe_strncpy(temp_buffer, app_state.password, sizeof(temp_buffer));
    }

    SwkbdButton button = swkbdInputText(&swkbd, temp_buffer, sizeof(temp_buffer));

    if (button == SWKBD_BUTTON_CONFIRM) {
        char *target = get_field_pointer(field);
        int maxlen = get_field_maxlen(field);
        safe_strncpy(target, temp_buffer, maxlen + 1);
    }
}

// Validate login (offline validation)
int validate_login(const char *school, const char *username, const char *password) {
    if (strlen(school) == 0 || strlen(username) == 0 || strlen(password) == 0) {
        safe_strncpy(app_state.status_message, "Fill all fields!", sizeof(app_state.status_message));
        return 0;
    }

    if (strlen(school) < 5) {
        safe_strncpy(app_state.status_message, "School code too short", sizeof(app_state.status_message));
        return 0;
    }

    // Simulated validation (no actual network)
    safe_strncpy(app_state.status_message, "Ready to connect (offline mode)", sizeof(app_state.status_message));
    return 1;
}

int main(int argc, char *argv[]) {
    gfxInitDefault();
    consoleInit(GFX_TOP, NULL);

    // Initialize app state
    memset(&app_state, 0, sizeof(AppState));
    safe_strncpy(app_state.status_message, "Ready to login", sizeof(app_state.status_message));

    while (aptMainLoop()) {
        gspWaitForVBlank();
        hidScanInput();

        u32 kdown = hidKeysDown();

        if (kdown & KEY_START) {
            break;
        }

        if (kdown & KEY_UP) {
            app_state.current_field = (app_state.current_field - 1 + 3) % 3;
        }

        if (kdown & KEY_DOWN) {
            app_state.current_field = (app_state.current_field + 1) % 3;
        }

        if (kdown & KEY_A) {
            open_keyboard(app_state.current_field);
        }

        if (kdown & KEY_B) {
            char *field = get_field_pointer(app_state.current_field);
            if (strlen(field) > 0) {
                field[strlen(field) - 1] = '\0';
            }
        }

        if (kdown & KEY_Y) {
            memset(get_field_pointer(app_state.current_field), 0, get_field_maxlen(app_state.current_field));
        }

        if (kdown & KEY_X) {
            validate_login(app_state.school_number, app_state.username, app_state.password);
        }

        draw_ui();
        gfxFlushBuffers();
        gfxSwapBuffers();
    }

    gfxExit();
    return 0;
}
