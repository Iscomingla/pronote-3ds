/*
 * crypto.c -- Pronote QR jeton decryption
 *
 * Protocol (from pronotepy/PRONOTE protocol.md):
 *
 *   key  = MD5( pin_string )
 *   iv   = MD5( login_hex_string )
 *   ct   = hex_decode( jeton_hex_string )
 *   pt   = AES-128-CBC-Decrypt( key, iv, ct )
 *   pass = PKCS7-strip( pt )
 *
 * The "login" field in the QR JSON is a 32-char hex UUID string.
 * MD5 is computed on the *string bytes*, not on the decoded UUID bytes.
 *
 * Example (from confirmed payload in TOFIX.md):
 *   login = "D733111BDFB5BE004343EE8F18FD0E1F"
 *   jeton = "91472F67A2174D0A..." (hex, even number of chars)
 *   pin   = "1234" (user's 4-digit PIN)
 */

#include "crypto.h"
#include "log.h"
#include "../lib/crypto/md5.h"
#include "../lib/crypto/aes.h"
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * hex_decode: convert hex string to bytes.
 * Returns number of bytes written, or -1 on bad input.
 * out_len must be >= strlen(hex)/2.
 * --------------------------------------------------------------------------- */
static int hex_decode(const char *hex, uint8_t *out, size_t out_len) {
    size_t hlen = strlen(hex);
    if (hlen % 2 != 0) return -1;
    size_t n = hlen / 2;
    if (n > out_len) return -1;
    for (size_t i = 0; i < n; i++) {
        char hi = hex[i*2], lo = hex[i*2+1];
        uint8_t hv, lv;
        if      (hi>='0'&&hi<='9') hv=hi-'0';
        else if (hi>='a'&&hi<='f') hv=hi-'a'+10;
        else if (hi>='A'&&hi<='F') hv=hi-'A'+10;
        else return -1;
        if      (lo>='0'&&lo<='9') lv=lo-'0';
        else if (lo>='a'&&lo<='f') lv=lo-'a'+10;
        else if (lo>='A'&&lo<='F') lv=lo-'A'+10;
        else return -1;
        out[i] = (hv << 4) | lv;
    }
    return (int)n;
}

/* ---------------------------------------------------------------------------
 * pronote_decrypt_jeton
 * --------------------------------------------------------------------------- */
int pronote_decrypt_jeton(const char *pin, const char *login,
                          const char *jeton, char *out, size_t out_max) {
    /* --- key = MD5(pin) --- */
    uint8_t key[16];
    md5((const uint8_t *)pin, strlen(pin), key);

    /* --- iv = MD5(login) --- */
    uint8_t iv[16];
    md5((const uint8_t *)login, strlen(login), iv);

    LOG("crypto: key = %02x%02x%02x%02x...", key[0],key[1],key[2],key[3]);
    LOG("crypto: iv  = %02x%02x%02x%02x...", iv[0], iv[1], iv[2], iv[3]);

    /* --- hex-decode jeton --- */
    size_t jlen = strlen(jeton);
    if (jlen == 0 || jlen % 2 != 0) {
        LOG("crypto: bad jeton length %zu", jlen);
        return 0;
    }
    size_t ct_len = jlen / 2;
    if (ct_len % 16 != 0) {
        LOG("crypto: ciphertext length %zu not a multiple of 16", ct_len);
        return 0;
    }

    uint8_t *ct = (uint8_t *)malloc(ct_len);
    if (!ct) { LOG("crypto: malloc failed"); return 0; }

    int decoded = hex_decode(jeton, ct, ct_len);
    if (decoded < 0 || (size_t)decoded != ct_len) {
        LOG("crypto: hex_decode failed (decoded=%d expected=%zu)", decoded, ct_len);
        free(ct);
        return 0;
    }

    /* --- AES-128-CBC decrypt --- */
    AES128_CTX aes;
    aes128_init(&aes, key);

    uint8_t prev[16];
    memcpy(prev, iv, 16);

    for (size_t b = 0; b < ct_len; b += 16) {
        uint8_t tmp[16];
        memcpy(tmp, ct + b, 16);        /* save ciphertext block for next IV */
        aes128_decrypt_block(&aes, ct + b);
        for (int i = 0; i < 16; i++) ct[b + i] ^= prev[i];  /* XOR with IV */
        memcpy(prev, tmp, 16);
    }

    /* --- strip PKCS7 padding --- */
    uint8_t pad = ct[ct_len - 1];
    if (pad == 0 || pad > 16) {
        LOG("crypto: bad PKCS7 pad value %u", (unsigned)pad);
        free(ct);
        return 0;
    }
    /* Verify all pad bytes */
    for (size_t i = ct_len - pad; i < ct_len; i++) {
        if (ct[i] != pad) {
            LOG("crypto: PKCS7 pad mismatch at byte %zu (got %02x expected %02x)",
                i, ct[i], pad);
            free(ct);
            return 0;
        }
    }

    size_t pt_len = ct_len - pad;
    LOG("crypto: decrypted %zu bytes, pad=%u, pt_len=%zu", ct_len, pad, pt_len);

    if (pt_len >= out_max) {
        LOG("crypto: output buffer too small (%zu >= %zu)", pt_len, out_max);
        free(ct);
        return 0;
    }

    memcpy(out, ct, pt_len);
    out[pt_len] = '\0';
    free(ct);

    LOG("crypto: password = '%s'", out);
    return 1;
}
