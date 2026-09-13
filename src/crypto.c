/*
 * crypto.c -- Pronote QR jeton decryption
 *
 * Protocol (from pronotepy/PRONOTE protocol.md, QR code login section):
 *
 *   key = MD5(pin)          -- MD5 of the 4-digit PIN string, e.g. "1234"
 *   iv  = 16 zero bytes     -- NOT MD5(login); the login field is the
 *                              decrypted username, not an IV source
 *   ct  = hex_decode(jeton) -- jeton and login are both AES-128-CBC
 *                              ciphertexts from the QR JSON
 *   pt  = AES-128-CBC-Decrypt(key, iv, ct)
 *   result = PKCS7-strip(pt)
 *
 * Both "login" and "jeton" fields in the QR JSON are encrypted with the
 * same key/iv. Decrypting "jeton" gives the Pronote password; decrypting
 * "login" gives the Pronote username.
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
    if (n > out_len) { LOG("hex_decode: need %zu have %zu", n, out_len); return -1; }
    for (size_t i = 0; i < n; i++) {
        char hi = hex[i*2], lo = hex[i*2+1];
        uint8_t hv, lv;
        if      (hi>='0'&&hi<='9') hv=hi-'0';
        else if (hi>='a'&&hi<='f') hv=hi-'a'+10;
        else if (hi>='A'&&hi<='F') hv=hi-'A'+10;
        else { LOG("hex_decode: bad char '%c'", hi); return -1; }
        if      (lo>='0'&&lo<='9') lv=lo-'0';
        else if (lo>='a'&&lo<='f') lv=lo-'a'+10;
        else if (lo>='A'&&lo<='F') lv=lo-'A'+10;
        else { LOG("hex_decode: bad char '%c'", lo); return -1; }
        out[i] = (hv << 4) | lv;
    }
    return (int)n;
}

static int aes_cbc_decrypt(const uint8_t key[16], const uint8_t iv[16],
                            const char *hex_ct, char *out, size_t out_max) {
    size_t hlen = strlen(hex_ct);
    if (hlen == 0 || hlen % 2 != 0) {
        LOG("aes_cbc_decrypt: bad hex length %zu", hlen); return 0;
    }
    size_t ct_len = hlen / 2;
    if (ct_len % 16 != 0) {
        LOG("aes_cbc_decrypt: ct_len %zu not multiple of 16", ct_len); return 0;
    }

    uint8_t *ct = (uint8_t *)malloc(ct_len);
    if (!ct) { LOG("aes_cbc_decrypt: malloc failed"); return 0; }

    if (hex_decode(hex_ct, ct, ct_len) != (int)ct_len) {
        free(ct); return 0;
    }

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

    LOG("aes_cbc_decrypt: pt[0..15]=%02x%02x%02x%02x%02x%02x%02x%02x"
        "%02x%02x%02x%02x%02x%02x%02x%02x",
        ct[0],ct[1],ct[2],ct[3],ct[4],ct[5],ct[6],ct[7],
        ct[8],ct[9],ct[10],ct[11],ct[12],ct[13],ct[14],ct[15]);

    uint8_t pad = ct[ct_len - 1];
    LOG("aes_cbc_decrypt: pad=0x%02x", pad);
    if (pad == 0 || pad > 16) {
        LOG("aes_cbc_decrypt: bad PKCS7 pad"); free(ct); return 0;
    }
    for (size_t i = ct_len - pad; i < ct_len; i++) {
        if (ct[i] != pad) {
            LOG("aes_cbc_decrypt: PKCS7 mismatch at %zu", i);
            free(ct); return 0;
        }
    }

    size_t pt_len = ct_len - pad;
    if (pt_len >= out_max) {
        LOG("aes_cbc_decrypt: output too small"); free(ct); return 0;
    }
    memcpy(out, ct, pt_len);
    out[pt_len] = '\0';
    free(ct);
    return 1;
}

int pronote_decrypt_jeton(const char *pin, const char *login,
                          const char *jeton, char *out, size_t out_max) {
    /* key = MD5(pin) */
    uint8_t key[16];
    md5((const uint8_t *)pin, strlen(pin), key);
    LOG("crypto: key=%02x%02x%02x%02x%02x%02x%02x%02x"
        "%02x%02x%02x%02x%02x%02x%02x%02x",
        key[0],key[1],key[2],key[3],key[4],key[5],key[6],key[7],
        key[8],key[9],key[10],key[11],key[12],key[13],key[14],key[15]);

    /* iv = 16 zero bytes (per PRONOTE protocol spec) */
    uint8_t iv[16] = {0};

    /* Decrypt jeton -> password */
    LOG("crypto: decrypting jeton (len=%zu)", strlen(jeton));
    if (!aes_cbc_decrypt(key, iv, jeton, out, out_max)) {
        LOG("crypto: jeton decrypt failed"); return 0;
    }
    LOG("crypto: password='%s'", out);

    /* Also decrypt login -> username (logged for info) */
    char username[128] = {0};
    if (aes_cbc_decrypt(key, iv, login, username, sizeof(username)))
        LOG("crypto: username='%s'", username);
    else
        LOG("crypto: login decrypt failed (non-fatal)");

    return 1;
}
