#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "typx_executor.h"
#include "typx_hid_runtime.h"
#include "typx_local_schedule.h"
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
  uint64_t now_us;
  uint32_t action_count;
  unsigned run_calls;
  unsigned key_down_count;
  unsigned release_count;
  unsigned hold8_count;
  bool key_is_down;
  bool ordering_valid;
} fake_execution_t;

static uint64_t fake_now(void *context) {
  return ((fake_execution_t *)context)->now_us;
}

static bool fake_wait(void *context, uint32_t delay_us) {
  fake_execution_t *fake = context;
  fake->now_us += delay_us;
  if (delay_us == 8000u) {
    ++fake->hold8_count;
  }
  return true;
}

static bool fake_key_down(
    void *context, uint8_t modifier, uint8_t keycode) {
  fake_execution_t *fake = context;
  (void)modifier;
  (void)keycode;
  if (fake->key_is_down) {
    fake->ordering_valid = false;
  }
  fake->key_is_down = true;
  ++fake->key_down_count;
  return true;
}

static bool fake_release(void *context) {
  fake_execution_t *fake = context;
  fake->key_is_down = false;
  ++fake->release_count;
  return true;
}

static bool fake_stop(void *context) {
  (void)context;
  return false;
}

static void fake_progress(
    void *context, uint32_t source, uint32_t done, uint32_t total) {
  (void)context;
  (void)source;
  (void)done;
  (void)total;
}

static void fake_terminal(
    void *context,
    typx_terminal_status_t status,
    typx_executor_error_t error,
    typx_protocol_error_t protocol_error) {
  (void)context;
  (void)status;
  (void)error;
  (void)protocol_error;
}

static typx_executor_error_t run_verified(
    void *context, const typx_verified_schedule_v1_t *schedule) {
  fake_execution_t *fake = context;
  typx_executor_io_t io = {
    .context = fake,
    .now_us = fake_now,
    .wait_us = fake_wait,
    .key_down = fake_key_down,
    .release_all = fake_release,
    .stop_requested = fake_stop,
    .progress = fake_progress,
    .terminal = fake_terminal
  };
  ++fake->run_calls;
  fake->action_count = schedule->header.action_count;
  return typx_executor_run(schedule, &io);
}

static typx_local_schedule_blob_t load_blob(const char *path) {
  typx_local_schedule_blob_t blob = {0};
  FILE *file = fopen(path, "rb");
  long size;
  CHECK(file != NULL);
  if (file == NULL) {
    return blob;
  }
  CHECK(fseek(file, 0, SEEK_END) == 0);
  size = ftell(file);
  CHECK(size > 0);
  CHECK(fseek(file, 0, SEEK_SET) == 0);
  if (size > 0) {
    uint8_t *data = malloc((size_t)size);
    CHECK(data != NULL);
    if (data != NULL) {
      CHECK(fread(data, 1u, (size_t)size, file) == (size_t)size);
      blob.data = data;
      blob.size = (size_t)size;
    }
  }
  fclose(file);
  return blob;
}

static typx_local_schedule_result_t verify_and_run(
    const typx_local_schedule_blob_t *blob, fake_execution_t *fake) {
  typx_protocol_limits_t limits = typx_protocol_v1_esp32_cam_limits();
  typx_sha256_portable_context_t sha_context;
  typx_sha256_provider_t sha = typx_sha256_portable_provider(&sha_context);
  typx_local_schedule_runner_t runner = {fake, run_verified};
  fake->ordering_valid = true;
  return typx_local_schedule_verify_and_run(blob, &limits, &sha, &runner);
}

static void test_valid_schedule(
    const char *path, uint32_t expected_actions, uint64_t expected_duration) {
  typx_local_schedule_blob_t blob = load_blob(path);
  fake_execution_t fake = {0};
  typx_local_schedule_result_t result = verify_and_run(&blob, &fake);
  CHECK(result.protocol_error == TYPX_PROTOCOL_OK);
  CHECK(result.execution_started);
  CHECK(result.executor_error == TYPX_EXECUTOR_OK);
  CHECK(fake.run_calls == 1u);
  CHECK(fake.action_count == expected_actions);
  CHECK(fake.key_down_count == expected_actions);
  CHECK(fake.release_count == expected_actions + 1u);
  CHECK(fake.hold8_count == expected_actions);
  CHECK(fake.now_us == expected_duration);
  CHECK(fake.ordering_valid);
  CHECK(!fake.key_is_down);
  free((void *)blob.data);
}

static void test_invalid_never_executes(void) {
  typx_local_schedule_blob_t blob = load_blob(
      "targets/esp32_cam/main/schedules/standard.bin");
  fake_execution_t fake = {0};
  typx_local_schedule_result_t result;
  uint8_t *mutable_data = (uint8_t *)blob.data;
  mutable_data[blob.size - 1u] ^= 0x01u;
  result = verify_and_run(&blob, &fake);
  CHECK(result.protocol_error == TYPX_PROTOCOL_CHECKSUM_MISMATCH);
  CHECK(!result.execution_started);
  CHECK(fake.run_calls == 0u);

  mutable_data[blob.size - 1u] ^= 0x01u;
  --blob.size;
  result = verify_and_run(&blob, &fake);
  CHECK(result.protocol_error == TYPX_PROTOCOL_PAYLOAD_SIZE_INVALID);
  CHECK(!result.execution_started);
  CHECK(fake.run_calls == 0u);
  free(mutable_data);
}

static void test_run_commands(void) {
  static const char *standard[] = {"run-standard"};
  static const char *personal[] = {"run-personal"};
  static const char *extra[] = {"run-standard", "now"};
  typx_console_command_t command;
  CHECK(typx_console_parse_argv(1, standard, &command));
  CHECK(command.type == TYPX_COMMAND_RUN_STANDARD);
  CHECK(typx_console_parse_argv(1, personal, &command));
  CHECK(command.type == TYPX_COMMAND_RUN_PERSONAL);
  CHECK(!typx_console_parse_argv(2, extra, &command));
}

int main(void) {
  test_valid_schedule(
      "targets/esp32_cam/main/schedules/standard.bin", 11u, 2018000u);
  test_valid_schedule(
      "targets/esp32_cam/main/schedules/personal.bin", 14u, 2422812u);
  test_invalid_never_executes();
  test_run_commands();
  printf(
      "Local schedule tests: %u assertions, %u failures\n",
      assertions, failures);
  return failures == 0u ? 0 : 1;
}
