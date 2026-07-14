#ifndef TYPX_EXECUTOR_H
#define TYPX_EXECUTOR_H

#include "typx_protocol.h"

typedef enum {
  TYPX_EXECUTOR_OK = 0,
  TYPX_EXECUTOR_NOT_VERIFIED,
  TYPX_EXECUTOR_CLOCK_ERROR,
  TYPX_EXECUTOR_WAIT_FAILED,
  TYPX_EXECUTOR_KEY_DOWN_FAILED,
  TYPX_EXECUTOR_RELEASE_FAILED,
  TYPX_EXECUTOR_STOPPED,
  TYPX_EXECUTOR_PROTOCOL_READ_FAILED,
  TYPX_EXECUTOR_INTERRUPTED_RESET
} typx_executor_error_t;

typedef enum {
  TYPX_TERMINAL_COMPLETED = 0,
  TYPX_TERMINAL_ABORTED,
  TYPX_TERMINAL_ERROR
} typx_terminal_status_t;

typedef uint64_t (*typx_now_us_fn)(void *context);
typedef bool (*typx_wait_us_fn)(void *context, uint32_t delay_us);
typedef bool (*typx_key_down_fn)(
    void *context, uint8_t modifier, uint8_t keycode);
typedef bool (*typx_release_all_fn)(void *context);
typedef bool (*typx_stop_requested_fn)(void *context);
typedef void (*typx_progress_fn)(
    void *context,
    uint32_t source_index,
    uint32_t completed_records,
    uint32_t total_records);
typedef void (*typx_terminal_fn)(
    void *context,
    typx_terminal_status_t status,
    typx_executor_error_t error,
    typx_protocol_error_t protocol_error);

typedef struct {
  void *context;
  typx_now_us_fn now_us;
  typx_wait_us_fn wait_us;
  typx_key_down_fn key_down;
  typx_release_all_fn release_all;
  typx_stop_requested_fn stop_requested;
  typx_progress_fn progress;
  typx_terminal_fn terminal;
} typx_executor_io_t;

typx_executor_error_t typx_executor_run(
    const typx_verified_schedule_v1_t *schedule,
    const typx_executor_io_t *io);

const char *typx_executor_error_code(typx_executor_error_t error);

#endif
