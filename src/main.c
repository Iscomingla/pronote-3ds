#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

#define MAX_URL_LEN 256
#define MAX_USERNAME_LEN 64
#define MAX_PASSWORD_LEN 64
#define MAX_SCHOOL_LEN 32
#define MAX_RESPONSE_LEN 8192

typedef struct {
    char school_number[MAX_SCHOOL_LEN];
    char username[MAX_USERNAME_LEN];
    char password[MAX_PASSWORD_LEN];
    int current_field; // 0: school, 1: username, 2: password
    int logged_in;
    char status_message[256];
    int is_loading;
} AppState;

AppState app_state;

// Response buffer for CURL
typedef struct {
    char *memory;
    size_t size;
} ResponseBuffer;

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    ResponseBuffer *resp = (ResponseBuffer *)userp;
    
    char *ptr = realloc(resp->memory, resp->size + realsize + 1);
    if (!ptr) {
        strncpy(app_state.status_message, "Memory error", sizeof(app_state.status_message) - 1);
        return 0;
    }
    
    resp->memory = ptr;
    memcpy(&(resp->memory[resp->size]), contents, realsize);
    resp->size += realsize;
    resp->memory[resp->size] = 0;
    
    return realsize;
}

int perform_login(const char *school_number, const char *username, const char *password) {
    if (strlen(school_number) == 0 || strlen(username) == 0 || strlen(password) == 0) {
        strncpy(app_state.status_message, "Please fill all fields", sizeof(app_state.status_message) - 1);
        return 0;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        strncpy(app_state.status_message, "Failed to init CURL", sizeof(app_state.status_message) - 1);
        return 0;
    }

    char url[MAX_URL_LEN];
    snprintf(url, sizeof(url), "https://%s.index-education.net/pronote/eleve.html", school_number);

    // Prepare POST data with URL encoding
    char postdata[512];
    snprintf(postdata, sizeof(postdata), 
             "login=%s&password=%s&urlRetour=", 
             username, password);

    ResponseBuffer resp = {malloc(1), 0};
    if (!resp.memory) {
        strncpy(app_state.status_message, "Memory allocation failed", sizeof(app_state.status_message) - 1);
        curl_easy_cleanup(curl);
        return 0;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postdata);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Nintendo 3DS)");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);

    int login_success = 0;

    if (res != CURLE_OK) {
        snprintf(app_state.status_message, sizeof(app_state.status_message), 
                 "Error: %s", curl_easy_strerror(res));
    } else {
        // Check for success indicators in response
        if (strstr(resp.memory, "logout") || strstr(resp.memory, "deconnexion") ||
            strstr(resp.memory, "onglets") || strstr(resp.memory, "AccueilAncienLoginPage")) {
            strncpy(app_state.status_message, "Login successful!", sizeof(app_state.status_message) - 1);
            login_success = 1;
        } else {
            strncpy(app_state.status_message, "Login failed", sizeof(app_state.status_message) - 1);
        }
    }

    free(resp.memory);
    curl_easy_cleanup(curl);
    app_state.logged_in = login_success;
    return login_success;
}

char* get_field_pointer(int field) {
    if (field == 0) return app_state.school_number;
    if (field == 1) return app_state.username;
    return app_state.password;
}

int get_field_maxlen(int field) {
    if (field == 0) return MAX_SCHOOL_LEN - 1;
    if (field == 1) return MAX_USERNAME_LEN - 1;
    return MAX_PASSWORD_LEN - 1;
}

void draw_ui() {
    consoleClear();
    printf("=== 3DS PRONOTE LOGIN ===\n");
    printf("Developed with libctru\n\n");

    // School number field
    if (app_state.current_field == 0) {
        printf("> School Code: [%s]\n", app_state.school_number);
    } else {
        printf("  School Code: [%s]\n", app_state.school_number);
    }

    printf("  (example: 0260008t)\n\n");

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
    printf("B:        Clear character\n");
    printf("X:        LOGIN\n");
    printf("Y:        Clear field\n");
    printf("START:    Exit\n\n");

    if (app_state.is_loading) {
        printf("Status: Connecting...\n");
    } else {
        printf("Status: %s\n", app_state.status_message);
    }
}

void open_keyboard(int field) {
    SwkbdState swkbd;
    char temp_buffer[256] = {0};
    
    swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, MAX_SCHOOL_LEN);
    
    if (field == 0) {
        swkbdSetHintText(&swkbd, "Enter school code (e.g. 0260008t)");
        strcpy(temp_buffer, app_state.school_number);
    } else if (field == 1) {
        swkbdSetHintText(&swkbd, "Enter username");
        strcpy(temp_buffer, app_state.username);
    } else {
        swkbdSetFeatures(&swkbd, SWKBD_PASSWORD_HIDE);
        swkbdSetHintText(&swkbd, "Enter password");
        strcpy(temp_buffer, app_state.password);
    }

    SwkbdButton button = swkbdInputText(&swkbd, temp_buffer, sizeof(temp_buffer));

    if (button == SWKBD_BUTTON_CONFIRM) {
        if (field == 0) {
            strncpy(app_state.school_number, temp_buffer, MAX_SCHOOL_LEN - 1);
        } else if (field == 1) {
            strncpy(app_state.username, temp_buffer, MAX_USERNAME_LEN - 1);
        } else {
            strncpy(app_state.password, temp_buffer, MAX_PASSWORD_LEN - 1);
        }
    }
}

int main(int argc, char *argv[]) {
    gfxInitDefault();
    consoleInit(GFX_TOP, NULL);

    memset(&app_state, 0, sizeof(AppState));
    strncpy(app_state.status_message, "Ready to login", sizeof(app_state.status_message) - 1);

    curl_global_init(CURL_GLOBAL_DEFAULT);

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
            app_state.is_loading = 1;
            draw_ui();
            gfxFlushBuffers();
            gfxSwapBuffers();

            perform_login(app_state.school_number, app_state.username, app_state.password);
            app_state.is_loading = 0;
        }

        draw_ui();
        gfxFlushBuffers();
        gfxSwapBuffers();
    }

    curl_global_cleanup();
    gfxExit();
    return 0;
}
