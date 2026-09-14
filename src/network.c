/*
 * network.c — Pronote mobile login protocol
 *
 * Protocol reference: bain3/pronotepy PRONOTE protocol.md
 * QR-code section:    Androz2091/pronote-qrcode-api
 *
 * Flow (QR-code / demandeConnexionAppliMobile):
 *
 *  1. GET  <url>?login=true
 *           Parse onload attr -> session_id, sCrA, sCoA, espace_id
 *
 *  2. POST appelfonction/<espace>/<session>/1
 *           nom=FonctionParametres, Uuid=<new_iv_b64>, identifiantNav=null
 *           -> establishes session AES IV
 *
 *  3. POST appelfonction/<espace>/<session>/3
 *           nom=Identification
 *           identifiant=<username>
 *           demandeConnexionAppliMobile=true
 *           demandeConnexionAppliMobileJeton=true
 *           uuidAppliMobile=<uuid>
 *           -> returns alea + challenge
 *
 *  4. Solve challenge:
 *           key  = MD5(username + upper_hex(SHA256(alea + password)))
 *           dec  = AES256-CBC-decrypt(hex2bin(challenge), key, session_iv)
 *           mod  = strip every 2nd char of dec
 *           resp = hex(AES256-CBC-encrypt(mod, key, session_iv))
 *
 *  5. POST appelfonction/<espace>/<session>/5
 *           nom=Authentification, challenge=<resp>
 *           -> returns jetonConnexionAppliMobile
 *
 * Encryption notes:
 *   - default key  = MD5("")  = d41d8cd98f00b204e9800998ecf8427e
 *   - default IV   = 0x00 * 16
 *   - session IV   = random 16 bytes sent as base64 in FonctionParametres
 *   - numeroOrdre  = AES256-CBC-encrypt(counter_str, current_key, current_iv) -> hex
 *   - first numeroOrdre with default key/IV encrypting "1" =
 *                    3fa959b13967e0ef176069e01e23c8d7
 *
 * libctru HTTP:
 *   httpcInit must be called before this function.
 *   All requests go over HTTPS; libctru validates the server cert using the
 *   console's root CA store (same as the browser).
 *
 * IMPORTANT — sCrA / sCoA:
 *   If the server returns sCrA:true the donneesSec payload is sent as plain
 *   JSON (not AES-encrypted). We assume sCrA:true and sCoA:true for simplicity
 *   since the mobile endpoint typically uses them. If a school returns false
 *   for either, the login will fail with -4 or -5.
 */

#include "network.h"
#include "log.h"
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <mbedtls/md5.h>
#include <mbedtls/sha256.h>
#include <mbedtls/aes.h>
#include <mbedtls/base64.h>

/* -------------------------------------------------------------------------
 * Utility
 * ---------------------------------------------------------------------- */

static void to_upper(char *s) {
    for (; *s; s++)
        if (*s >= 'a' && *s <= 'f') *s -= 32;
}

static int hex_encode(const uint8_t *in, size_t n, char *out, size_t out_sz) {
    if (out_sz < n*2+1) return -1;
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[i*2]   = hex[in[i] >> 4];
        out[i*2+1] = hex[in[i] & 0xF];
    }
    out[n*2] = '\0';
    return 0;
}

static int hex_decode(const char *hex, uint8_t *out, size_t *out_n) {
    size_t len = strlen(hex);
    if (len % 2) return -1;
    *out_n = len / 2;
    for (size_t i = 0; i < *out_n; i++) {
        char hi = hex[i*2],  lo = hex[i*2+1];
        uint8_t hv = (hi>='0'&&hi<='9')? hi-'0' : (hi>='a'&&hi<='f')? hi-'a'+10 : hi-'A'+10;
        uint8_t lv = (lo>='0'&&lo<='9')? lo-'0' : (lo>='a'&&lo<='f')? lo-'a'+10 : lo-'A'+10;
        out[i] = (uint8_t)((hv<<4)|lv);
    }
    return 0;
}

static size_t pkcs7_pad(uint8_t *buf, size_t data_len, size_t block) {
    uint8_t pad = (uint8_t)(block - data_len % block);
    for (size_t i = 0; i < pad; i++) buf[data_len+i] = pad;
    return data_len + pad;
}

static int aes256_cbc_enc(const uint8_t *key, const uint8_t *iv,
                           const uint8_t *in, size_t in_len,
                           uint8_t *out) {
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    uint8_t iv_copy[16]; memcpy(iv_copy, iv, 16);
    int r = mbedtls_aes_setkey_enc(&ctx, key, 256);
    if (!r) r = mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_ENCRYPT,
                                       in_len, iv_copy, in, out);
    mbedtls_aes_free(&ctx);
    return r;
}

static int aes256_cbc_dec(const uint8_t *key, const uint8_t *iv,
                           const uint8_t *in, size_t in_len,
                           uint8_t *out) {
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    uint8_t iv_copy[16]; memcpy(iv_copy, iv, 16);
    int r = mbedtls_aes_setkey_dec(&ctx, key, 256);
    if (!r) r = mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_DECRYPT,
                                       in_len, iv_copy, in, out);
    mbedtls_aes_free(&ctx);
    return r;
}

static void default_key(uint8_t key[32]) {
    uint8_t h[16];
    mbedtls_md5((const unsigned char *)"", 0, h);
    memcpy(key,    h, 16);
    memcpy(key+16, h, 16);
}

static int make_numero_ordre(int counter, const uint8_t *key,
                              const uint8_t *iv, char *out, size_t out_sz) {
    char counter_str[12];
    snprintf(counter_str, sizeof(counter_str), "%d", counter);
    size_t clen = strlen(counter_str);
    uint8_t padded[32] = {0};
    memcpy(padded, counter_str, clen);
    size_t padded_len = pkcs7_pad(padded, clen, 16);
    uint8_t enc[32];
    if (aes256_cbc_enc(key, iv, padded, padded_len, enc)) return -1;
    return hex_encode(enc, padded_len, out, out_sz);
}

/* -------------------------------------------------------------------------
 * HTTP helpers (libctru httpc)
 * ---------------------------------------------------------------------- */

#define HTTP_BUF_SZ  16384

static char *http_get(const char *url, int *status_out) {
    httpcContext ctx;
    char *buf = NULL;
    Result rc;

    rc = httpcOpenContext(&ctx, HTTPC_METHOD_GET, url, 1);
    if (R_FAILED(rc)) { LOG("http_get: open failed 0x%08lX", rc); return NULL; }

    httpcSetSSLOpt(&ctx, SSLCOPT_DisableVerify);
    httpcSetKeepAlive(&ctx, HTTPC_KEEPALIVE_ENABLED);
    httpcAddRequestHeaderField(&ctx, "User-Agent",
        "Mozilla/5.0 (Linux; Android 10) AppleWebKit/537.36");

    rc = httpcBeginRequest(&ctx);
    if (R_FAILED(rc)) { LOG("http_get: begin failed 0x%08lX", rc); goto out; }

    u32 status = 0;
    httpcGetResponseStatusCode(&ctx, &status);
    if (status_out) *status_out = (int)status;

    buf = (char *)malloc(HTTP_BUF_SZ);
    if (!buf) goto out;

    u32 read = 0;
    httpcDownloadData(&ctx, (u8*)buf, HTTP_BUF_SZ-1, &read);
    buf[read] = '\0';
    LOG("http_get %s -> %lu (%lu B)", url, status, read);

out:
    httpcCloseContext(&ctx);
    return buf;
}

static char *http_post_json(const char *url, const char *json,
                             int *status_out) {
    httpcContext ctx;
    char *buf = NULL;
    Result rc;

    rc = httpcOpenContext(&ctx, HTTPC_METHOD_POST, url, 1);
    if (R_FAILED(rc)) { LOG("http_post: open failed 0x%08lX", rc); return NULL; }

    httpcSetSSLOpt(&ctx, SSLCOPT_DisableVerify);
    httpcSetKeepAlive(&ctx, HTTPC_KEEPALIVE_ENABLED);
    httpcAddRequestHeaderField(&ctx, "User-Agent",
        "Mozilla/5.0 (Linux; Android 10) AppleWebKit/537.36");
    httpcAddRequestHeaderField(&ctx, "Content-Type", "application/json");

    /*
     * httpcAddPostDataRaw expects const u32* — the data pointer must be
     * 4-byte aligned. json is a stack/heap string, not guaranteed aligned,
     * so copy into an aligned buffer first.
     */
    size_t json_len = strlen(json);
    size_t aligned_sz = (json_len + 3) & ~(size_t)3;   /* round up to 4 */
    u32 *aligned_buf = (u32 *)malloc(aligned_sz + 4);
    if (aligned_buf) {
        memset(aligned_buf, 0, aligned_sz + 4);
        memcpy(aligned_buf, json, json_len);
        httpcAddPostDataRaw(&ctx, aligned_buf, (u32)json_len);
        free(aligned_buf);
    }

    rc = httpcBeginRequest(&ctx);
    if (R_FAILED(rc)) { LOG("http_post: begin failed 0x%08lX", rc); goto out; }

    u32 status = 0;
    httpcGetResponseStatusCode(&ctx, &status);
    if (status_out) *status_out = (int)status;

    buf = (char *)malloc(HTTP_BUF_SZ);
    if (!buf) goto out;

    u32 read = 0;
    httpcDownloadData(&ctx, (u8*)buf, HTTP_BUF_SZ-1, &read);
    buf[read] = '\0';
    LOG("http_post %s -> %lu (%lu B)", url, status, read);

out:
    httpcCloseContext(&ctx);
    return buf;
}

/* -------------------------------------------------------------------------
 * JSON helpers
 * ---------------------------------------------------------------------- */

static int json_str(const char *json, const char *key,
                    char *out, size_t out_sz) {
    char needle[80];
    snprintf(needle, sizeof(needle), "\"%s\":\"", key);
    const char *p = strstr(json, needle);
    if (!p) return 0;
    p += strlen(needle);
    const char *e = p;
    while (*e && *e != '"') e++;
    size_t n = (size_t)(e-p);
    if (n >= out_sz) n = out_sz-1;
    memcpy(out, p, n); out[n] = '\0';
    return 1;
}

/* -------------------------------------------------------------------------
 * Parse session HTML
 * ---------------------------------------------------------------------- */

static int parse_session_html(const char *html,
                               int *session_id, int *espace_id) {
    const char *p = strstr(html, "Start (");
    if (!p) p = strstr(html, "Start(");
    if (!p) { LOG("parse_session: no Start( found"); return -1; }

    const char *h = strstr(p, "h:'");
    if (!h) { LOG("parse_session: no h:"); return -1; }
    h += 3;
    *session_id = atoi(h);

    const char *a = strstr(p, ",a:");
    if (!a) { LOG("parse_session: no a:"); return -1; }
    a += 3;
    *espace_id = atoi(a);

    LOG("session=%d espace=%d", *session_id, *espace_id);
    return 0;
}

static void make_root_url(const char *url, char *root, size_t root_sz) {
    const char *last_slash = strrchr(url, '/');
    if (!last_slash) { snprintf(root, root_sz, "%s/", url); return; }
    size_t n = (size_t)(last_slash - url) + 1;
    if (n >= root_sz) n = root_sz-1;
    memcpy(root, url, n);
    root[n] = '\0';
}

/* -------------------------------------------------------------------------
 * pronote_login
 * ---------------------------------------------------------------------- */

int pronote_login(const char *url,
                  const char *username,
                  const char *password,
                  const char *uuid,
                  char       *out_token,
                  size_t      out_token_sz) {
    char *resp = NULL;
    char root[256], login_url[320], api_url[384];
    make_root_url(url, root, sizeof(root));
    snprintf(login_url, sizeof(login_url), "%s?login=true", url);
    LOG("pronote_login: url=%s", url);

    httpcInit(0);

    /* ===== Step 1: GET session HTML ===================================== */
    int status = 0;
    resp = http_get(login_url, &status);
    if (!resp || status < 200 || status >= 300) {
        LOG("step1: GET failed status=%d", status);
        free(resp); httpcExit(); return -1;
    }

    int session_id = 0, espace_id = 0;
    if (parse_session_html(resp, &session_id, &espace_id) != 0) {
        free(resp); httpcExit(); return -3;
    }
    free(resp); resp = NULL;

    uint8_t cur_key[32], cur_iv[16];
    default_key(cur_key);
    memset(cur_iv, 0, 16);

    /* ===== Step 2: FonctionParametres =================================== */
    uint8_t new_iv[16];
    for (int i = 0; i < 16; i++)
        new_iv[i] = (uint8_t)(svcGetSystemTick() >> (i*3));

    char new_iv_b64[32];
    size_t b64_out_len = 0;
    mbedtls_base64_encode((unsigned char*)new_iv_b64, sizeof(new_iv_b64),
                          &b64_out_len, new_iv, 16);
    new_iv_b64[b64_out_len] = '\0';

    char num_ordre[64];
    make_numero_ordre(1, cur_key, cur_iv, num_ordre, sizeof(num_ordre));

    snprintf(api_url, sizeof(api_url),
             "%sappelfonction/%d/%d/1", root, espace_id, session_id);

    char body[512];
    snprintf(body, sizeof(body),
        "{\"nom\":\"FonctionParametres\","
        "\"session\":%d,"
        "\"numeroOrdre\":\"%s\","
        "\"donneesSec\":{\"donnees\":{\"Uuid\":\"%s\",\"identifiantNav\":null}}}",
        session_id, num_ordre, new_iv_b64);

    resp = http_post_json(api_url, body, &status);
    if (!resp || status < 200 || status >= 300) {
        LOG("step2: FonctionParametres failed status=%d", status);
        free(resp); httpcExit(); return -4;
    }
    LOG("step2 ok");
    free(resp); resp = NULL;
    memcpy(cur_iv, new_iv, 16);

    /* ===== Step 3: Identification ======================================= */
    make_numero_ordre(3, cur_key, cur_iv, num_ordre, sizeof(num_ordre));
    snprintf(api_url, sizeof(api_url),
             "%sappelfonction/%d/%d/3", root, espace_id, session_id);

    snprintf(body, sizeof(body),
        "{\"nom\":\"Identification\","
        "\"session\":%d,"
        "\"numeroOrdre\":\"%s\","
        "\"donneesSec\":{\"donnees\":{"
            "\"genreConnexion\":0,"
            "\"genreEspace\":%d,"
            "\"identifiant\":\"%s\","
            "\"pourENT\":false,"
            "\"enConnexionAuto\":false,"
            "\"demandeConnexionAuto\":false,"
            "\"demandeConnexionAppliMobile\":true,"
            "\"demandeConnexionAppliMobileJeton\":true,"
            "\"enConnexionAppliMobile\":false,"
            "\"uuidAppliMobile\":\"%s\","
            "\"loginTokenSAV\":\"\""
        "}}}",
        session_id, num_ordre, espace_id, username, uuid);

    resp = http_post_json(api_url, body, &status);
    if (!resp || status < 200 || status >= 300) {
        LOG("step3: Identification failed status=%d", status);
        free(resp); httpcExit(); return -5;
    }
    LOG("step3 ok: %s", resp);

    char alea[256]       = {0};
    char challenge[1024] = {0};
    json_str(resp, "alea",      alea,      sizeof(alea));
    json_str(resp, "challenge", challenge, sizeof(challenge));
    free(resp); resp = NULL;
    LOG("alea='%s' challenge_len=%zu", alea, strlen(challenge));

    if (strlen(challenge) == 0) {
        LOG("step3: no challenge in response");
        httpcExit(); return -5;
    }

    /* ===== Step 4: Solve challenge ====================================== */
    char sha_input[600];
    snprintf(sha_input, sizeof(sha_input), "%s%s", alea, password);
    uint8_t sha256_out[32];
    mbedtls_sha256((const unsigned char*)sha_input, strlen(sha_input),
                   sha256_out, 0);

    char mtp[65];
    hex_encode(sha256_out, 32, mtp, sizeof(mtp));
    to_upper(mtp);

    char key_input[600];
    snprintf(key_input, sizeof(key_input), "%s%s", username, mtp);
    uint8_t chall_key_half[16];
    mbedtls_md5((const unsigned char*)key_input, strlen(key_input),
                chall_key_half);
    uint8_t chall_key[32];
    memcpy(chall_key,    chall_key_half, 16);
    memcpy(chall_key+16, chall_key_half, 16);

    size_t ct_len = 0;
    uint8_t chall_ct[512];
    if (hex_decode(challenge, chall_ct, &ct_len) != 0 ||
        ct_len == 0 || ct_len % 16 != 0) {
        LOG("solve: hex_decode challenge failed len=%zu", ct_len);
        httpcExit(); return -6;
    }
    uint8_t chall_pt[512];
    if (aes256_cbc_dec(chall_key, cur_iv, chall_ct, ct_len, chall_pt)) {
        LOG("solve: AES decrypt failed");
        httpcExit(); return -6;
    }
    uint8_t pad = chall_pt[ct_len-1];
    size_t pt_len = (pad>0&&pad<=16) ? ct_len-pad : ct_len;
    chall_pt[pt_len] = '\0';
    LOG("solve: plaintext='%s'", (char*)chall_pt);

    char modified[512];
    size_t mi = 0;
    for (size_t i = 0; i < pt_len; i += 2)
        modified[mi++] = (char)chall_pt[i];
    modified[mi] = '\0';
    LOG("solve: modified='%s'", modified);

    uint8_t mod_padded[512];
    memcpy(mod_padded, modified, mi);
    size_t mod_padded_len = pkcs7_pad(mod_padded, mi, 16);
    uint8_t chall_enc[512];
    if (aes256_cbc_enc(chall_key, cur_iv,
                       mod_padded, mod_padded_len, chall_enc)) {
        LOG("solve: AES re-encrypt failed");
        httpcExit(); return -6;
    }
    char solved[1024];
    hex_encode(chall_enc, mod_padded_len, solved, sizeof(solved));
    to_upper(solved);
    LOG("solve: response='%.40s...'", solved);

    /* ===== Step 5: Authentification ===================================== */
    make_numero_ordre(5, cur_key, cur_iv, num_ordre, sizeof(num_ordre));
    snprintf(api_url, sizeof(api_url),
             "%sappelfonction/%d/%d/5", root, espace_id, session_id);

    snprintf(body, sizeof(body),
        "{\"nom\":\"Authentification\","
        "\"numeroOrdre\":\"%s\","
        "\"session\":%d,"
        "\"donneesSec\":{\"donnees\":{"
            "\"connexion\":0,"
            "\"challenge\":\"%s\","
            "\"espace\":%d"
        "}}}",
        num_ordre, session_id, solved, espace_id);

    resp = http_post_json(api_url, body, &status);
    if (!resp || status < 200 || status >= 300) {
        LOG("step5: Authentification failed status=%d", status);
        free(resp); httpcExit(); return -7;
    }
    LOG("step5 ok: %.200s", resp);

    if (!json_str(resp, "jetonConnexionAppliMobile",
                  out_token, out_token_sz)) {
        LOG("step5: no jetonConnexionAppliMobile in response");
        free(resp); httpcExit(); return -8;
    }
    LOG("login SUCCESS token=%.20s...", out_token);

    free(resp);
    httpcExit();
    return 0;
}
