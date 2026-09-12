/*
 * crypto.c — Pronote jeton decryption
 *
 * Protocol (confirmed from Pronote web client source):
 *   ciphertext = hex-decode(jeton)
 *   key        = MD5(pin_string)        // 16 bytes
 *   IV         = 0x00 * 16
 *   plaintext  = AES-128-CBC-decrypt(ciphertext, key, IV)
 *   result     = PKCS7-unpad(plaintext)
 */

#include "crypto.h"
#include "log.h"
#include "../lib/aes/aes.h"
#include "../lib/md5/md5.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/* Decode a single hex nibble; returns -1 on invalid char */
static int hexnibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int pronote_decrypt_jeton(const char *jeton_hex, const char *pin,
                          char *out_buf, size_t out_len) {
    if (!jeton_hex || !pin || !out_buf || out_len == 0) return -1;

    size_t hex_len = strlen(jeton_hex);
    if (hex_len == 0 || hex_len % 2 != 0) {
        LOG("crypto: bad hex length %zu", hex_len);
        return -1;
    }

    size_t ct_len = hex_len / 2;
    if (ct_len % 16 != 0) {
        LOG("crypto: ciphertext not block-aligned (%zu bytes)", ct_len);
        return -1;
    }

    /* Decode hex */
    uint8_t *ct = (uint8_t *)malloc(ct_len);
    if (!ct) return -1;
    for (size_t i = 0; i < ct_len; i++) {
        int hi = hexnibble(jeton_hex[i*2]);
        int lo = hexnibble(jeton_hex[i*2+1]);
        if (hi < 0 || lo < 0) {
            LOG("crypto: invalid hex char at position %zu", i*2);
            free(ct);
            return -1;
        }
        ct[i] = (uint8_t)((hi << 4) | lo);
    }

    /* key = MD5(pin) */
    uint8_t key[16];
    md5((const uint8_t *)pin, strlen(pin), key);

    /* IV = 16 zero bytes */
    uint8_t iv[16];
    memset(iv, 0, sizeof(iv));

    /* AES-128-CBC decrypt in-place */
    struct AES_ctx ctx;
    AES_init_ctx_iv(&ctx, key, iv);
    AES_CBC_decrypt_buffer(&ctx, ct, ct_len);

    /* PKCS#7 unpad */
    uint8_t pad = ct[ct_len - 1];
    if (pad == 0 || pad > 16) {
        LOG("crypto: invalid PKCS7 pad value %u", pad);
        free(ct);
        return -1;
    }
    for (size_t i = ct_len - pad; i < ct_len; i++) {
        if (ct[i] != pad) {
            LOG("crypto: PKCS7 pad mismatch at byte %zu", i);
            free(ct);
            return -1;
        }
    }
    size_t pt_len = ct_len - pad;

    if (pt_len + 1 > out_len) {
        LOG("crypto: output buffer too small (%zu needed, %zu available)",
            pt_len + 1, out_len);
        free(ct);
        return -1;
    }

    memcpy(out_buf, ct, pt_len);
    out_buf[pt_len] = '\0';
    free(ct);

    LOG("crypto: jeton decrypted OK (%zu bytes plaintext)", pt_len);
    return 0;
}
