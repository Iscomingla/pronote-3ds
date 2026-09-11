#ifndef QR_H
#define QR_H

#include <stddef.h>

#define QR_SUCCESS   0
#define QR_CANCELLED 1
#define QR_ERROR     2

// Scan a QR code using the 3DS camera
// Blocks until a QR code is found or B is pressed
// Returns QR_SUCCESS, QR_CANCELLED, or QR_ERROR
int qr_scan(char *out_buf, size_t out_len);

#endif
