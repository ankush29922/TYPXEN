#include "typx_executor.h"

static bool io_valid(const typx_executor_io_t *io) {
  return io != NULL && io->now_us != NULL && io->wait_us != NULL &&
      io->key_down != NULL && io->release_all != NULL &&
      io->stop_requested != NULL && io->progress != NULL &&
      io->terminal != NULL;
}

static bool release_best_effort(const typx_executor_io_t *io) {
  return io->release_all(io->context);
}

static typx_executor_error_t finish(
    const typx_executor_io_t *io,
    typx_terminal_status_t status,
    typx_executor_error_t error,
    typx_protocol_error_t protocol_error) {
  bool released = release_best_effort(io);
  if (!released) {
    status = TYPX_TERMINAL_ERROR;
    error = TYPX_EXECUTOR_RELEASE_FAILED;
  }
  io->terminal(io->context, status, error, protocol_error);
  return error;
}

static typx_executor_error_t wait_checked(
    const typx_executor_io_t *io, uint32_t delay_us) {
  uint64_t before;
  uint64_t after;
  if (delay_us == 0u) {
    return TYPX_EXECUTOR_OK;
  }
  if (io->stop_requested(io->context)) {
    return TYPX_EXECUTOR_STOPPED;
  }
  before = io->now_us(io->context);
  if (!io->wait_us(io->context, delay_us)) {
    return io->stop_requested(io->context)
        ? TYPX_EXECUTOR_STOPPED
        : TYPX_EXECUTOR_WAIT_FAILED;
  }
  after = io->now_us(io->context);
  return after < before
      ? TYPX_EXECUTOR_CLOCK_ERROR
      : TYPX_EXECUTOR_OK;
}

typx_executor_error_t typx_executor_run(
    const typx_verified_schedule_v1_t *schedule,
    const typx_executor_io_t *io) {
  uint32_t index;
  if (!io_valid(io)) {
    return TYPX_EXECUTOR_WAIT_FAILED;
  }
  if (!typx_protocol_v1_is_verified(schedule)) {
    return finish(
        io,
        TYPX_TERMINAL_ERROR,
        TYPX_EXECUTOR_NOT_VERIFIED,
        TYPX_PROTOCOL_NOT_VERIFIED);
  }

  for (index = 0; index < schedule->header.action_count; ++index) {
    typx_protocol_record_v1_t record;
    typx_protocol_error_t protocol_error;
    typx_executor_error_t wait_error;
    if (io->stop_requested(io->context)) {
      return finish(
          io, TYPX_TERMINAL_ABORTED, TYPX_EXECUTOR_STOPPED, TYPX_PROTOCOL_OK);
    }
    protocol_error = typx_protocol_v1_read_record(schedule, index, &record);
    if (protocol_error != TYPX_PROTOCOL_OK) {
      return finish(
          io,
          TYPX_TERMINAL_ERROR,
          TYPX_EXECUTOR_PROTOCOL_READ_FAILED,
          protocol_error);
    }

    wait_error = wait_checked(io, record.wait_before_key_down_us);
    if (wait_error != TYPX_EXECUTOR_OK) {
      return finish(
          io,
          wait_error == TYPX_EXECUTOR_STOPPED
              ? TYPX_TERMINAL_ABORTED
              : TYPX_TERMINAL_ERROR,
          wait_error,
          TYPX_PROTOCOL_OK);
    }
    if (io->stop_requested(io->context)) {
      return finish(
          io, TYPX_TERMINAL_ABORTED, TYPX_EXECUTOR_STOPPED, TYPX_PROTOCOL_OK);
    }
    if (!io->key_down(io->context, record.modifier, record.keycode)) {
      return finish(
          io,
          TYPX_TERMINAL_ERROR,
          TYPX_EXECUTOR_KEY_DOWN_FAILED,
          TYPX_PROTOCOL_OK);
    }
    wait_error = wait_checked(io, record.key_down_hold_us);
    if (wait_error != TYPX_EXECUTOR_OK) {
      return finish(
          io,
          wait_error == TYPX_EXECUTOR_STOPPED
              ? TYPX_TERMINAL_ABORTED
              : TYPX_TERMINAL_ERROR,
          wait_error,
          TYPX_PROTOCOL_OK);
    }
    if (!io->release_all(io->context)) {
      release_best_effort(io);
      io->terminal(
          io->context,
          TYPX_TERMINAL_ERROR,
          TYPX_EXECUTOR_RELEASE_FAILED,
          TYPX_PROTOCOL_OK);
      return TYPX_EXECUTOR_RELEASE_FAILED;
    }
    io->progress(
        io->context,
        record.source_index,
        index + 1u,
        schedule->header.action_count);

    if (io->stop_requested(io->context)) {
      return finish(
          io, TYPX_TERMINAL_ABORTED, TYPX_EXECUTOR_STOPPED, TYPX_PROTOCOL_OK);
    }

    wait_error = wait_checked(io, record.wait_after_release_us);
    if (wait_error != TYPX_EXECUTOR_OK) {
      return finish(
          io,
          wait_error == TYPX_EXECUTOR_STOPPED
              ? TYPX_TERMINAL_ABORTED
              : TYPX_TERMINAL_ERROR,
          wait_error,
          TYPX_PROTOCOL_OK);
    }
  }
  return finish(
      io, TYPX_TERMINAL_COMPLETED, TYPX_EXECUTOR_OK, TYPX_PROTOCOL_OK);
}

const char *typx_executor_error_code(typx_executor_error_t error) {
  static const char *codes[] = {
      "OK",
      "NOT_VERIFIED",
      "CLOCK_ERROR",
      "WAIT_FAILED",
      "KEY_DOWN_FAILED",
      "RELEASE_FAILED",
      "STOPPED",
      "PROTOCOL_READ_FAILED",
      "INTERRUPTED_RESET"};
  size_t count = sizeof(codes) / sizeof(codes[0]);
  return (unsigned)error < count ? codes[error] : "UNKNOWN_EXECUTOR_ERROR";
}
