#ifndef CRYPTO_H
#define CRYPTO_H

#include <stddef.h>

/*
 * pronote_decrypt_jeton
 *
 * Implements the Pronote QR login decryption as documented in the pronotepy
 * protocol (https://github.com/bain3/pronotepy/blob/main/PRONOTE%20protocol.md):
 *
 *   key  = MD5(pin)               -- 16 bytes
 *   iv   = MD5(login)             -- 16 bytes, where login is the hex UUID string
 *   ct   = hex_decode(jeton)      -- ciphertext bytes
 *   pt   = AES-128-CBC-decrypt(key, iv, ct)
 *   password = strip_pkcs7(pt)
 *
 * Parameters:
 *   pin      [in]  4-digit PIN as a null-terminated string (e.g. "1234")
 *   login    [in]  login hex UUID from user.json (e.g. "D733111B...")
 *   jeton    [in]  hex-encoded ciphertext from user.json
 *   out      [out] decrypted password (null-terminated)
 *   out_max  [in]  size of out buffer
 *
 * Returns 1 on success, 0 on failure (bad hex, bad padding, buffer too small).
 */
int pronote_decrypt_jeton(const char *pin, const char *login,
                          const char *jeton, char *out, size_t out_max);

#endif /* CRYPTO_H */
