#pragma once

/*
 * log.h -- SD card logging for notApro
 *
 * Log file: sdmc:/3ds/notApro/log.txt
 * The directory is created automatically on log_init().
 *
 * Usage:
 *   log_init();           // call once after gfxInitDefault()
 *   LOG("hello %d", 42); // printf-style, appends newline + timestamp
 *   log_close();          // call before gfxExit()
 */

#include <stdarg.h>

#define LOG_PATH  "sdmc:/3ds/notApro/log.txt"
#define LOG_DIR   "sdmc:/3ds/notApro"

void log_init(void);
void log_close(void);
void log_write(const char *fmt, ...);

#define LOG(...)  log_write(__VA_ARGS__)
