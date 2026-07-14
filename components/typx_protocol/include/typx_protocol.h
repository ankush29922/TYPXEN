#ifndef TYPX_PROTOCOL_H
#define TYPX_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TYPX_PROTOCOL_V1_HEADER_BYTES 96u
#define TYPX_PROTOCOL_V1_RECORD_BYTES 32u
#define TYPX_PROTOCOL_V1_GENERIC_MAX_RECORDS 250000u
#define TYPX_PROTOCOL_V1_GENERIC_MAX_BYTES 8000096u
#define TYPX_PROTOCOL_V1_MAX_DELAY_US 600000000u
#define TYPX_ESP32_CAM_MAX_SCHEDULE_BYTES (512u * 1024u)
#define TYPX_ESP32_CAM_MAX_RECORDS 16381u

#define TYPX_RECORD_FLAG_SOURCE 0x0001u
#define TYPX_RECORD_FLAG_TECHNICAL 0x0002u
#define TYPX_RECORD_FLAG_FINAL_CLEANUP 0x0004u
#define TYPX_RECORD_FLAG_RELEASE_ALL 0x0008u

typedef enum {
  TYPX_PROTOCOL_OK = 0,
  TYPX_PROTOCOL_READER_ERROR,
  TYPX_PROTOCOL_HEADER_INVALID,
  TYPX_PROTOCOL_VERSION_INCOMPATIBLE,
  TYPX_PROTOCOL_PAYLOAD_SIZE_INVALID,
  TYPX_PROTOCOL_ACTION_COUNT_MISMATCH,
  TYPX_PROTOCOL_SCHEDULE_LIMIT_EXCEEDED,
  TYPX_PROTOCOL_CHECKSUM_MISMATCH,
  TYPX_PROTOCOL_FLAGS_INVALID,
  TYPX_PROTOCOL_RESERVED_FIELD_NONZERO,
  TYPX_PROTOCOL_JOB_ID_INVALID,
  TYPX_PROTOCOL_DELAY_INVALID,
  TYPX_PROTOCOL_MODIFIER_INVALID,
  TYPX_PROTOCOL_KEYCODE_UNSUPPORTED,
  TYPX_PROTOCOL_SOURCE_INDEX_INVALID,
  TYPX_PROTOCOL_TOTAL_DURATION_INVALID,
  TYPX_PROTOCOL_FINAL_CLEANUP_INVALID,
  TYPX_PROTOCOL_NOT_VERIFIED,
  TYPX_PROTOCOL_INTEGER_OVERFLOW
} typx_protocol_error_t;

typedef bool (*typx_reader_read_at_fn)(
    void *context, uint64_t offset, uint8_t *destination, size_t length);

typedef struct {
  void *context;
  uint64_t size_bytes;
  typx_reader_read_at_fn read_at;
} typx_reader_t;

typedef bool (*typx_sha256_begin_fn)(void *context);
typedef bool (*typx_sha256_update_fn)(
    void *context, const uint8_t *data, size_t length);
typedef bool (*typx_sha256_finish_fn)(void *context, uint8_t digest[32]);

typedef struct {
  void *context;
  typx_sha256_begin_fn begin;
  typx_sha256_update_fn update;
  typx_sha256_finish_fn finish;
} typx_sha256_provider_t;

typedef struct {
  uint64_t max_encoded_bytes;
  uint32_t max_record_count;
  uint32_t max_individual_delay_us;
} typx_protocol_limits_t;

typedef struct {
  uint8_t protocol_major;
  uint8_t protocol_minor;
  uint8_t schedule_format_version;
  uint32_t action_count;
  uint32_t payload_bytes;
  uint32_t total_source_characters;
  uint64_t total_expected_duration_us;
  uint8_t job_id[16];
  uint8_t payload_sha256[32];
} typx_protocol_header_v1_t;

typedef struct {
  uint32_t wait_before_key_down_us;
  uint32_t key_down_hold_us;
  uint32_t wait_after_release_us;
  uint32_t source_index;
  uint8_t modifier;
  uint8_t keycode;
  uint16_t flags;
} typx_protocol_record_v1_t;

typedef struct {
  uint32_t verification_token;
  typx_reader_t reader;
  typx_protocol_limits_t limits;
  typx_protocol_header_v1_t header;
} typx_verified_schedule_v1_t;

typx_protocol_limits_t typx_protocol_v1_generic_limits(void);
typx_protocol_limits_t typx_protocol_v1_esp32_cam_limits(void);

typx_protocol_error_t typx_protocol_v1_verify(
    const typx_reader_t *reader,
    const typx_protocol_limits_t *limits,
    const typx_sha256_provider_t *sha256,
    typx_verified_schedule_v1_t *verified_schedule);

bool typx_protocol_v1_is_verified(
    const typx_verified_schedule_v1_t *schedule);

typx_protocol_error_t typx_protocol_v1_read_record(
    const typx_verified_schedule_v1_t *schedule,
    uint32_t record_index,
    typx_protocol_record_v1_t *record);

const char *typx_protocol_error_code(typx_protocol_error_t error);

#ifdef __cplusplus
}
#endif

#endif
