#include "typx_sha256.h"

#include <string.h>

static const uint32_t INITIAL_STATE[8] = {
    0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
    0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};

static const uint32_t ROUND_CONSTANTS[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

static uint32_t rotate_right(uint32_t value, unsigned shift) {
  return (value >> shift) | (value << (32u - shift));
}

static uint32_t read_u32_be(const uint8_t *bytes) {
  return ((uint32_t)bytes[0] << 24) |
      ((uint32_t)bytes[1] << 16) |
      ((uint32_t)bytes[2] << 8) |
      (uint32_t)bytes[3];
}

static void write_u32_be(uint8_t *bytes, uint32_t value) {
  bytes[0] = (uint8_t)(value >> 24);
  bytes[1] = (uint8_t)(value >> 16);
  bytes[2] = (uint8_t)(value >> 8);
  bytes[3] = (uint8_t)value;
}

static void process_block(
    typx_sha256_portable_context_t *context,
    const uint8_t block[64]) {
  uint32_t words[64];
  uint32_t a;
  uint32_t b;
  uint32_t c;
  uint32_t d;
  uint32_t e;
  uint32_t f;
  uint32_t g;
  uint32_t h;
  unsigned index;

  for (index = 0; index < 16u; ++index) {
    words[index] = read_u32_be(block + index * 4u);
  }
  for (index = 16u; index < 64u; ++index) {
    uint32_t first = words[index - 15u];
    uint32_t second = words[index - 2u];
    uint32_t sigma0 = rotate_right(first, 7u) ^
        rotate_right(first, 18u) ^ (first >> 3u);
    uint32_t sigma1 = rotate_right(second, 17u) ^
        rotate_right(second, 19u) ^ (second >> 10u);
    words[index] = words[index - 16u] + sigma0 +
        words[index - 7u] + sigma1;
  }

  a = context->state[0];
  b = context->state[1];
  c = context->state[2];
  d = context->state[3];
  e = context->state[4];
  f = context->state[5];
  g = context->state[6];
  h = context->state[7];

  for (index = 0; index < 64u; ++index) {
    uint32_t sum1 = rotate_right(e, 6u) ^
        rotate_right(e, 11u) ^ rotate_right(e, 25u);
    uint32_t choose = (e & f) ^ (~e & g);
    uint32_t temporary1 = h + sum1 + choose +
        ROUND_CONSTANTS[index] + words[index];
    uint32_t sum0 = rotate_right(a, 2u) ^
        rotate_right(a, 13u) ^ rotate_right(a, 22u);
    uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
    uint32_t temporary2 = sum0 + majority;
    h = g;
    g = f;
    f = e;
    e = d + temporary1;
    d = c;
    c = b;
    b = a;
    a = temporary1 + temporary2;
  }

  context->state[0] += a;
  context->state[1] += b;
  context->state[2] += c;
  context->state[3] += d;
  context->state[4] += e;
  context->state[5] += f;
  context->state[6] += g;
  context->state[7] += h;
}

static bool portable_begin(void *opaque) {
  typx_sha256_portable_context_t *context = opaque;
  if (context == NULL) {
    return false;
  }
  memcpy(context->state, INITIAL_STATE, sizeof(INITIAL_STATE));
  context->total_bytes = 0u;
  context->block_bytes = 0u;
  memset(context->block, 0, sizeof(context->block));
  return true;
}

static bool portable_update(
    void *opaque, const uint8_t *data, size_t length) {
  typx_sha256_portable_context_t *context = opaque;
  size_t consumed = 0u;
  if (context == NULL || (length > 0u && data == NULL) ||
      UINT64_MAX - context->total_bytes < length) {
    return false;
  }
  context->total_bytes += length;
  while (consumed < length) {
    size_t available = sizeof(context->block) - context->block_bytes;
    size_t copy = length - consumed < available
        ? length - consumed
        : available;
    memcpy(context->block + context->block_bytes, data + consumed, copy);
    context->block_bytes += copy;
    consumed += copy;
    if (context->block_bytes == sizeof(context->block)) {
      process_block(context, context->block);
      context->block_bytes = 0u;
    }
  }
  return true;
}

static bool portable_finish(void *opaque, uint8_t digest[32]) {
  typx_sha256_portable_context_t *context = opaque;
  uint64_t bit_length;
  unsigned index;
  if (context == NULL || digest == NULL ||
      context->total_bytes > UINT64_MAX / 8u) {
    return false;
  }
  bit_length = context->total_bytes * 8u;
  context->block[context->block_bytes++] = 0x80u;
  if (context->block_bytes > 56u) {
    memset(
        context->block + context->block_bytes,
        0,
        sizeof(context->block) - context->block_bytes);
    process_block(context, context->block);
    context->block_bytes = 0u;
  }
  memset(context->block + context->block_bytes, 0, 56u - context->block_bytes);
  for (index = 0; index < 8u; ++index) {
    context->block[63u - index] = (uint8_t)(bit_length >> (index * 8u));
  }
  process_block(context, context->block);
  for (index = 0; index < 8u; ++index) {
    write_u32_be(digest + index * 4u, context->state[index]);
  }
  return true;
}

typx_sha256_provider_t typx_sha256_portable_provider(
    typx_sha256_portable_context_t *context) {
  typx_sha256_provider_t provider = {
      context, portable_begin, portable_update, portable_finish};
  return provider;
}
