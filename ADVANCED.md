# Advanced Features & Implementations

## Alternative Implementations

### 1. Session Persistence (Cookie Handling)

```c
// After successful login, save session cookie:
typedef struct {
    char session_id[256];
    time_t login_time;
    int is_valid;
} SessionInfo;

SessionInfo session = {0};

// Modify perform_login() to extract cookies:
curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "");  // In-memory
curl_easy_setopt(curl, CURLOPT_COOKIEJAR, "pronote.cookie");  // Save to file

// Use saved session for next request:
curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "pronote.cookie");
```

### 2. Response Parsing for Grades

```c
#include <regex.h>

int extract_grades(const char *html) {
    regex_t regex;
    regmatch_t match[2];
    
    // Example: Extract grade values from HTML
    const char *pattern = "<td class=\"note\">([0-9.]+)</td>";
    
    if (regcomp(&regex, pattern, REG_EXTENDED) == 0) {
        if (regexec(&regex, html, 2, match, 0) == 0) {
            // Parse matched grade
            printf("Grade: %.*s\n", 
                (int)(match[1].rm_eo - match[1].rm_so),
                html + match[1].rm_so);
        }
        regfree(&regex);
    }
    return 0;
}
```

### 3. Screen Output with Scrolling

```c
#define MAX_LINES 50
#define LINE_HEIGHT 16

typedef struct {
    char lines[MAX_LINES][SCREEN_WIDTH];
    int num_lines;
    int scroll_offset;
} ScrollBuffer;

ScrollBuffer output;

void scroll_draw() {
    consoleClear();
    
    int start = output.scroll_offset;
    int end = start + (SCREEN_HEIGHT / LINE_HEIGHT);
    
    for (int i = start; i < end && i < output.num_lines; i++) {
        printf("%s\n", output.lines[i - start]);
    }
}

void handle_scroll_input(u32 kdown) {
    if (kdown & KEY_UP) output.scroll_offset--;
    if (kdown & KEY_DOWN) output.scroll_offset++;
    
    // Clamp scroll
    if (output.scroll_offset < 0) output.scroll_offset = 0;
    if (output.scroll_offset > output.num_lines - 10) 
        output.scroll_offset = output.num_lines - 10;
}
```

### 4. Dual-Screen Layout

```c
// Use both top and bottom screens
int main() {
    gfxInitDefault();
    
    // Initialize both screens
    consoleInit(GFX_TOP, NULL);    // Top screen for login
    consoleInit(GFX_BOTTOM, NULL); // Bottom screen for info
    
    // Draw to different screens:
    printf("\n\nTop screen content\n");  // Renders on top
    
    // Switch to bottom screen context
    // Use BOTTOM screen for keyboard hints, status, etc.
}
```

### 5. Multi-Language Support

```c
typedef struct {
    const char *school;
    const char *username;
    const char *password;
    const char *login;
    const char *status;
} Language;

Language lang_en = {
    .school = "School Code",
    .username = "Username",
    .password = "Password",
    .login = "LOGIN",
    .status = "Status"
};

Language lang_fr = {
    .school = "Code école",
    .username = "Identifiant",
    .password = "Mot de passe",
    .login = "CONNEXION",
    .status = "État"
};

Language *current_lang = &lang_en;

void draw_ui_translated() {
    printf("> %s: [%s]\n", current_lang->school, app_state.school_number);
}
```

### 6. Secure Credential Storage

```c
#include <mbedtls/cipher.h>

typedef struct {
    unsigned char encrypted[256];
    size_t enc_len;
    unsigned char salt[16];
} EncryptedCreds;

// Simple XOR encryption (NOT production-grade!)
void simple_encrypt(const char *plain, unsigned char *out, size_t *out_len) {
    const char *key = "3DS_PRONOTE_KEY";
    
    *out_len = strlen(plain);
    for (size_t i = 0; i < *out_len; i++) {
        out[i] = plain[i] ^ key[i % strlen(key)];
    }
}

void save_credentials_encrypted(const char *school, const char *user, const char *pass) {
    FILE *f = fopen("pronote.conf", "wb");
    if (f) {
        unsigned char enc[256];
        size_t len;
        
        simple_encrypt(school, enc, &len);
        fwrite(enc, 1, len, f);
        
        fclose(f);
    }
}
```

---

## Extended Features

### 7. Display Parsed Grades

```c
typedef struct {
    char subject[64];
    float grade;
    float max_grade;
} GradeEntry;

GradeEntry grades[20];
int grade_count = 0;

void parse_and_display_grades(const char *html) {
    // Simple HTML parser (could use libxml2)
    const char *ptr = html;
    
    while ((ptr = strstr(ptr, "subject")) != NULL) {
        // Extract subject name
        // Extract grade value
        // Store in grades array
        grade_count++;
        ptr++;
    }
    
    // Display formatted grades
    for (int i = 0; i < grade_count; i++) {
        printf("%s: %.1f/%.1f\n", 
            grades[i].subject, 
            grades[i].grade, 
            grades[i].max_grade);
    }
}
```

### 8. Real-Time Status Updates

```c
#define STATUS_CONNECTING   0
#define STATUS_LOGGING_IN   1
#define STATUS_PARSING      2
#define STATUS_COMPLETE     3
#define STATUS_ERROR        4

typedef struct {
    int status;
    float progress;  // 0.0 to 1.0
    char message[128];
} LoadingState;

LoadingState loading;

void draw_loading_screen() {
    printf("    Loading... %d%%\n", (int)(loading.progress * 100));
    
    // Draw progress bar
    int bar_width = 20;
    int filled = (int)(bar_width * loading.progress);
    printf("    [");
    for (int i = 0; i < bar_width; i++) {
        printf(i < filled ? "=" : " ");
    }
    printf("]\n");
    
    printf("    %s\n", loading.message);
}
```

### 9. Error Recovery & Retry Logic

```c
#define MAX_RETRIES 3
#define RETRY_DELAY_MS 2000

int perform_login_with_retry(const char *school, const char *user, const char *pass) {
    for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
        snprintf(app_state.status_message, sizeof(app_state.status_message),
                "Attempt %d/%d...", attempt + 1, MAX_RETRIES);
        
        if (perform_login(school, user, pass)) {
            return 1;  // Success
        }
        
        if (attempt < MAX_RETRIES - 1) {
            // Wait before retry
            svcSleepThread(RETRY_DELAY_MS * 1000000);  // Convert to nanoseconds
        }
    }
    
    strncpy(app_state.status_message, "All attempts failed", 
            sizeof(app_state.status_message) - 1);
    return 0;
}
```

### 10. File-Based Logging

```c
FILE *log_file = NULL;

void log_message(const char *format, ...) {
    if (!log_file) {
        log_file = fopen("pronote_log.txt", "a");
    }
    
    if (log_file) {
        va_list args;
        va_start(args, format);
        vfprintf(log_file, format, args);
        fprintf(log_file, "\n");
        va_end(args);
        fflush(log_file);
    }
    
    // Also log to console
    printf("LOG: ");
    va_list args2;
    va_start(args2, format);
    vprintf(format, args2);
    va_end(args2);
    printf("\n");
}

// Use it:
log_message("Login attempt for user: %s", username);
log_message("Connection status: %s", curl_easy_strerror(res));
```

---

## Advanced CURL Options

```c
// Add these to perform_login() for better handling:

// Follow redirects
curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);

// Custom headers
struct curl_slist *headers = NULL;
headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
headers = curl_slist_append(headers, "Accept: text/html,application/xhtml+xml");
curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

// Connection pooling
curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 300L);

// Debugging
curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

// Clean up headers
curl_slist_free_all(headers);
```

---

## Memory Management Best Practices

```c
// For large data handling:
#define CHUNK_SIZE 1024

typedef struct {
    char *data;
    size_t size;
    size_t capacity;
} DynamicBuffer;

DynamicBuffer* create_buffer() {
    DynamicBuffer *buf = malloc(sizeof(DynamicBuffer));
    buf->data = malloc(CHUNK_SIZE);
    buf->size = 0;
    buf->capacity = CHUNK_SIZE;
    return buf;
}

void append_to_buffer(DynamicBuffer *buf, const char *data, size_t len) {
    if (buf->size + len > buf->capacity) {
        buf->capacity *= 2;
        buf->data = realloc(buf->data, buf->capacity);
    }
    memcpy(buf->data + buf->size, data, len);
    buf->size += len;
}

void free_buffer(DynamicBuffer *buf) {
    free(buf->data);
    free(buf);
}
```

---

## Performance Optimization

### Reduce HTTPS Overhead
```c
// Reuse connection
curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 0L);
```

### Implement Response Caching
```c
typedef struct {
    time_t last_fetch;
    char *data;
    int valid;
} CachedResponse;

CachedResponse cache = {0};
#define CACHE_DURATION 300  // 5 minutes

int use_cached_if_valid() {
    time_t now = time(NULL);
    if (cache.valid && (now - cache.last_fetch) < CACHE_DURATION) {
        return 1;  // Use cached data
    }
    return 0;
}
```

---

## Testing & Debugging

### Enable Debug Output
```c
#ifdef DEBUG_MODE
#define DEBUG(fmt, ...) printf("[DEBUG] " fmt "\n", ##__VA_ARGS__)
#else
#define DEBUG(fmt, ...)
#endif

// Use it:
DEBUG("School code: %s", school_number);
DEBUG("Response size: %zu bytes", resp.size);
```

### Network Packet Inspection
```c
// Enable CURL verbose mode to see all requests/responses
curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

// Redirect debug output to file
FILE *debug_log = fopen("curl_debug.log", "w");
curl_easy_setopt(curl, CURLOPT_STDERR, debug_log);
```

---

## Next Steps

1. **Implement credential caching** - Make repeated logins faster
2. **Add response parsing** - Actually display Pronote data
3. **Implement refresh mechanism** - Auto-update grades
4. **Add error handling** - Better recovery from failures
5. **Create UI improvements** - Multi-screen layout
6. **Add settings menu** - Configurable options

Each feature builds on the foundation. Start with the basic app working, then add features incrementally!
