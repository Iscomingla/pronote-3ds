#pragma once
#include <stddef.h>

/*
 * pronote_decrypt(hex, pin, out_buf, out_len)
 *
 * Decrypts a hex-encoded Pronote AES-128-CBC ciphertext using the
 * 4-digit PIN as the key source.
 *
 * Used for both fields from user.json:
 *   - "login" field  -> Pronote username
 *   - "jeton" field  -> Pronote password
 *
 * Algorithm:
 *   key       = MD5(pin_as_ascii_string)   // 16 bytes
 *   IV        = 0x00 * 16
 *   plaintext = AES-128-CBC-decrypt(hex_decode(hex), key, IV)
 *   result    = PKCS7-unpad(plaintext)
 *
 * Returns 0 on success, -1 on error.
 */
int pronote_decrypt(const char *hex, const char *pin,
                    char *out_buf, size_t out_len);
