#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "typx_sha256.h"
#include "typx_wifi_upload.h"

static unsigned assertions;
static unsigned failures;

#define CHECK(condition) do { \
  ++assertions; \
  if (!(condition)) { \
    ++failures; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
  } \
} while (0)

typedef struct {
  uint8_t bytes[TYPX_UPLOAD_MAX_BYTES];
  bool fail_erase;
  bool fail_write;
  bool fail_read;
} fake_storage_t;

static fake_storage_t storage;

static bool erase_storage(void *context) {
  fake_storage_t *fake = context;
  if (fake->fail_erase) return false;
  memset(fake->bytes, 0xff, sizeof(fake->bytes));
  return true;
}

static bool write_storage(
    void *context, uint32_t offset, const uint8_t *data, size_t length) {
  fake_storage_t *fake = context;
  if (fake->fail_write || offset > sizeof(fake->bytes) ||
      length > sizeof(fake->bytes) - offset) return false;
  memcpy(fake->bytes + offset, data, length);
  return true;
}

static bool erase_storage_range(
    void *context, uint32_t offset, size_t length) {
  fake_storage_t *fake = context;
  if (fake->fail_erase || offset > sizeof(fake->bytes) ||
      length > sizeof(fake->bytes) - offset) return false;
  memset(fake->bytes + offset, 0xff, length);
  return true;
}

static bool read_storage(
    void *context, uint32_t offset, uint8_t *data, size_t length) {
  fake_storage_t *fake = context;
  if (fake->fail_read || offset > sizeof(fake->bytes) ||
      length > sizeof(fake->bytes) - offset) return false;
  memcpy(data, fake->bytes + offset, length);
  return true;
}

static typx_upload_storage_t storage_api(void) {
  typx_upload_storage_t api = {
    &storage, erase_storage, erase_storage_range, write_storage, read_storage};
  return api;
}

typedef struct {
  uint8_t *data;
  size_t size;
} file_data_t;

static file_data_t load_file(const char *path) {
  file_data_t result = {0};
  FILE *file = fopen(path, "rb");
  long size;
  CHECK(file != NULL);
  if (file == NULL) return result;
  CHECK(fseek(file, 0, SEEK_END) == 0);
  size = ftell(file);
  CHECK(size > 0);
  CHECK(fseek(file, 0, SEEK_SET) == 0);
  result.data = malloc((size_t)size);
  CHECK(result.data != NULL);
  if (result.data != NULL) {
    CHECK(fread(result.data, 1u, (size_t)size, file) == (size_t)size);
    result.size = (size_t)size;
  }
  fclose(file);
  return result;
}

static void sha256_bytes(
    const uint8_t *data, size_t length, uint8_t digest[32]) {
  typx_sha256_portable_context_t context;
  typx_sha256_provider_t sha = typx_sha256_portable_provider(&context);
  CHECK(sha.begin(sha.context));
  CHECK(sha.update(sha.context, data, length));
  CHECK(sha.finish(sha.context, digest));
}

static void job_id(uint8_t id[16], uint8_t first) {
  size_t index;
  for (index = 0; index < 16u; ++index) id[index] = (uint8_t)(first + index);
}

static typx_upload_error_t put_chunk(
    typx_wifi_upload_t *upload,
    const uint8_t id[16],
    uint32_t sequence,
    const uint8_t *data,
    uint32_t length,
    uint32_t advertised_crc) {
  typx_upload_chunk_session_t session;
  uint32_t sent = 0u;
  typx_upload_error_t error = typx_wifi_upload_chunk_begin(
      upload, id, sequence, sequence * TYPX_UPLOAD_CHUNK_BYTES,
      length, advertised_crc, &session);
  if (error != TYPX_UPLOAD_OK) return error;
  while (sent < length) {
    size_t piece = length - sent;
    if (piece > 257u) piece = 257u;
    error = typx_wifi_upload_chunk_write(&session, data + sent, piece);
    if (error != TYPX_UPLOAD_OK) return error;
    sent += (uint32_t)piece;
  }
  return typx_wifi_upload_chunk_finish(&session);
}

static void upload_all(
    typx_wifi_upload_t *upload,
    const uint8_t id[16],
    const file_data_t *file) {
  uint32_t offset = 0u;
  uint32_t sequence = 0u;
  while (offset < file->size) {
    uint32_t length = (uint32_t)(file->size - offset);
    if (length > TYPX_UPLOAD_CHUNK_BYTES) length = TYPX_UPLOAD_CHUNK_BYTES;
    CHECK(put_chunk(
        upload, id, sequence, file->data + offset, length,
        typx_wifi_upload_crc32(file->data + offset, length)) == TYPX_UPLOAD_OK);
    offset += length;
    ++sequence;
  }
}

static typx_upload_error_t finalize(
    typx_wifi_upload_t *upload, const uint8_t id[16]) {
  typx_sha256_portable_context_t context;
  typx_sha256_provider_t sha = typx_sha256_portable_provider(&context);
  return typx_wifi_upload_finalize(
      upload, id, upload->expected_size, upload->expected_sha256, &sha);
}

static void test_successful_lifecycle(void) {
  file_data_t file = load_file("test_vectors/protocol-v1/small-standard.bin");
  typx_wifi_upload_t upload;
  typx_upload_storage_t api = storage_api();
  const typx_verified_schedule_v1_t *schedule = NULL;
  uint8_t id[16];
  uint8_t digest[32];
  memset(&storage, 0, sizeof(storage));
  job_id(id, 1u);
  sha256_bytes(file.data, file.size, digest);
  typx_wifi_upload_init(&upload, &api);
  CHECK(upload.status == TYPX_UPLOAD_IDLE);
  CHECK(typx_wifi_upload_begin(
      &upload, id, (uint32_t)file.size, digest) == TYPX_UPLOAD_OK);
  upload_all(&upload, id, &file);
  CHECK(upload.received_bytes == file.size);
  CHECK(finalize(&upload, id) == TYPX_UPLOAD_OK);
  CHECK(upload.status == TYPX_UPLOAD_READY);
  CHECK(typx_wifi_upload_prepare_start(
      &upload, id, TYPX_UPLOAD_HID_DISCONNECTED,
      &schedule) == TYPX_UPLOAD_HID_NOT_CONNECTED);
  CHECK(upload.status == TYPX_UPLOAD_READY);
  CHECK(typx_wifi_upload_prepare_start(
      &upload, id, TYPX_UPLOAD_HID_PENDING,
      &schedule) == TYPX_UPLOAD_HID_SAFETY_PENDING);
  CHECK(upload.status == TYPX_UPLOAD_READY);
  CHECK(typx_wifi_upload_prepare_start(
      &upload, id, TYPX_UPLOAD_HID_FAILED,
      &schedule) == TYPX_UPLOAD_HID_SAFETY_FAILED);
  CHECK(upload.status == TYPX_UPLOAD_READY);
  CHECK(typx_wifi_upload_prepare_start(
      &upload, id, TYPX_UPLOAD_HID_READY, &schedule) == TYPX_UPLOAD_OK);
  CHECK(schedule != NULL && typx_protocol_v1_is_verified(schedule));
  CHECK(upload.status == TYPX_UPLOAD_COUNTDOWN);
  CHECK(typx_wifi_upload_mark_executing(&upload) == TYPX_UPLOAD_OK);
  CHECK(upload.status == TYPX_UPLOAD_EXECUTING);
  typx_wifi_upload_record_progress(
      &upload, 7u, 1u, upload.total_records);
  CHECK(upload.completed_records == 1u);
  CHECK(upload.current_source_index == 7u);
  typx_wifi_upload_complete(&upload, TYPX_EXECUTOR_OK);
  CHECK(upload.status == TYPX_UPLOAD_COMPLETED);
  free(file.data);
}

static void test_interrupted_and_reboot_safety(void) {
  file_data_t file = load_file("test_vectors/protocol-v1/small-standard.bin");
  typx_wifi_upload_t upload;
  typx_wifi_upload_t rebooted;
  typx_upload_storage_t api = storage_api();
  const typx_verified_schedule_v1_t *schedule = NULL;
  uint8_t id[16];
  uint8_t digest[32];
  job_id(id, 20u);
  sha256_bytes(file.data, file.size, digest);
  typx_wifi_upload_init(&upload, &api);
  CHECK(typx_wifi_upload_begin(
      &upload, id, (uint32_t)file.size, digest) == TYPX_UPLOAD_OK);
  CHECK(typx_wifi_upload_prepare_start(
      &upload, id, TYPX_UPLOAD_HID_READY,
      &schedule) == TYPX_UPLOAD_INVALID_STATE);
  CHECK(finalize(&upload, id) == TYPX_UPLOAD_INCOMPLETE);
  typx_wifi_upload_init(&rebooted, &api);
  CHECK(rebooted.status == TYPX_UPLOAD_IDLE);
  typx_wifi_upload_mark_interrupted_reset(&rebooted);
  CHECK(rebooted.status == TYPX_UPLOAD_INTERRUPTED_RESET);
  CHECK(rebooted.executor_error == TYPX_EXECUTOR_INTERRUPTED_RESET);
  CHECK(typx_wifi_upload_prepare_start(
      &rebooted, id, TYPX_UPLOAD_HID_READY,
      &schedule) == TYPX_UPLOAD_INVALID_STATE);
  free(file.data);
}

static void test_cancel_and_stop_lifecycle(void) {
  file_data_t file = load_file("test_vectors/protocol-v1/small-standard.bin");
  typx_wifi_upload_t upload;
  typx_upload_storage_t api = storage_api();
  const typx_verified_schedule_v1_t *schedule = NULL;
  uint8_t id[16];
  uint8_t other_id[16];
  uint8_t digest[32];
  job_id(id, 100u);
  job_id(other_id, 120u);
  sha256_bytes(file.data, file.size, digest);
  typx_wifi_upload_init(&upload, &api);
  CHECK(typx_wifi_upload_begin(
      &upload, id, (uint32_t)file.size, digest) == TYPX_UPLOAD_OK);
  upload_all(&upload, id, &file);
  CHECK(finalize(&upload, id) == TYPX_UPLOAD_OK);
  CHECK(typx_wifi_upload_job_locked(&upload));
  CHECK(typx_wifi_upload_cancel(&upload, other_id) ==
      TYPX_UPLOAD_JOB_MISMATCH);
  CHECK(typx_wifi_upload_cancel(&upload, id) == TYPX_UPLOAD_OK);
  CHECK(upload.status == TYPX_UPLOAD_IDLE);

  CHECK(typx_wifi_upload_begin(
      &upload, id, (uint32_t)file.size, digest) == TYPX_UPLOAD_OK);
  upload_all(&upload, id, &file);
  CHECK(finalize(&upload, id) == TYPX_UPLOAD_OK);
  CHECK(typx_wifi_upload_prepare_start(
      &upload, id, TYPX_UPLOAD_HID_READY, &schedule) == TYPX_UPLOAD_OK);
  CHECK(typx_wifi_upload_request_stop(&upload) == TYPX_UPLOAD_OK);
  CHECK(upload.status == TYPX_UPLOAD_STOPPING);
  CHECK(upload.stop_requested);
  CHECK(typx_wifi_upload_request_stop(&upload) == TYPX_UPLOAD_OK);
  typx_wifi_upload_complete(&upload, TYPX_EXECUTOR_STOPPED);
  CHECK(upload.status == TYPX_UPLOAD_STOPPED);
  CHECK(typx_wifi_upload_request_stop(&upload) == TYPX_UPLOAD_OK);
  CHECK(typx_wifi_upload_cancel(&upload, id) == TYPX_UPLOAD_INVALID_STATE);
  free(file.data);
}

static void test_chunk_retry_conflict_and_crc(void) {
  file_data_t file = load_file("test_vectors/protocol-v1/small-standard.bin");
  typx_wifi_upload_t upload;
  typx_upload_storage_t api = storage_api();
  uint8_t id[16];
  uint8_t digest[32];
  uint8_t changed[448];
  uint32_t crc;
  job_id(id, 40u);
  sha256_bytes(file.data, file.size, digest);
  typx_wifi_upload_init(&upload, &api);
  CHECK(typx_wifi_upload_begin(
      &upload, id, (uint32_t)file.size, digest) == TYPX_UPLOAD_OK);
  crc = typx_wifi_upload_crc32(file.data, file.size);
  CHECK(put_chunk(&upload, id, 0u, file.data, (uint32_t)file.size, crc) ==
      TYPX_UPLOAD_OK);
  CHECK(upload.received_bytes == file.size);
  CHECK(put_chunk(&upload, id, 0u, file.data, (uint32_t)file.size, crc) ==
      TYPX_UPLOAD_OK);
  CHECK(upload.received_bytes == file.size);
  CHECK(put_chunk(&upload, id, 0u, file.data, (uint32_t)file.size, crc ^ 1u) ==
      TYPX_UPLOAD_CHUNK_CONFLICT);
  memcpy(changed, file.data, file.size);
  changed[100] ^= 1u;
  CHECK(put_chunk(&upload, id, 0u, changed, (uint32_t)file.size, crc) ==
      TYPX_UPLOAD_CHUNK_CONFLICT);

  job_id(id, 60u);
  CHECK(typx_wifi_upload_begin(
      &upload, id, (uint32_t)file.size, digest) == TYPX_UPLOAD_OK);
  CHECK(put_chunk(&upload, id, 0u, file.data, (uint32_t)file.size, crc ^ 1u) ==
      TYPX_UPLOAD_CHUNK_CRC_MISMATCH);
  CHECK(upload.received_bytes == 0u);
  free(file.data);
}

static void test_oversize_and_finalize_failures(void) {
  file_data_t file = load_file("test_vectors/protocol-v1/small-standard.bin");
  typx_wifi_upload_t upload;
  typx_upload_storage_t api = storage_api();
  uint8_t id[16];
  uint8_t digest[32];
  job_id(id, 80u);
  sha256_bytes(file.data, file.size, digest);
  typx_wifi_upload_init(&upload, &api);
  CHECK(typx_wifi_upload_begin(
      &upload, id, TYPX_UPLOAD_MAX_BYTES + 1u, digest) == TYPX_UPLOAD_OVERSIZED);
  digest[0] ^= 1u;
  CHECK(typx_wifi_upload_begin(
      &upload, id, (uint32_t)file.size, digest) == TYPX_UPLOAD_OK);
  upload_all(&upload, id, &file);
  CHECK(typx_wifi_upload_finalize(
      &upload, id, (uint32_t)file.size + 1u, digest, NULL) ==
      TYPX_UPLOAD_SHA256_MISMATCH);
  CHECK(finalize(&upload, id) == TYPX_UPLOAD_SHA256_MISMATCH);
  CHECK(upload.status != TYPX_UPLOAD_READY);
  free(file.data);
}

int main(void) {
  test_successful_lifecycle();
  test_interrupted_and_reboot_safety();
  test_chunk_retry_conflict_and_crc();
  test_oversize_and_finalize_failures();
  test_cancel_and_stop_lifecycle();
  printf("Wi-Fi upload tests: %u assertions, %u failures\n", assertions, failures);
  return failures == 0u ? 0 : 1;
}
