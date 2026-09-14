/*
 * crypto.c — Pronote jeton decryption
 *
 * Protocol (Androz2091/pronote-qrcode-api):
 *   key       = MD5(pin_string)      // 16 bytes
 *   IV        = 0x00 * 16
 *   plaintext = AES-128-CBC-decrypt(hex_decode(jeton), key, IV)
 *   result    = PKCS7-unpad(plaintext)
 *
 * Both the login field and the jeton field are separately decryptable
 * with the same key/IV. Decrypting login yields the Pronote username;
 * decrypting jeton yields the Pronote password.
 *
 * Previous implementation used a hand-rolled AES that had a broken
 * InvMixColumns — the GF(2^8) multiply-by-{0e,0b,0d,09} was wrong,
 * producing incorrect plaintext on every block. Replaced with mbedTLS
 * which is available via pacman -S 3ds-mbedtls and is verified correct.
 */

#include "crypto.h"
#include "log.h"
#include <mbedtls/md5.h>
#include <mbedtls/aes.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

static int hexnibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int hex_decode(const char *hex, size_t hex_len,
                      uint8_t *out, size_t out_size) {
    if (hex_len % 2 != 0) return -1;
    size_t n = hex_len / 2;
    if (n > out_size) return -1;
    for (size_t i = 0; i < n; i++) {
        int hi = hexnibble(hex[i*2]);
        int lo = hexnibble(hex[i*2 + 1]);
        if (hi < 0 || lo < 0) return -1;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return (int)n;
}

int pronote_decrypt(const char *hex, const char *pin,
                    char *out_buf, size_t out_len) {
    if (!hex || !pin || !out_buf || out_len == 0) return -1;

    size_t hex_len = strlen(hex);
    if (hex_len == 0 || hex_len % 2 != 0) {
        LOG("crypto: bad hex length %zu", hex_len);
        return -1;
    }

    size_t ct_len = hex_len / 2;
    if (ct_len % 16 != 0 || ct_len == 0) {
        LOG("crypto: ciphertext not block-aligned (%zu bytes)", ct_len);
        return -1;
    }

    /* Decode hex ciphertext */
    uint8_t *ct = (uint8_t *)malloc(ct_len);
    if (!ct) return -1;

    if (hex_decode(hex, hex_len, ct, ct_len) < 0) {
        LOG("crypto: hex decode failed");
        free(ct); return -1;
    }

    /* key = MD5(pin) */
    uint8_t key[16];
    mbedtls_md5((const unsigned char *)pin, strlen(pin), key);

    {
        char keyhex[33];
        for (int i = 0; i < 16; i++)
            snprintf(keyhex + i*2, 3, "%02x", key[i]);
        LOG("crypto: key=%s", keyhex);
    }

    /* IV = 16 zero bytes */
    uint8_t iv[16];
    memset(iv, 0, sizeof(iv));

    /* AES-128-CBC decrypt via mbedTLS */
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);

    int ret = mbedtls_aes_setkey_dec(&aes, key, 128);
    if (ret != 0) {
        LOG("crypto: mbedtls_aes_setkey_dec failed (%d)", ret);
        mbedtls_aes_free(&aes);
        free(ct); return -1;
    }

    ret = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT,
                                  ct_len, iv, ct, ct);
    mbedtls_aes_free(&aes);

    if (ret != 0) {
        LOG("crypto: mbedtls_aes_crypt_cbc failed (%d)", ret);
        free(ct); return -1;
    }

    {
        char pt_hex[33];
        for (int i = 0; i < 16; i++)
            snprintf(pt_hex + i*2, 3, "%02x", ct[i]);
        LOG("crypto: pt[0..15]=%s", pt_hex);
    }

    /* PKCS#7 unpad */
    uint8_t pad = ct[ct_len - 1];
    LOG("crypto: pad=0x%02x", pad);

    if (pad == 0 || pad > 16) {
        LOG("crypto: invalid PKCS7 pad value %u", pad);
        free(ct); return -1;
    }
    for (size_t i = ct_len - pad; i < ct_len; i++) {
        if (ct[i] != pad) {
            LOG("crypto: PKCS7 mismatch at byte %zu (got 0x%02x, want 0x%02x)",
                i, ct[i], pad);
            free(ct); return -1;
        }
    }

    size_t pt_len = ct_len - pad;
    if (pt_len + 1 > out_len) {
        LOG("crypto: output too small (%zu needed)", pt_len + 1);
        free(ct); return -1;
    }

    memcpy(out_buf, ct, pt_len);
    out_buf[pt_len] = '\0';
    free(ct);

    LOG("crypto: decrypt OK (%zu bytes plaintext)", pt_len);
    return 0;
}
