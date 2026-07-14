#ifndef TYPX_LOCAL_SCHEDULE_H
#define TYPX_LOCAL_SCHEDULE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "typx_executor.h"
#include "typx_protocol.h"

typedef struct {
  const uint8_t *data;
  size_t size;
} typx_local_schedule_blob_t;

typedef typx_executor_error_t (*typx_local_verified_runner_fn)(
    void *context, const typx_verified_schedule_v1_t *schedule);

typedef struct {
  void *context;
  typx_local_verified_runner_fn run_verified;
} typx_local_schedule_runner_t;

typedef struct {
  bool execution_started;
  typx_protocol_error_t protocol_error;
  typx_executor_error_t executor_error;
} typx_local_schedule_result_t;

typx_local_schedule_result_t typx_local_schedule_verify_and_run(
    const typx_local_schedule_blob_t *blob,
    const typx_protocol_limits_t *limits,
    const typx_sha256_provider_t *sha256,
    const typx_local_schedule_runner_t *runner);

#endif
