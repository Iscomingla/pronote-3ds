#pragma once
#include <stdint.h>
#include <stddef.h>

/*
 * Pronote jeton decryption.
 *
 * The Pronote QR code contains a JSON payload with a "jeton" field.
 * The jeton is a hex-encoded AES-128-CBC ciphertext:
 *   key = MD5(PIN as ASCII string)   -- 16 bytes
 *   IV  = 16 zero bytes
 *
 * pronote_decrypt_jeton() decodes the hex, decrypts in-place, strips
 * PKCS#7 padding, and writes the plaintext to out_buf.
 *
 * Returns 0 on success, -1 on error (bad hex, bad padding, buf too small).
 */
int pronote_decrypt_jeton(const char *jeton_hex, const char *pin,
                          char *out_buf, size_t out_len);
