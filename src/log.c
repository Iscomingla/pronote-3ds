/*
 * log.c -- SD card logging for notApro
 *
 * Appends timestamped lines to sdmc:/3ds/notApro/log.txt.
 * The directory is created if it doesn't exist.
 * File is kept open between writes for speed; flushed on every write
 * so the log survives a crash.
 */

#include "log.h"
#include <3ds.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>

static FILE *s_log = NULL;

void log_init(void) {
    /* Create directory tree if needed */
    mkdir("sdmc:/3ds",      0777);
    mkdir("sdmc:/3ds/notApro", 0777);

    s_log = fopen(LOG_PATH, "a");
    if (!s_log) return;

    /* Session separator */
    fprintf(s_log, "\n=== notApro session start ===\n");
    fflush(s_log);
}

void log_close(void) {
    if (!s_log) return;
    fprintf(s_log, "=== session end ===\n");
    fflush(s_log);
    fclose(s_log);
    s_log = NULL;
}

void log_write(const char *fmt, ...) {
    if (!s_log) return;

    /* Timestamp: tick-based ms since boot */
    u64 ms = osGetTime();
    fprintf(s_log, "[%llu] ", (unsigned long long)ms);

    va_list args;
    va_start(args, fmt);
    vfprintf(s_log, fmt, args);
    va_end(args);

    fputc('\n', s_log);
    fflush(s_log);  /* flush every write so crashes don't lose lines */
}
