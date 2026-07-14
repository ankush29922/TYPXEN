#ifdef ESP_PLATFORM

#include "typx_sha256.h"

static bool mbedtls_begin(void *opaque) {
  mbedtls_sha256_context *context = opaque;
  if (context == NULL) {
    return false;
  }
  mbedtls_sha256_init(context);
  return mbedtls_sha256_starts(context, 0) == 0;
}

static bool mbedtls_update(
    void *opaque, const uint8_t *data, size_t length) {
  return opaque != NULL &&
      mbedtls_sha256_update((mbedtls_sha256_context *)opaque, data, length) == 0;
}

static bool mbedtls_finish(void *opaque, uint8_t digest[32]) {
  int result;
  if (opaque == NULL || digest == NULL) {
    return false;
  }
  result = mbedtls_sha256_finish((mbedtls_sha256_context *)opaque, digest);
  mbedtls_sha256_free((mbedtls_sha256_context *)opaque);
  return result == 0;
}

typx_sha256_provider_t typx_sha256_mbedtls_provider(
    mbedtls_sha256_context *context) {
  typx_sha256_provider_t provider = {
      context, mbedtls_begin, mbedtls_update, mbedtls_finish};
  return provider;
}

#endif
