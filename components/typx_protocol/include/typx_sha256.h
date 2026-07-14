#ifndef TYPX_SHA256_H
#define TYPX_SHA256_H

#include "typx_protocol.h"

typedef struct {
  uint32_t state[8];
  uint64_t total_bytes;
  uint8_t block[64];
  size_t block_bytes;
} typx_sha256_portable_context_t;

typx_sha256_provider_t typx_sha256_portable_provider(
    typx_sha256_portable_context_t *context);

#ifdef ESP_PLATFORM
#include "mbedtls/sha256.h"

typx_sha256_provider_t typx_sha256_mbedtls_provider(
    mbedtls_sha256_context *context);
#endif

#endif
