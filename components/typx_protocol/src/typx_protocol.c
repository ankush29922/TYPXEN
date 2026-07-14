#include "typx_protocol.h"

#include <limits.h>
#include <string.h>

#define TYPX_VERIFICATION_TOKEN 0x54595058u
#define TYPX_HEADER_REQUIRED_FLAGS 0x00000003u
#define TYPX_RECORD_ALLOWED_FLAGS 0x000fu
#define TYPX_ACTION_KIND_KEYSTROKE 1u
#define TYPX_PROGRESS_AFTER_RELEASE 1u

static const uint8_t TYPX_MAGIC[8] = {
    0x54, 0x59, 0x50, 0x58, 0x48, 0x49, 0x44, 0x31};

static uint16_t read_u16_le(const uint8_t *bytes) {
  return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static uint32_t read_u32_le(const uint8_t *bytes) {
  return (uint32_t)bytes[0] |
      ((uint32_t)bytes[1] << 8) |
      ((uint32_t)bytes[2] << 16) |
      ((uint32_t)bytes[3] << 24);
}

static uint64_t read_u64_le(const uint8_t *bytes) {
  return (uint64_t)read_u32_le(bytes) |
      ((uint64_t)read_u32_le(bytes + 4) << 32);
}

static bool read_exact(
    const typx_reader_t *reader,
    uint64_t offset,
    uint8_t *destination,
    size_t length) {
  if (reader == NULL || reader->read_at == NULL || destination == NULL) {
    return false;
  }
  if (offset > reader->size_bytes ||
      (uint64_t)length > reader->size_bytes - offset) {
    return false;
  }
  return reader->read_at(reader->context, offset, destination, length);
}

static bool all_zero(const uint8_t *bytes, size_t length) {
  size_t index;
  for (index = 0; index < length; ++index) {
    if (bytes[index] != 0u) {
      return false;
    }
  }
  return true;
}

static bool supported_keycode(uint8_t keycode) {
  return (keycode >= 0x04u && keycode <= 0x31u) ||
      (keycode >= 0x33u && keycode <= 0x38u) ||
      keycode == 0x4au || keycode == 0x4cu ||
      keycode == 0x4du || keycode == 0x4fu;
}

static typx_protocol_error_t decode_record(
    const uint8_t bytes[TYPX_PROTOCOL_V1_RECORD_BYTES],
    uint32_t max_delay_us,
    typx_protocol_record_v1_t *record) {
  uint16_t classification;
  if (!all_zero(bytes + 22, 10)) {
    return TYPX_PROTOCOL_RESERVED_FIELD_NONZERO;
  }
  if (bytes[20] != TYPX_ACTION_KIND_KEYSTROKE ||
      bytes[21] != TYPX_PROGRESS_AFTER_RELEASE) {
    return TYPX_PROTOCOL_FLAGS_INVALID;
  }

  record->wait_before_key_down_us = read_u32_le(bytes);
  record->key_down_hold_us = read_u32_le(bytes + 4);
  record->wait_after_release_us = read_u32_le(bytes + 8);
  record->source_index = read_u32_le(bytes + 12);
  record->modifier = bytes[16];
  record->keycode = bytes[17];
  record->flags = read_u16_le(bytes + 18);

  if (record->wait_before_key_down_us > max_delay_us ||
      record->key_down_hold_us > max_delay_us ||
      record->wait_after_release_us > max_delay_us ||
      record->key_down_hold_us != 8000u) {
    return TYPX_PROTOCOL_DELAY_INVALID;
  }
  if ((record->modifier & (uint8_t)~0x03u) != 0u) {
    return TYPX_PROTOCOL_MODIFIER_INVALID;
  }
  if (!supported_keycode(record->keycode)) {
    return TYPX_PROTOCOL_KEYCODE_UNSUPPORTED;
  }
  classification = record->flags &
      (TYPX_RECORD_FLAG_SOURCE | TYPX_RECORD_FLAG_TECHNICAL);
  if ((record->flags & (uint16_t)~TYPX_RECORD_ALLOWED_FLAGS) != 0u ||
      (record->flags & TYPX_RECORD_FLAG_RELEASE_ALL) == 0u ||
      (classification != TYPX_RECORD_FLAG_SOURCE &&
       classification != TYPX_RECORD_FLAG_TECHNICAL) ||
      ((record->flags & TYPX_RECORD_FLAG_FINAL_CLEANUP) != 0u &&
       classification != TYPX_RECORD_FLAG_TECHNICAL)) {
    return TYPX_PROTOCOL_FLAGS_INVALID;
  }
  return TYPX_PROTOCOL_OK;
}

static typx_protocol_error_t read_record_unverified(
    const typx_reader_t *reader,
    uint32_t index,
    uint32_t max_delay_us,
    typx_protocol_record_v1_t *record) {
  uint8_t bytes[TYPX_PROTOCOL_V1_RECORD_BYTES];
  uint64_t offset = (uint64_t)TYPX_PROTOCOL_V1_HEADER_BYTES +
      (uint64_t)index * (uint64_t)TYPX_PROTOCOL_V1_RECORD_BYTES;
  if (!read_exact(reader, offset, bytes, sizeof(bytes))) {
    return TYPX_PROTOCOL_READER_ERROR;
  }
  return decode_record(bytes, max_delay_us, record);
}

static typx_protocol_error_t verify_payload_sha256(
    const typx_reader_t *reader,
    const typx_protocol_header_v1_t *header,
    const typx_sha256_provider_t *sha256) {
  uint8_t buffer[256];
  uint8_t digest[32];
  uint64_t offset = TYPX_PROTOCOL_V1_HEADER_BYTES;
  uint32_t remaining = header->payload_bytes;
  if (sha256 == NULL || sha256->begin == NULL || sha256->update == NULL ||
      sha256->finish == NULL || !sha256->begin(sha256->context)) {
    return TYPX_PROTOCOL_READER_ERROR;
  }
  while (remaining > 0u) {
    size_t chunk = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
    if (!read_exact(reader, offset, buffer, chunk) ||
        !sha256->update(sha256->context, buffer, chunk)) {
      return TYPX_PROTOCOL_READER_ERROR;
    }
    offset += chunk;
    remaining -= (uint32_t)chunk;
  }
  if (!sha256->finish(sha256->context, digest)) {
    return TYPX_PROTOCOL_READER_ERROR;
  }
  return memcmp(digest, header->payload_sha256, sizeof(digest)) == 0
      ? TYPX_PROTOCOL_OK
      : TYPX_PROTOCOL_CHECKSUM_MISMATCH;
}

static typx_protocol_error_t validate_records(
    const typx_reader_t *reader,
    const typx_protocol_limits_t *limits,
    const typx_protocol_header_v1_t *header) {
  static const uint8_t cleanup_modifiers[3] = {0x00u, 0x03u, 0x00u};
  static const uint8_t cleanup_keycodes[3] = {0x29u, 0x4du, 0x4cu};
  uint32_t index;
  uint32_t previous_source_index = 0u;
  uint64_t total_duration = 0u;

  if (header->action_count < 3u) {
    return TYPX_PROTOCOL_FINAL_CLEANUP_INVALID;
  }
  for (index = 0; index < header->action_count; ++index) {
    typx_protocol_record_v1_t record;
    uint64_t record_duration;
    bool should_be_cleanup = index >= header->action_count - 3u;
    bool marked_cleanup;
    typx_protocol_error_t error = read_record_unverified(
        reader, index, limits->max_individual_delay_us, &record);
    if (error != TYPX_PROTOCOL_OK) {
      return error;
    }
    if (record.source_index < previous_source_index ||
        record.source_index > header->total_source_characters ||
        ((record.flags & TYPX_RECORD_FLAG_SOURCE) != 0u &&
         record.source_index == 0u)) {
      return TYPX_PROTOCOL_SOURCE_INDEX_INVALID;
    }
    previous_source_index = record.source_index;
    marked_cleanup =
        (record.flags & TYPX_RECORD_FLAG_FINAL_CLEANUP) != 0u;
    if (marked_cleanup != should_be_cleanup) {
      return TYPX_PROTOCOL_FINAL_CLEANUP_INVALID;
    }
    if (should_be_cleanup) {
      uint32_t cleanup_index = index - (header->action_count - 3u);
      if (record.modifier != cleanup_modifiers[cleanup_index] ||
          record.keycode != cleanup_keycodes[cleanup_index]) {
        return TYPX_PROTOCOL_FINAL_CLEANUP_INVALID;
      }
    }
    record_duration = (uint64_t)record.wait_before_key_down_us +
        (uint64_t)record.key_down_hold_us +
        (uint64_t)record.wait_after_release_us;
    if (UINT64_MAX - total_duration < record_duration) {
      return TYPX_PROTOCOL_INTEGER_OVERFLOW;
    }
    total_duration += record_duration;
  }
  return total_duration == header->total_expected_duration_us &&
      total_duration > 0u
      ? TYPX_PROTOCOL_OK
      : TYPX_PROTOCOL_TOTAL_DURATION_INVALID;
}

typx_protocol_limits_t typx_protocol_v1_generic_limits(void) {
  typx_protocol_limits_t limits = {
      TYPX_PROTOCOL_V1_GENERIC_MAX_BYTES,
      TYPX_PROTOCOL_V1_GENERIC_MAX_RECORDS,
      TYPX_PROTOCOL_V1_MAX_DELAY_US};
  return limits;
}

typx_protocol_limits_t typx_protocol_v1_esp32_cam_limits(void) {
  typx_protocol_limits_t limits = {
      TYPX_ESP32_CAM_MAX_SCHEDULE_BYTES,
      TYPX_ESP32_CAM_MAX_RECORDS,
      TYPX_PROTOCOL_V1_MAX_DELAY_US};
  return limits;
}

typx_protocol_error_t typx_protocol_v1_verify(
    const typx_reader_t *reader,
    const typx_protocol_limits_t *limits,
    const typx_sha256_provider_t *sha256,
    typx_verified_schedule_v1_t *verified_schedule) {
  uint8_t bytes[TYPX_PROTOCOL_V1_HEADER_BYTES];
  typx_protocol_header_v1_t header;
  uint64_t expected_payload;
  uint64_t expected_size;
  typx_protocol_error_t error;

  if (verified_schedule == NULL) {
    return TYPX_PROTOCOL_HEADER_INVALID;
  }
  memset(verified_schedule, 0, sizeof(*verified_schedule));
  if (reader == NULL || limits == NULL || reader->read_at == NULL ||
      limits->max_encoded_bytes < TYPX_PROTOCOL_V1_HEADER_BYTES ||
      limits->max_record_count == 0u ||
      limits->max_individual_delay_us == 0u) {
    return TYPX_PROTOCOL_HEADER_INVALID;
  }
  if (reader->size_bytes < TYPX_PROTOCOL_V1_HEADER_BYTES) {
    return TYPX_PROTOCOL_HEADER_INVALID;
  }
  if (reader->size_bytes > limits->max_encoded_bytes) {
    return TYPX_PROTOCOL_SCHEDULE_LIMIT_EXCEEDED;
  }
  if (!read_exact(reader, 0u, bytes, sizeof(bytes))) {
    return TYPX_PROTOCOL_READER_ERROR;
  }
  if (memcmp(bytes, TYPX_MAGIC, sizeof(TYPX_MAGIC)) != 0) {
    return TYPX_PROTOCOL_HEADER_INVALID;
  }
  if (bytes[8] != 1u || bytes[10] != 1u) {
    return TYPX_PROTOCOL_VERSION_INCOMPATIBLE;
  }
  if (bytes[11] != 1u || read_u16_le(bytes + 12) != 96u ||
      read_u16_le(bytes + 14) != 32u) {
    return TYPX_PROTOCOL_HEADER_INVALID;
  }
  if (read_u32_le(bytes + 16) != TYPX_HEADER_REQUIRED_FLAGS) {
    return TYPX_PROTOCOL_FLAGS_INVALID;
  }
  if (!all_zero(bytes + 88, 8)) {
    return TYPX_PROTOCOL_RESERVED_FIELD_NONZERO;
  }

  memset(&header, 0, sizeof(header));
  header.protocol_major = bytes[8];
  header.protocol_minor = bytes[9];
  header.schedule_format_version = bytes[10];
  header.action_count = read_u32_le(bytes + 20);
  header.payload_bytes = read_u32_le(bytes + 24);
  header.total_source_characters = read_u32_le(bytes + 28);
  header.total_expected_duration_us = read_u64_le(bytes + 32);
  memcpy(header.job_id, bytes + 40, sizeof(header.job_id));
  memcpy(header.payload_sha256, bytes + 56, sizeof(header.payload_sha256));

  if (header.action_count == 0u ||
      header.action_count > limits->max_record_count) {
    return TYPX_PROTOCOL_SCHEDULE_LIMIT_EXCEEDED;
  }
  expected_payload = (uint64_t)header.action_count *
      (uint64_t)TYPX_PROTOCOL_V1_RECORD_BYTES;
  if (expected_payload > UINT32_MAX) {
    return TYPX_PROTOCOL_INTEGER_OVERFLOW;
  }
  if (header.payload_bytes != (uint32_t)expected_payload) {
    return TYPX_PROTOCOL_ACTION_COUNT_MISMATCH;
  }
  if (UINT64_MAX - TYPX_PROTOCOL_V1_HEADER_BYTES < expected_payload) {
    return TYPX_PROTOCOL_INTEGER_OVERFLOW;
  }
  expected_size = TYPX_PROTOCOL_V1_HEADER_BYTES + expected_payload;
  if (expected_size != reader->size_bytes) {
    return TYPX_PROTOCOL_PAYLOAD_SIZE_INVALID;
  }
  if (expected_size > limits->max_encoded_bytes) {
    return TYPX_PROTOCOL_SCHEDULE_LIMIT_EXCEEDED;
  }
  if (header.total_source_characters == 0u ||
      all_zero(header.job_id, sizeof(header.job_id))) {
    return header.total_source_characters == 0u
        ? TYPX_PROTOCOL_SOURCE_INDEX_INVALID
        : TYPX_PROTOCOL_JOB_ID_INVALID;
  }
  error = verify_payload_sha256(reader, &header, sha256);
  if (error != TYPX_PROTOCOL_OK) {
    return error;
  }
  error = validate_records(reader, limits, &header);
  if (error != TYPX_PROTOCOL_OK) {
    return error;
  }

  verified_schedule->reader = *reader;
  verified_schedule->limits = *limits;
  verified_schedule->header = header;
  verified_schedule->verification_token = TYPX_VERIFICATION_TOKEN;
  return TYPX_PROTOCOL_OK;
}

bool typx_protocol_v1_is_verified(
    const typx_verified_schedule_v1_t *schedule) {
  return schedule != NULL &&
      schedule->verification_token == TYPX_VERIFICATION_TOKEN;
}

typx_protocol_error_t typx_protocol_v1_read_record(
    const typx_verified_schedule_v1_t *schedule,
    uint32_t record_index,
    typx_protocol_record_v1_t *record) {
  if (!typx_protocol_v1_is_verified(schedule)) {
    return TYPX_PROTOCOL_NOT_VERIFIED;
  }
  if (record == NULL || record_index >= schedule->header.action_count) {
    return TYPX_PROTOCOL_ACTION_COUNT_MISMATCH;
  }
  return read_record_unverified(
      &schedule->reader,
      record_index,
      schedule->limits.max_individual_delay_us,
      record);
}

const char *typx_protocol_error_code(typx_protocol_error_t error) {
  static const char *codes[] = {
      "OK",
      "READER_ERROR",
      "HEADER_INVALID",
      "VERSION_INCOMPATIBLE",
      "PAYLOAD_SIZE_INVALID",
      "ACTION_COUNT_MISMATCH",
      "SCHEDULE_LIMIT_EXCEEDED",
      "CHECKSUM_MISMATCH",
      "FLAGS_INVALID",
      "RESERVED_FIELD_NONZERO",
      "JOB_ID_INVALID",
      "DELAY_INVALID",
      "MODIFIER_INVALID",
      "KEYCODE_UNSUPPORTED",
      "SOURCE_INDEX_INVALID",
      "TOTAL_DURATION_INVALID",
      "FINAL_CLEANUP_INVALID",
      "NOT_VERIFIED",
      "INTEGER_OVERFLOW"};
  size_t count = sizeof(codes) / sizeof(codes[0]);
  return (unsigned)error < count ? codes[error] : "UNKNOWN_PROTOCOL_ERROR";
}
