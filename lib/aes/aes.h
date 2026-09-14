/*
 * Tiny public-domain AES-128 implementation (ECB block only).
 * Source: https://github.com/kokke/tiny-AES-c (unlicense)
 * Only the parts needed for CBC are kept.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>

#define AES_BLOCKLEN 16
#define AES_KEYLEN   16

struct AES_ctx {
    uint8_t RoundKey[176];
    uint8_t Iv[AES_BLOCKLEN];
};

void AES_init_ctx_iv(struct AES_ctx *ctx, const uint8_t *key, const uint8_t *iv);
void AES_CBC_decrypt_buffer(struct AES_ctx *ctx, uint8_t *buf, size_t length);
