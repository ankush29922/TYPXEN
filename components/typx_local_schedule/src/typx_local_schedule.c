#include "typx_local_schedule.h"

#include <string.h>

typedef struct {
  const uint8_t *data;
  size_t size;
} memory_reader_context_t;

static bool read_memory(
    void *context, uint64_t offset, uint8_t *destination, size_t length) {
  memory_reader_context_t *memory = context;
  if (memory == NULL || destination == NULL || offset > memory->size ||
      length > memory->size - (size_t)offset) {
    return false;
  }
  memcpy(destination, memory->data + (size_t)offset, length);
  return true;
}

typx_local_schedule_result_t typx_local_schedule_verify_and_run(
    const typx_local_schedule_blob_t *blob,
    const typx_protocol_limits_t *limits,
    const typx_sha256_provider_t *sha256,
    const typx_local_schedule_runner_t *runner) {
  typx_local_schedule_result_t result = {
    false, TYPX_PROTOCOL_HEADER_INVALID, TYPX_EXECUTOR_NOT_VERIFIED};
  memory_reader_context_t memory;
  typx_reader_t reader;
  typx_verified_schedule_v1_t schedule;
  if (blob == NULL || blob->data == NULL || blob->size == 0u ||
      limits == NULL || sha256 == NULL || runner == NULL ||
      runner->run_verified == NULL) {
    return result;
  }
  memory.data = blob->data;
  memory.size = blob->size;
  reader.context = &memory;
  reader.size_bytes = blob->size;
  reader.read_at = read_memory;
  result.protocol_error = typx_protocol_v1_verify(
      &reader, limits, sha256, &schedule);
  if (result.protocol_error != TYPX_PROTOCOL_OK) {
    return result;
  }
  result.execution_started = true;
  result.executor_error = runner->run_verified(runner->context, &schedule);
  return result;
}
