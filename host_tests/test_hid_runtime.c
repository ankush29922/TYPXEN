#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "typx_executor.h"
#include "typx_hid_runtime.h"
#include "typx_protocol.h"
#include "typx_sha256.h"

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
  uint8_t reports[64][8];
  unsigned report_count;
  unsigned send_calls;
  unsigned fail_send_call;
  uint32_t waits[128];
  unsigned wait_count;
  unsigned total_wait_calls;
  unsigned countdown_waits;
  unsigned hold8_waits;
  unsigned gap60_waits;
  unsigned countdowns[5];
  unsigned countdown_count;
  typx_hid_runtime_t *runtime;
  unsigned stop_after_wait;
  unsigned fail_wait_call;
} fake_hid_t;

static bool fake_send(void *context, const uint8_t report[8]) {
  fake_hid_t *fake = context;
  ++fake->send_calls;
  if (fake->send_calls == fake->fail_send_call) {
    return false;
  }
  if (fake->report_count < 64u) {
    memcpy(fake->reports[fake->report_count++], report, 8u);
  }
  return true;
}

static bool fake_wait(void *context, uint32_t delay_ms) {
  fake_hid_t *fake = context;
  ++fake->total_wait_calls;
  if (delay_ms == 1000u) ++fake->countdown_waits;
  if (delay_ms == 8u) ++fake->hold8_waits;
  if (delay_ms == 60u) ++fake->gap60_waits;
  if (fake->wait_count < 128u) {
    fake->waits[fake->wait_count++] = delay_ms;
  }
  if (fake->wait_count == fake->fail_wait_call) {
    return false;
  }
  if (fake->stop_after_wait != 0u &&
      fake->wait_count == fake->stop_after_wait) {
    typx_hid_runtime_request_stop(fake->runtime);
  }
  return true;
}

static void fake_countdown(void *context, unsigned seconds) {
  fake_hid_t *fake = context;
  if (fake->countdown_count < 5u) {
    fake->countdowns[fake->countdown_count++] = seconds;
  }
}

static void make_runtime(fake_hid_t *fake, typx_hid_runtime_t *runtime) {
  typx_hid_runtime_io_t io = {
    .context = fake,
    .send_report = fake_send,
    .wait_ms = fake_wait,
    .countdown = fake_countdown
  };
  memset(fake, 0, sizeof(*fake));
  typx_hid_runtime_init(runtime, &io);
  fake->runtime = runtime;
}

static bool is_release(const uint8_t report[8]) {
  static const uint8_t empty[8] = {0};
  return memcmp(report, empty, sizeof(empty)) == 0;
}

static void test_no_automatic_key_and_connection_state(void) {
  fake_hid_t fake;
  typx_hid_runtime_t runtime;
  make_runtime(&fake, &runtime);
  CHECK(fake.report_count == 0u);
  CHECK(!runtime.connected);
  CHECK(typx_hid_runtime_can_clear_bonds(&runtime));
  typx_hid_runtime_set_connected(&runtime, true);
  CHECK(runtime.connected);
  CHECK(!typx_hid_runtime_can_clear_bonds(&runtime));
  CHECK(fake.report_count == 0u);
  typx_hid_runtime_set_connected(&runtime, false);
  CHECK(!runtime.connected);
  CHECK(runtime.stop_requested);
  CHECK(typx_hid_runtime_can_clear_bonds(&runtime));
  CHECK(fake.report_count == 0u);
}

static void test_disconnected_and_busy_refusal(void) {
  fake_hid_t fake;
  typx_hid_runtime_t runtime;
  make_runtime(&fake, &runtime);
  CHECK(typx_hid_run_test(&runtime, 8u, 1u) == TYPX_HID_DISCONNECTED);
  CHECK(fake.report_count == 0u);
  typx_hid_runtime_set_connected(&runtime, true);
  runtime.running = true;
  CHECK(typx_hid_run_test(&runtime, 8u, 1u) == TYPX_HID_BUSY);
  CHECK(fake.report_count == 0u);
}

static void test_key_release_count_and_countdown(void) {
  fake_hid_t fake;
  typx_hid_runtime_t runtime;
  make_runtime(&fake, &runtime);
  typx_hid_runtime_set_connected(&runtime, true);
  CHECK(typx_hid_run_test(&runtime, 8u, 2u) == TYPX_HID_OK);
  CHECK(fake.report_count == 6u);
  CHECK(is_release(fake.reports[0]));
  CHECK(fake.reports[1][2] == 0x04u);
  CHECK(is_release(fake.reports[2]));
  CHECK(fake.reports[3][2] == 0x05u);
  CHECK(is_release(fake.reports[4]));
  CHECK(is_release(fake.reports[5]));
  CHECK(fake.countdown_count == 5u);
  CHECK(fake.countdowns[0] == 5u && fake.countdowns[4] == 1u);
  CHECK(fake.wait_count == 9u);
  CHECK(fake.waits[5] == 8u && fake.waits[6] == 60u);
  CHECK(fake.waits[7] == 8u && fake.waits[8] == 60u);
}

static void test_exact_count_has_no_newline(void) {
  fake_hid_t fake;
  typx_hid_runtime_t runtime;
  make_runtime(&fake, &runtime);
  unsigned index;
  typx_hid_runtime_set_connected(&runtime, true);
  CHECK(typx_hid_run_test(&runtime, 12u, 36u) == TYPX_HID_OK);
  CHECK(fake.report_count == 64u); /* Capture is bounded; send count is not. */
  CHECK(fake.send_calls == 74u); /* begin + 36 down/release pairs + end */
  for (index = 0; index < fake.report_count; ++index) {
    CHECK(fake.reports[index][2] != 0x28u);
  }
}

static void test_full_calibration_timing_contract(void) {
  fake_hid_t fake;
  typx_hid_runtime_t runtime;
  make_runtime(&fake, &runtime);
  typx_hid_runtime_set_connected(&runtime, true);
  CHECK(typx_hid_run_test(&runtime, 8u, 144u) == TYPX_HID_OK);
  CHECK(fake.countdown_count == 5u);
  CHECK(fake.countdown_waits == 5u);
  CHECK(fake.hold8_waits == 144u);
  CHECK(fake.gap60_waits == 144u);
  CHECK(fake.total_wait_calls == 293u);
  CHECK(fake.send_calls == 290u);
  CHECK(!runtime.running);
}

static void test_stop_and_failure_release(void) {
  fake_hid_t fake;
  typx_hid_runtime_t runtime;
  make_runtime(&fake, &runtime);
  typx_hid_runtime_set_connected(&runtime, true);
  fake.stop_after_wait = 6u; /* Five countdown waits, then the first hold. */
  CHECK(typx_hid_run_test(&runtime, 8u, 4u) == TYPX_HID_STOPPED);
  CHECK(fake.report_count >= 3u);
  CHECK(is_release(fake.reports[fake.report_count - 1u]));

  make_runtime(&fake, &runtime);
  typx_hid_runtime_set_connected(&runtime, true);
  fake.fail_send_call = 2u; /* First key-down fails. */
  CHECK(typx_hid_run_test(&runtime, 8u, 1u) == TYPX_HID_SEND_FAILED);
  CHECK(fake.send_calls >= 3u);
  CHECK(is_release(fake.reports[fake.report_count - 1u]));

  make_runtime(&fake, &runtime);
  typx_hid_runtime_set_connected(&runtime, true);
  fake.fail_wait_call = 7u; /* First wait after release fails. */
  CHECK(typx_hid_run_test(&runtime, 8u, 1u) == TYPX_HID_WAIT_FAILED);
  CHECK(fake.report_count >= 4u);
  CHECK(is_release(fake.reports[fake.report_count - 1u]));
}

static void test_shift_sequence(void) {
  fake_hid_t fake;
  typx_hid_runtime_t runtime;
  make_runtime(&fake, &runtime);
  typx_hid_runtime_set_connected(&runtime, true);
  CHECK(typx_hid_run_shift_test(&runtime, 20u) == TYPX_HID_OK);
  CHECK(fake.send_calls == 14u);
  CHECK(fake.reports[1][0] == 0x02u && fake.reports[1][2] == 0x04u);
  CHECK(fake.reports[3][0] == 0u && fake.reports[3][2] == 0x04u);
  CHECK(fake.reports[5][0] == 0x02u && fake.reports[5][2] == 0x1eu);
}

static void test_command_parser(void) {
  static const char *help[] = {"help"};
  static const char *status[] = {"status"};
  static const char *release[] = {"release"};
  static const char *stop[] = {"stop"};
  static const char *clear_bonds[] = {"clear-bonds"};
  static const char *test[] = {"test", "8", "144"};
  static const char *test_shift[] = {"test-shift", "30"};
  static const char *bad_hold[] = {"test", "9", "144"};
  static const char *bad_count[] = {"test", "8", "1081"};
  static const char *bad_number[] = {"test", "eight", "144"};
  static const char *extra[] = {"status", "extra"};
  static const char *unknown[] = {"unknown"};
  typx_console_command_t command;
  CHECK(typx_console_parse("help", &command));
  CHECK(command.type == TYPX_COMMAND_HELP);
  CHECK(typx_console_parse("test 8 144", &command));
  CHECK(command.type == TYPX_COMMAND_TEST);
  CHECK(command.hold_ms == 8u && command.character_count == 144u);
  CHECK(typx_console_parse("test-shift 30", &command));
  CHECK(command.type == TYPX_COMMAND_TEST_SHIFT && command.hold_ms == 30u);
  CHECK(!typx_console_parse("test 9 144", &command));
  CHECK(!typx_console_parse("test 8 0", &command));
  CHECK(!typx_console_parse("test 8 1081", &command));
  CHECK(!typx_console_parse("test 8", &command));
  CHECK(!typx_console_parse("status extra", &command));
  CHECK(!typx_console_parse("clear-bonds now", &command));
  CHECK(typx_hid_hold_supported(8u) && typx_hid_hold_supported(30u));
  CHECK(!typx_hid_hold_supported(7u));
  CHECK(typx_console_parse_argv(1, help, &command));
  CHECK(command.type == TYPX_COMMAND_HELP);
  CHECK(typx_console_parse_argv(1, status, &command));
  CHECK(command.type == TYPX_COMMAND_STATUS);
  CHECK(typx_console_parse_argv(1, release, &command));
  CHECK(command.type == TYPX_COMMAND_RELEASE);
  CHECK(typx_console_parse_argv(1, stop, &command));
  CHECK(command.type == TYPX_COMMAND_STOP);
  CHECK(typx_console_parse_argv(1, clear_bonds, &command));
  CHECK(command.type == TYPX_COMMAND_CLEAR_BONDS);
  CHECK(typx_console_parse_argv(3, test, &command));
  CHECK(command.type == TYPX_COMMAND_TEST);
  CHECK(command.hold_ms == 8u && command.character_count == 144u);
  CHECK(typx_console_parse_argv(2, test_shift, &command));
  CHECK(command.type == TYPX_COMMAND_TEST_SHIFT && command.hold_ms == 30u);
  CHECK(!typx_console_parse_argv(3, bad_hold, &command));
  CHECK(!typx_console_parse_argv(3, bad_count, &command));
  CHECK(!typx_console_parse_argv(3, bad_number, &command));
  CHECK(!typx_console_parse_argv(2, extra, &command));
  CHECK(!typx_console_parse_argv(1, unknown, &command));
  CHECK(!typx_console_parse_argv(0, NULL, &command));
}

typedef struct {
  FILE *file;
} file_reader_t;

static bool read_at(
    void *context, uint64_t offset, uint8_t *destination, size_t length) {
  file_reader_t *reader = context;
  return fseek(reader->file, (long)offset, SEEK_SET) == 0 &&
      fread(destination, 1u, length, reader->file) == length;
}

typedef struct {
  uint64_t now_us;
  uint32_t waits[256];
  unsigned wait_count;
  unsigned down_count;
  unsigned release_count;
  unsigned progress_count;
  unsigned stop_on_wait_call;
  bool stop_after_down;
  bool stop_requested;
} executor_fake_t;

static uint64_t exec_now(void *context) {
  return ((executor_fake_t *)context)->now_us;
}

static bool exec_wait(void *context, uint32_t delay_us) {
  executor_fake_t *fake = context;
  fake->waits[fake->wait_count++] = delay_us;
  fake->now_us += delay_us;
  if (fake->stop_on_wait_call != 0u &&
      fake->wait_count == fake->stop_on_wait_call) {
    fake->stop_requested = true;
    return false;
  }
  return true;
}

static bool exec_down(void *context, uint8_t modifier, uint8_t keycode) {
  executor_fake_t *fake = context;
  (void)modifier;
  (void)keycode;
  ++fake->down_count;
  if (fake->stop_after_down) fake->stop_requested = true;
  return true;
}

static bool exec_release(void *context) {
  ++((executor_fake_t *)context)->release_count;
  return true;
}

static bool exec_stop(void *context) {
  return ((executor_fake_t *)context)->stop_requested;
}
static void exec_progress(void *context, uint32_t source, uint32_t done, uint32_t total) {
  executor_fake_t *fake = context;
  (void)source; (void)done; (void)total;
  ++fake->progress_count;
}
static void exec_terminal(void *context, typx_terminal_status_t status,
    typx_executor_error_t error, typx_protocol_error_t protocol) {
  (void)context; (void)status; (void)error; (void)protocol;
}

static void exercise_schedule(const char *path) {
  file_reader_t reader_context;
  typx_reader_t reader;
  typx_protocol_limits_t limits = typx_protocol_v1_generic_limits();
  typx_sha256_portable_context_t sha_context;
  typx_sha256_provider_t sha = typx_sha256_portable_provider(&sha_context);
  typx_verified_schedule_v1_t schedule;
  executor_fake_t fake = {0};
  typx_executor_io_t io = {
    .context = &fake,
    .now_us = exec_now,
    .wait_us = exec_wait,
    .key_down = exec_down,
    .release_all = exec_release,
    .stop_requested = exec_stop,
    .progress = exec_progress,
    .terminal = exec_terminal
  };
  typx_protocol_record_v1_t first;
  uint32_t nonzero_wait_index = 0u;
  reader_context.file = fopen(path, "rb");
  CHECK(reader_context.file != NULL);
  if (reader_context.file == NULL) return;
  fseek(reader_context.file, 0, SEEK_END);
  reader.context = &reader_context;
  reader.size_bytes = (uint64_t)ftell(reader_context.file);
  reader.read_at = read_at;
  CHECK(typx_protocol_v1_verify(&reader, &limits, &sha, &schedule) == TYPX_PROTOCOL_OK);
  CHECK(typx_protocol_v1_read_record(&schedule, 0u, &first) == TYPX_PROTOCOL_OK);
  CHECK(typx_executor_run(&schedule, &io) == TYPX_EXECUTOR_OK);
  CHECK(fake.down_count == schedule.header.action_count);
  CHECK(fake.release_count == schedule.header.action_count + 1u);
  if (first.wait_before_key_down_us != 0u) {
    CHECK(fake.waits[nonzero_wait_index++] == first.wait_before_key_down_us);
  }
  if (first.key_down_hold_us != 0u) {
    CHECK(fake.waits[nonzero_wait_index] == first.key_down_hold_us);
  }
  fclose(reader_context.file);
}

static void test_standard_and_personal_use_executor_sink(void) {
  exercise_schedule("test_vectors/protocol-v1/small-standard.bin");
  exercise_schedule("test_vectors/protocol-v1/deterministic-personal.bin");
}

static void test_executor_stop_boundaries(void) {
  file_reader_t reader_context;
  typx_reader_t reader;
  typx_protocol_limits_t limits = typx_protocol_v1_generic_limits();
  typx_sha256_portable_context_t sha_context;
  typx_sha256_provider_t sha = typx_sha256_portable_provider(&sha_context);
  typx_verified_schedule_v1_t schedule;
  executor_fake_t fake = {0};
  typx_executor_io_t io = {
    .context = &fake,
    .now_us = exec_now,
    .wait_us = exec_wait,
    .key_down = exec_down,
    .release_all = exec_release,
    .stop_requested = exec_stop,
    .progress = exec_progress,
    .terminal = exec_terminal
  };
  reader_context.file = fopen(
      "test_vectors/protocol-v1/small-standard.bin", "rb");
  CHECK(reader_context.file != NULL);
  if (reader_context.file == NULL) return;
  fseek(reader_context.file, 0, SEEK_END);
  reader.context = &reader_context;
  reader.size_bytes = (uint64_t)ftell(reader_context.file);
  reader.read_at = read_at;
  CHECK(typx_protocol_v1_verify(
      &reader, &limits, &sha, &schedule) == TYPX_PROTOCOL_OK);

  fake.stop_requested = true;
  CHECK(typx_executor_run(&schedule, &io) == TYPX_EXECUTOR_STOPPED);
  CHECK(fake.down_count == 0u);
  CHECK(fake.progress_count == 0u);
  CHECK(fake.release_count >= 1u);

  memset(&fake, 0, sizeof(fake));
  fake.stop_on_wait_call = 2u;
  CHECK(typx_executor_run(&schedule, &io) == TYPX_EXECUTOR_STOPPED);
  CHECK(fake.down_count == 1u);
  CHECK(fake.progress_count == 1u);

  memset(&fake, 0, sizeof(fake));
  fake.stop_after_down = true;
  CHECK(typx_executor_run(&schedule, &io) == TYPX_EXECUTOR_STOPPED);
  CHECK(fake.down_count == 1u);
  CHECK(fake.progress_count == 0u);
  CHECK(fake.release_count >= 1u);
  fclose(reader_context.file);
}

int main(void) {
  test_no_automatic_key_and_connection_state();
  test_disconnected_and_busy_refusal();
  test_key_release_count_and_countdown();
  test_exact_count_has_no_newline();
  test_full_calibration_timing_contract();
  test_stop_and_failure_release();
  test_shift_sequence();
  test_command_parser();
  test_standard_and_personal_use_executor_sink();
  test_executor_stop_boundaries();
  printf("HID runtime tests: %u assertions, %u failures\n", assertions, failures);
  return failures == 0u ? 0 : 1;
}
