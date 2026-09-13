/*
 * crypto.c -- Pronote QR jeton decryption
 *
 * Protocol (from pronotepy source):
 *
 *   key  = MD5( pin )          -- MD5 of the 4-digit PIN string, e.g. "1234"
 *   iv   = MD5( login )        -- MD5 of the login hex UUID string from the QR
 *   ct   = hex_decode( jeton ) -- jeton is a hex-encoded AES-128-CBC ciphertext
 *   pt   = AES-128-CBC-Decrypt( key, iv, ct )
 *   pass = PKCS7-strip( pt )   -- the decrypted Pronote password
 *
 * Common failure modes:
 *   - Wrong PIN          -> wrong key -> garbage plaintext -> bad PKCS7 pad
 *   - login case mismatch-> wrong IV  -> first block wrong, rest may be OK
 *                           but PKCS7 check on last block will still fail
 *   - jeton truncated    -> ct_len not multiple of 16 -> caught early
 */

#include "crypto.h"
#include "log.h"
#include "../lib/crypto/md5.h"
#include "../lib/crypto/aes.h"
#include <stdlib.h>
#include <string.h>

static int hex_decode(const char *hex, uint8_t *out, size_t out_len) {
    size_t hlen = strlen(hex);
    if (hlen % 2 != 0) { LOG("hex_decode: odd length %zu", hlen); return -1; }
    size_t n = hlen / 2;
    if (n > out_len) { LOG("hex_decode: need %zu bytes, have %zu", n, out_len); return -1; }
    for (size_t i = 0; i < n; i++) {
        char hi = hex[i*2], lo = hex[i*2+1];
        uint8_t hv, lv;
        if      (hi>='0'&&hi<='9') hv=hi-'0';
        else if (hi>='a'&&hi<='f') hv=hi-'a'+10;
        else if (hi>='A'&&hi<='F') hv=hi-'A'+10;
        else { LOG("hex_decode: bad char '%c' at %zu", hi, i*2); return -1; }
        if      (lo>='0'&&lo<='9') lv=lo-'0';
        else if (lo>='a'&&lo<='f') lv=lo-'a'+10;
        else if (lo>='A'&&lo<='F') lv=lo-'A'+10;
        else { LOG("hex_decode: bad char '%c' at %zu", lo, i*2+1); return -1; }
        out[i] = (hv << 4) | lv;
    }
    return (int)n;
}

int pronote_decrypt_jeton(const char *pin, const char *login,
                          const char *jeton, char *out, size_t out_max) {
    LOG("crypto: pin='%s' pin_len=%zu", pin, strlen(pin));
    LOG("crypto: login='%.32s...' login_len=%zu", login, strlen(login));
    LOG("crypto: jeton_len=%zu", strlen(jeton));

    /* key = MD5(pin) */
    uint8_t key[16];
    md5((const uint8_t *)pin, strlen(pin), key);
    LOG("crypto: key=%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
        key[0],key[1],key[2],key[3],key[4],key[5],key[6],key[7],
        key[8],key[9],key[10],key[11],key[12],key[13],key[14],key[15]);

    /* iv = MD5(login) */
    uint8_t iv[16];
    md5((const uint8_t *)login, strlen(login), iv);
    LOG("crypto: iv=%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
        iv[0],iv[1],iv[2],iv[3],iv[4],iv[5],iv[6],iv[7],
        iv[8],iv[9],iv[10],iv[11],iv[12],iv[13],iv[14],iv[15]);

    /* hex-decode jeton */
    size_t jlen = strlen(jeton);
    if (jlen == 0 || jlen % 2 != 0) {
        LOG("crypto: bad jeton length %zu", jlen); return 0;
    }
    size_t ct_len = jlen / 2;
    if (ct_len % 16 != 0) {
        LOG("crypto: ct_len %zu not multiple of 16", ct_len); return 0;
    }

    uint8_t *ct = (uint8_t *)malloc(ct_len);
    if (!ct) { LOG("crypto: malloc failed"); return 0; }

    int decoded = hex_decode(jeton, ct, ct_len);
    if (decoded < 0 || (size_t)decoded != ct_len) {
        LOG("crypto: hex_decode failed (decoded=%d expected=%zu)", decoded, ct_len);
        free(ct); return 0;
    }
    LOG("crypto: ct_len=%zu blocks=%zu", ct_len, ct_len/16);

    /* AES-128-CBC decrypt */
    AES128_CTX aes;
    aes128_init(&aes, key);

    uint8_t prev[16];
    memcpy(prev, iv, 16);

    for (size_t b = 0; b < ct_len; b += 16) {
        uint8_t tmp[16];
        memcpy(tmp, ct + b, 16);
        aes128_decrypt_block(&aes, ct + b);
        for (int i = 0; i < 16; i++) ct[b + i] ^= prev[i];
        memcpy(prev, tmp, 16);
    }

    /* Log first 32 bytes of plaintext before PKCS7 strip */
    LOG("crypto: pt[0..15]=%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
        ct[0],ct[1],ct[2],ct[3],ct[4],ct[5],ct[6],ct[7],
        ct[8],ct[9],ct[10],ct[11],ct[12],ct[13],ct[14],ct[15]);
    LOG("crypto: pt last 16: pad_candidate=0x%02x", ct[ct_len-1]);

    /* PKCS7 strip */
    uint8_t pad = ct[ct_len - 1];
    if (pad == 0 || pad > 16) {
        LOG("crypto: bad PKCS7 pad=0x%02x (wrong PIN or IV?)", pad);
        free(ct); return 0;
    }
    for (size_t i = ct_len - pad; i < ct_len; i++) {
        if (ct[i] != pad) {
            LOG("crypto: PKCS7 mismatch at %zu: got 0x%02x expected 0x%02x",
                i, ct[i], pad);
            free(ct); return 0;
        }
    }

    size_t pt_len = ct_len - pad;
    LOG("crypto: pt_len=%zu pad=%u", pt_len, pad);

    if (pt_len >= out_max) {
        LOG("crypto: output too small (%zu >= %zu)", pt_len, out_max);
        free(ct); return 0;
    }

    memcpy(out, ct, pt_len);
    out[pt_len] = '\0';
    free(ct);

    LOG("crypto: password='%s'", out);
    return 1;
}
