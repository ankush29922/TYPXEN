#ifndef TYPX_WIFI_UPLOAD_H
#define TYPX_WIFI_UPLOAD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "typx_executor.h"
#include "typx_protocol.h"

#define TYPX_UPLOAD_MAX_BYTES (512u * 1024u)
#define TYPX_UPLOAD_CHUNK_BYTES 4096u
#define TYPX_UPLOAD_MAX_CHUNKS 128u

typedef enum {
  TYPX_UPLOAD_IDLE = 0,
  TYPX_UPLOAD_RECEIVING,
  TYPX_UPLOAD_VERIFYING,
  TYPX_UPLOAD_READY,
  TYPX_UPLOAD_COUNTDOWN,
  TYPX_UPLOAD_EXECUTING,
  TYPX_UPLOAD_STOPPING,
  TYPX_UPLOAD_STOPPED,
  TYPX_UPLOAD_COMPLETED,
  TYPX_UPLOAD_ERROR,
  TYPX_UPLOAD_INTERRUPTED_RESET
} typx_upload_status_t;

typedef enum {
  TYPX_UPLOAD_OK = 0,
  TYPX_UPLOAD_INVALID_ARGUMENT,
  TYPX_UPLOAD_INVALID_STATE,
  TYPX_UPLOAD_OVERSIZED,
  TYPX_UPLOAD_JOB_MISMATCH,
  TYPX_UPLOAD_CHUNK_CRC_MISMATCH,
  TYPX_UPLOAD_CHUNK_CONFLICT,
  TYPX_UPLOAD_STORAGE_FAILURE,
  TYPX_UPLOAD_INCOMPLETE,
  TYPX_UPLOAD_SHA256_MISMATCH,
  TYPX_UPLOAD_PROTOCOL_REJECTED,
  TYPX_UPLOAD_HID_NOT_CONNECTED,
  TYPX_UPLOAD_HID_SAFETY_PENDING,
  TYPX_UPLOAD_HID_SAFETY_FAILED
} typx_upload_error_t;

typedef enum {
  TYPX_UPLOAD_HID_DISCONNECTED = 0,
  TYPX_UPLOAD_HID_PENDING,
  TYPX_UPLOAD_HID_FAILED,
  TYPX_UPLOAD_HID_READY
} typx_upload_hid_readiness_t;

typedef bool (*typx_upload_erase_fn)(void *context);
typedef bool (*typx_upload_erase_range_fn)(
    void *context, uint32_t offset, size_t length);
typedef bool (*typx_upload_write_fn)(
    void *context, uint32_t offset, const uint8_t *data, size_t length);
typedef bool (*typx_upload_read_fn)(
    void *context, uint32_t offset, uint8_t *data, size_t length);

typedef struct {
  void *context;
  typx_upload_erase_fn erase;
  typx_upload_erase_range_fn erase_range;
  typx_upload_write_fn write;
  typx_upload_read_fn read;
} typx_upload_storage_t;

typedef struct {
  typx_upload_storage_t storage;
  typx_upload_status_t status;
  typx_upload_error_t last_error;
  typx_protocol_error_t protocol_error;
  typx_executor_error_t executor_error;
  uint8_t job_id[16];
  uint8_t expected_sha256[32];
  uint32_t expected_size;
  uint32_t received_bytes;
  uint16_t chunk_lengths[TYPX_UPLOAD_MAX_CHUNKS];
  uint32_t chunk_crc32[TYPX_UPLOAD_MAX_CHUNKS];
  volatile bool stop_requested;
  volatile uint32_t completed_records;
  volatile uint32_t current_source_index;
  uint32_t total_records;
  typx_verified_schedule_v1_t verified_schedule;
} typx_wifi_upload_t;

typedef struct {
  typx_wifi_upload_t *upload;
  uint32_t sequence;
  uint32_t offset;
  uint32_t length;
  uint32_t expected_crc32;
  uint32_t running_crc32;
  uint32_t received;
  bool repeated;
  bool conflict;
} typx_upload_chunk_session_t;

void typx_wifi_upload_init(
    typx_wifi_upload_t *upload, const typx_upload_storage_t *storage);

typx_upload_error_t typx_wifi_upload_begin(
    typx_wifi_upload_t *upload,
    const uint8_t job_id[16],
    uint32_t encoded_size,
    const uint8_t encoded_sha256[32]);

typx_upload_error_t typx_wifi_upload_chunk_begin(
    typx_wifi_upload_t *upload,
    const uint8_t job_id[16],
    uint32_t sequence,
    uint32_t offset,
    uint32_t length,
    uint32_t crc32,
    typx_upload_chunk_session_t *session);

typx_upload_error_t typx_wifi_upload_chunk_write(
    typx_upload_chunk_session_t *session,
    const uint8_t *data,
    size_t length);

typx_upload_error_t typx_wifi_upload_chunk_finish(
    typx_upload_chunk_session_t *session);

typx_upload_error_t typx_wifi_upload_finalize(
    typx_wifi_upload_t *upload,
    const uint8_t job_id[16],
    uint32_t encoded_size,
    const uint8_t encoded_sha256[32],
    const typx_sha256_provider_t *sha256);

typx_upload_error_t typx_wifi_upload_prepare_start(
    typx_wifi_upload_t *upload,
    const uint8_t job_id[16],
    typx_upload_hid_readiness_t hid_readiness,
    const typx_verified_schedule_v1_t **schedule);

typx_upload_error_t typx_wifi_upload_start_failed(
    typx_wifi_upload_t *upload);
typx_upload_error_t typx_wifi_upload_mark_executing(
    typx_wifi_upload_t *upload);
typx_upload_error_t typx_wifi_upload_request_stop(
    typx_wifi_upload_t *upload);
typx_upload_error_t typx_wifi_upload_cancel(
    typx_wifi_upload_t *upload, const uint8_t job_id[16]);
void typx_wifi_upload_record_progress(
    typx_wifi_upload_t *upload,
    uint32_t source_index,
    uint32_t completed_records,
    uint32_t total_records);
void typx_wifi_upload_mark_interrupted_reset(
    typx_wifi_upload_t *upload);
bool typx_wifi_upload_job_locked(
    const typx_wifi_upload_t *upload);

void typx_wifi_upload_complete(
    typx_wifi_upload_t *upload, typx_executor_error_t executor_error);

uint32_t typx_wifi_upload_crc32(
    const uint8_t *data, size_t length);
const char *typx_wifi_upload_error_code(typx_upload_error_t error);
const char *typx_wifi_upload_status_code(typx_upload_status_t status);

#endif
