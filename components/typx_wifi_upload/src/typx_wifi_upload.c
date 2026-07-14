#include "typx_wifi_upload.h"

#include <string.h>

static bool all_zero(const uint8_t *data, size_t length) {
  size_t index;
  for (index = 0; index < length; ++index) {
    if (data[index] != 0u) return false;
  }
  return true;
}

static bool same_job(
    const typx_wifi_upload_t *upload, const uint8_t job_id[16]) {
  return upload != NULL && job_id != NULL &&
      memcmp(upload->job_id, job_id, 16u) == 0;
}

static uint32_t crc32_update(
    uint32_t crc, const uint8_t *data, size_t length) {
  size_t index;
  for (index = 0; index < length; ++index) {
    unsigned bit;
    crc ^= data[index];
    for (bit = 0; bit < 8u; ++bit) {
      crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
  }
  return crc;
}

uint32_t typx_wifi_upload_crc32(
    const uint8_t *data, size_t length) {
  return data == NULL && length != 0u
      ? 0u
      : crc32_update(0xffffffffu, data, length) ^ 0xffffffffu;
}

static void set_error(
    typx_wifi_upload_t *upload, typx_upload_error_t error) {
  if (upload != NULL) upload->last_error = error;
}

bool typx_wifi_upload_job_locked(
    const typx_wifi_upload_t *upload) {
  if (upload == NULL) return false;
  return upload->status == TYPX_UPLOAD_READY ||
      upload->status == TYPX_UPLOAD_COUNTDOWN ||
      upload->status == TYPX_UPLOAD_EXECUTING ||
      upload->status == TYPX_UPLOAD_STOPPING;
}

void typx_wifi_upload_init(
    typx_wifi_upload_t *upload, const typx_upload_storage_t *storage) {
  if (upload == NULL) return;
  memset(upload, 0, sizeof(*upload));
  if (storage != NULL) upload->storage = *storage;
  upload->status = TYPX_UPLOAD_IDLE;
  upload->executor_error = TYPX_EXECUTOR_OK;
}

typx_upload_error_t typx_wifi_upload_begin(
    typx_wifi_upload_t *upload,
    const uint8_t job_id[16],
    uint32_t encoded_size,
    const uint8_t encoded_sha256[32]) {
  typx_upload_storage_t storage;
  if (upload == NULL || job_id == NULL || encoded_sha256 == NULL ||
      all_zero(job_id, 16u) || all_zero(encoded_sha256, 32u) ||
      upload->storage.erase == NULL || upload->storage.erase_range == NULL ||
      upload->storage.write == NULL || upload->storage.read == NULL) {
    return TYPX_UPLOAD_INVALID_ARGUMENT;
  }
  if (encoded_size == 0u || encoded_size > TYPX_UPLOAD_MAX_BYTES) {
    set_error(upload, TYPX_UPLOAD_OVERSIZED);
    return TYPX_UPLOAD_OVERSIZED;
  }
  if (typx_wifi_upload_job_locked(upload)) {
    return TYPX_UPLOAD_INVALID_STATE;
  }
  storage = upload->storage;
  memset(upload, 0, sizeof(*upload));
  upload->storage = storage;
  upload->status = TYPX_UPLOAD_RECEIVING;
  upload->expected_size = encoded_size;
  memcpy(upload->job_id, job_id, 16u);
  memcpy(upload->expected_sha256, encoded_sha256, 32u);
  if (!upload->storage.erase(upload->storage.context)) {
    upload->status = TYPX_UPLOAD_ERROR;
    upload->last_error = TYPX_UPLOAD_STORAGE_FAILURE;
    return TYPX_UPLOAD_STORAGE_FAILURE;
  }
  return TYPX_UPLOAD_OK;
}

typx_upload_error_t typx_wifi_upload_chunk_begin(
    typx_wifi_upload_t *upload,
    const uint8_t job_id[16],
    uint32_t sequence,
    uint32_t offset,
    uint32_t length,
    uint32_t crc32,
    typx_upload_chunk_session_t *session) {
  uint32_t expected_length;
  if (upload == NULL || session == NULL ||
      upload->status != TYPX_UPLOAD_RECEIVING) {
    return TYPX_UPLOAD_INVALID_STATE;
  }
  if (!same_job(upload, job_id)) return TYPX_UPLOAD_JOB_MISMATCH;
  if (sequence >= TYPX_UPLOAD_MAX_CHUNKS ||
      offset != sequence * TYPX_UPLOAD_CHUNK_BYTES ||
      offset >= upload->expected_size) {
    return TYPX_UPLOAD_INVALID_ARGUMENT;
  }
  expected_length = upload->expected_size - offset;
  if (expected_length > TYPX_UPLOAD_CHUNK_BYTES)
    expected_length = TYPX_UPLOAD_CHUNK_BYTES;
  if (length == 0u || length != expected_length) {
    return TYPX_UPLOAD_INVALID_ARGUMENT;
  }
  memset(session, 0, sizeof(*session));
  session->upload = upload;
  session->sequence = sequence;
  session->offset = offset;
  session->length = length;
  session->expected_crc32 = crc32;
  session->running_crc32 = 0xffffffffu;
  session->repeated = upload->chunk_lengths[sequence] != 0u;
  if (session->repeated &&
      (upload->chunk_lengths[sequence] != length ||
       upload->chunk_crc32[sequence] != crc32)) {
    session->conflict = true;
    return TYPX_UPLOAD_CHUNK_CONFLICT;
  }
  if (!session->repeated && !upload->storage.erase_range(
          upload->storage.context, offset, TYPX_UPLOAD_CHUNK_BYTES)) {
    return TYPX_UPLOAD_STORAGE_FAILURE;
  }
  return TYPX_UPLOAD_OK;
}

typx_upload_error_t typx_wifi_upload_chunk_write(
    typx_upload_chunk_session_t *session,
    const uint8_t *data,
    size_t length) {
  typx_wifi_upload_t *upload;
  if (session == NULL || session->upload == NULL || data == NULL ||
      length == 0u || length > session->length - session->received ||
      session->conflict) {
    return TYPX_UPLOAD_INVALID_ARGUMENT;
  }
  upload = session->upload;
  session->running_crc32 = crc32_update(
      session->running_crc32, data, length);
  if (session->repeated) {
    uint8_t stored[256];
    size_t compared = 0u;
    while (compared < length) {
      size_t chunk = length - compared;
      if (chunk > sizeof(stored)) chunk = sizeof(stored);
      if (!upload->storage.read(
              upload->storage.context,
              session->offset + session->received + (uint32_t)compared,
              stored, chunk)) {
        return TYPX_UPLOAD_STORAGE_FAILURE;
      }
      if (memcmp(stored, data + compared, chunk) != 0) {
        session->conflict = true;
        return TYPX_UPLOAD_CHUNK_CONFLICT;
      }
      compared += chunk;
    }
  } else if (!upload->storage.write(
                 upload->storage.context,
                 session->offset + session->received,
                 data, length)) {
    return TYPX_UPLOAD_STORAGE_FAILURE;
  }
  session->received += (uint32_t)length;
  return TYPX_UPLOAD_OK;
}

typx_upload_error_t typx_wifi_upload_chunk_finish(
    typx_upload_chunk_session_t *session) {
  typx_wifi_upload_t *upload;
  uint32_t actual_crc;
  if (session == NULL || session->upload == NULL || session->conflict) {
    return session != NULL && session->conflict
        ? TYPX_UPLOAD_CHUNK_CONFLICT : TYPX_UPLOAD_INVALID_ARGUMENT;
  }
  upload = session->upload;
  if (session->received != session->length)
    return TYPX_UPLOAD_INCOMPLETE;
  actual_crc = session->running_crc32 ^ 0xffffffffu;
  if (actual_crc != session->expected_crc32) {
    set_error(upload, TYPX_UPLOAD_CHUNK_CRC_MISMATCH);
    return TYPX_UPLOAD_CHUNK_CRC_MISMATCH;
  }
  if (!session->repeated) {
    upload->chunk_lengths[session->sequence] = (uint16_t)session->length;
    upload->chunk_crc32[session->sequence] = actual_crc;
    upload->received_bytes += session->length;
  }
  return TYPX_UPLOAD_OK;
}

static bool upload_read_at(
    void *context, uint64_t offset, uint8_t *destination, size_t length) {
  typx_wifi_upload_t *upload = context;
  return upload != NULL && offset <= upload->expected_size &&
      length <= upload->expected_size - (size_t)offset &&
      upload->storage.read(
          upload->storage.context, (uint32_t)offset, destination, length);
}

static typx_upload_error_t verify_encoded_sha(
    typx_wifi_upload_t *upload,
    const typx_sha256_provider_t *sha256) {
  uint8_t buffer[256];
  uint8_t digest[32];
  uint32_t offset = 0u;
  if (sha256 == NULL || sha256->begin == NULL || sha256->update == NULL ||
      sha256->finish == NULL || !sha256->begin(sha256->context))
    return TYPX_UPLOAD_STORAGE_FAILURE;
  while (offset < upload->expected_size) {
    size_t chunk = upload->expected_size - offset;
    if (chunk > sizeof(buffer)) chunk = sizeof(buffer);
    if (!upload->storage.read(upload->storage.context, offset, buffer, chunk) ||
        !sha256->update(sha256->context, buffer, chunk))
      return TYPX_UPLOAD_STORAGE_FAILURE;
    offset += (uint32_t)chunk;
  }
  if (!sha256->finish(sha256->context, digest))
    return TYPX_UPLOAD_STORAGE_FAILURE;
  return memcmp(digest, upload->expected_sha256, 32u) == 0
      ? TYPX_UPLOAD_OK : TYPX_UPLOAD_SHA256_MISMATCH;
}

typx_upload_error_t typx_wifi_upload_finalize(
    typx_wifi_upload_t *upload,
    const uint8_t job_id[16],
    uint32_t encoded_size,
    const uint8_t encoded_sha256[32],
    const typx_sha256_provider_t *sha256) {
  uint32_t chunk_count;
  uint32_t index;
  typx_upload_error_t upload_error;
  typx_reader_t reader;
  typx_protocol_limits_t limits;
  if (upload == NULL || upload->status != TYPX_UPLOAD_RECEIVING)
    return TYPX_UPLOAD_INVALID_STATE;
  if (!same_job(upload, job_id)) return TYPX_UPLOAD_JOB_MISMATCH;
  if (encoded_sha256 == NULL || encoded_size != upload->expected_size ||
      memcmp(encoded_sha256, upload->expected_sha256, 32u) != 0) {
    set_error(upload, TYPX_UPLOAD_SHA256_MISMATCH);
    return TYPX_UPLOAD_SHA256_MISMATCH;
  }
  if (upload->received_bytes != upload->expected_size)
    return TYPX_UPLOAD_INCOMPLETE;
  chunk_count = (upload->expected_size + TYPX_UPLOAD_CHUNK_BYTES - 1u) /
      TYPX_UPLOAD_CHUNK_BYTES;
  for (index = 0u; index < chunk_count; ++index) {
    if (upload->chunk_lengths[index] == 0u) return TYPX_UPLOAD_INCOMPLETE;
  }
  upload->status = TYPX_UPLOAD_VERIFYING;
  upload_error = verify_encoded_sha(upload, sha256);
  if (upload_error != TYPX_UPLOAD_OK) {
    upload->status = TYPX_UPLOAD_ERROR;
    set_error(upload, upload_error);
    return upload_error;
  }
  reader.context = upload;
  reader.size_bytes = upload->expected_size;
  reader.read_at = upload_read_at;
  limits = typx_protocol_v1_esp32_cam_limits();
  upload->protocol_error = typx_protocol_v1_verify(
      &reader, &limits, sha256, &upload->verified_schedule);
  if (upload->protocol_error != TYPX_PROTOCOL_OK) {
    upload->status = TYPX_UPLOAD_ERROR;
    upload->last_error = TYPX_UPLOAD_PROTOCOL_REJECTED;
    return TYPX_UPLOAD_PROTOCOL_REJECTED;
  }
  upload->status = TYPX_UPLOAD_READY;
  upload->last_error = TYPX_UPLOAD_OK;
  upload->total_records = upload->verified_schedule.header.action_count;
  upload->completed_records = 0u;
  upload->current_source_index = 0u;
  return TYPX_UPLOAD_OK;
}

typx_upload_error_t typx_wifi_upload_prepare_start(
    typx_wifi_upload_t *upload,
    const uint8_t job_id[16],
    typx_upload_hid_readiness_t hid_readiness,
    const typx_verified_schedule_v1_t **schedule) {
  if (upload == NULL || schedule == NULL ||
      upload->status != TYPX_UPLOAD_READY)
    return TYPX_UPLOAD_INVALID_STATE;
  if (!same_job(upload, job_id)) return TYPX_UPLOAD_JOB_MISMATCH;
  if (hid_readiness == TYPX_UPLOAD_HID_DISCONNECTED)
    return TYPX_UPLOAD_HID_NOT_CONNECTED;
  if (hid_readiness == TYPX_UPLOAD_HID_PENDING)
    return TYPX_UPLOAD_HID_SAFETY_PENDING;
  if (hid_readiness == TYPX_UPLOAD_HID_FAILED)
    return TYPX_UPLOAD_HID_SAFETY_FAILED;
  if (hid_readiness != TYPX_UPLOAD_HID_READY)
    return TYPX_UPLOAD_INVALID_ARGUMENT;
  if (!typx_protocol_v1_is_verified(&upload->verified_schedule))
    return TYPX_UPLOAD_PROTOCOL_REJECTED;
  upload->status = TYPX_UPLOAD_COUNTDOWN;
  upload->stop_requested = false;
  *schedule = &upload->verified_schedule;
  return TYPX_UPLOAD_OK;
}

typx_upload_error_t typx_wifi_upload_start_failed(
    typx_wifi_upload_t *upload) {
  if (upload == NULL || upload->status != TYPX_UPLOAD_COUNTDOWN) {
    return TYPX_UPLOAD_INVALID_STATE;
  }
  upload->status = TYPX_UPLOAD_READY;
  upload->stop_requested = false;
  return TYPX_UPLOAD_OK;
}

typx_upload_error_t typx_wifi_upload_mark_executing(
    typx_wifi_upload_t *upload) {
  if (upload == NULL || upload->status != TYPX_UPLOAD_COUNTDOWN) {
    return TYPX_UPLOAD_INVALID_STATE;
  }
  upload->status = TYPX_UPLOAD_EXECUTING;
  return TYPX_UPLOAD_OK;
}

typx_upload_error_t typx_wifi_upload_request_stop(
    typx_wifi_upload_t *upload) {
  if (upload == NULL) return TYPX_UPLOAD_INVALID_ARGUMENT;
  if (upload->status == TYPX_UPLOAD_STOPPING ||
      upload->status == TYPX_UPLOAD_STOPPED) {
    return TYPX_UPLOAD_OK;
  }
  if (upload->status != TYPX_UPLOAD_COUNTDOWN &&
      upload->status != TYPX_UPLOAD_EXECUTING) {
    return TYPX_UPLOAD_INVALID_STATE;
  }
  upload->stop_requested = true;
  upload->status = TYPX_UPLOAD_STOPPING;
  return TYPX_UPLOAD_OK;
}

typx_upload_error_t typx_wifi_upload_cancel(
    typx_wifi_upload_t *upload, const uint8_t job_id[16]) {
  typx_upload_storage_t storage;
  if (upload == NULL || upload->status != TYPX_UPLOAD_READY) {
    return TYPX_UPLOAD_INVALID_STATE;
  }
  if (!same_job(upload, job_id)) return TYPX_UPLOAD_JOB_MISMATCH;
  storage = upload->storage;
  memset(upload, 0, sizeof(*upload));
  upload->storage = storage;
  upload->status = TYPX_UPLOAD_IDLE;
  return TYPX_UPLOAD_OK;
}

void typx_wifi_upload_record_progress(
    typx_wifi_upload_t *upload,
    uint32_t source_index,
    uint32_t completed_records,
    uint32_t total_records) {
  if (upload == NULL || upload->status != TYPX_UPLOAD_EXECUTING ||
      total_records != upload->total_records ||
      completed_records > total_records) {
    return;
  }
  upload->current_source_index = source_index;
  upload->completed_records = completed_records;
}

void typx_wifi_upload_mark_interrupted_reset(
    typx_wifi_upload_t *upload) {
  if (upload == NULL) return;
  upload->status = TYPX_UPLOAD_INTERRUPTED_RESET;
  upload->executor_error = TYPX_EXECUTOR_INTERRUPTED_RESET;
  upload->last_error = TYPX_UPLOAD_OK;
  upload->stop_requested = false;
}

void typx_wifi_upload_complete(
    typx_wifi_upload_t *upload, typx_executor_error_t executor_error) {
  if (upload == NULL) return;
  upload->executor_error = executor_error;
  upload->status = executor_error == TYPX_EXECUTOR_OK
      ? TYPX_UPLOAD_COMPLETED
      : (executor_error == TYPX_EXECUTOR_STOPPED
          ? TYPX_UPLOAD_STOPPED : TYPX_UPLOAD_ERROR);
  upload->last_error = executor_error == TYPX_EXECUTOR_OK
      || executor_error == TYPX_EXECUTOR_STOPPED
      ? TYPX_UPLOAD_OK : TYPX_UPLOAD_INVALID_STATE;
  upload->stop_requested = false;
}

const char *typx_wifi_upload_error_code(typx_upload_error_t error) {
  static const char *codes[] = {
    "OK", "INVALID_ARGUMENT", "INVALID_STATE", "OVERSIZED",
    "JOB_MISMATCH", "CHUNK_CRC_MISMATCH", "CHUNK_CONFLICT",
    "STORAGE_FAILURE", "INCOMPLETE", "SHA256_MISMATCH",
    "PROTOCOL_REJECTED", "HID_NOT_CONNECTED", "HID_SAFETY_PENDING",
    "HID_SAFETY_FAILED"
  };
  size_t count = sizeof(codes) / sizeof(codes[0]);
  return (unsigned)error < count ? codes[error] : "UNKNOWN_UPLOAD_ERROR";
}

const char *typx_wifi_upload_status_code(typx_upload_status_t status) {
  static const char *codes[] = {
    "IDLE", "UPLOADING", "VERIFYING", "READY", "COUNTDOWN",
    "EXECUTING", "STOPPING", "STOPPED", "COMPLETED", "FAILED",
    "INTERRUPTED_RESET"};
  size_t count = sizeof(codes) / sizeof(codes[0]);
  return (unsigned)status < count ? codes[status] : "UNKNOWN";
}
