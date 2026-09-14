/*
 * Minimal MD5 — public domain
 * Produces a 16-byte digest from arbitrary input.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>

void md5(const uint8_t *data, size_t len, uint8_t digest[16]);
