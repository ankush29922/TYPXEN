#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "typx_execution_safety.h"

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
  bool active;
  bool fail_read;
  bool fail_write;
  unsigned writes;
} fake_io_t;

static bool read_marker(void *context, bool *active) {
  fake_io_t *io = context;
  if (io->fail_read) return false;
  *active = io->active;
  return true;
}

static bool write_marker(void *context, bool active) {
  fake_io_t *io = context;
  if (io->fail_write) return false;
  io->active = active;
  ++io->writes;
  return true;
}

static typx_execution_safety_t initialized(fake_io_t *io) {
  typx_execution_marker_io_t marker = {
    io, read_marker, write_marker};
  typx_execution_safety_t safety;
  CHECK(typx_execution_safety_init(&safety, &marker));
  return safety;
}

static typx_hid_safety_snapshot_t snapshot(
    const typx_execution_safety_t *safety) {
  typx_hid_safety_snapshot_t value;
  typx_execution_safety_snapshot(safety, &value);
  return value;
}

static void test_interrupted_boot_and_marker_lifecycle(void) {
  fake_io_t io = {.active = true};
  typx_execution_safety_t safety = initialized(&io);
  CHECK(typx_execution_safety_interrupted(&safety));
  CHECK(!io.active);
  CHECK(io.writes == 1u);
  CHECK(typx_execution_safety_begin(&safety));
  CHECK(io.active);
  CHECK(typx_execution_safety_finish(&safety));
  CHECK(!io.active);
}

static void test_connection_waits_for_authentication(void) {
  fake_io_t io = {0};
  typx_execution_safety_t safety = initialized(&io);
  uint32_t generation = typx_execution_safety_connected(&safety);
  typx_hid_safety_snapshot_t value = snapshot(&safety);
  CHECK(generation != 0u);
  CHECK(value.connected);
  CHECK(!value.authenticated);
  CHECK(!value.ready);
  CHECK(value.state == TYPX_HID_SAFETY_WAITING_FOR_AUTH);
  CHECK(value.error == TYPX_HID_SAFETY_ERROR_AUTH_PENDING);
  CHECK(!typx_execution_safety_begin_release(&safety, generation));
}

static void test_report_path_retry_and_confirmation(void) {
  fake_io_t io = {0};
  typx_execution_safety_t safety = initialized(&io);
  uint32_t generation = typx_execution_safety_connected(&safety);
  typx_execution_safety_authenticated(&safety, true);
  CHECK(snapshot(&safety).state == TYPX_HID_SAFETY_WAITING_FOR_REPORT_PATH);
  CHECK(typx_execution_safety_begin_release(&safety, generation));
  CHECK(!typx_execution_safety_complete_release(
      &safety, generation, false, true));
  CHECK(snapshot(&safety).state == TYPX_HID_SAFETY_FAILED_RETRYABLE);
  CHECK(snapshot(&safety).error ==
      TYPX_HID_SAFETY_ERROR_KEYBOARD_REPORT_FAILED);
  CHECK(typx_execution_safety_begin_release(&safety, generation));
  CHECK(typx_execution_safety_complete_release(
      &safety, generation, true, true));
  CHECK(snapshot(&safety).state == TYPX_HID_SAFETY_CONFIRMED);
  CHECK(snapshot(&safety).ready);
  CHECK(snapshot(&safety).attempts == 2u);
}

static void test_either_report_failure_blocks_start(void) {
  fake_io_t io = {0};
  typx_execution_safety_t safety = initialized(&io);
  uint32_t generation = typx_execution_safety_connected(&safety);
  typx_execution_safety_authenticated(&safety, true);
  CHECK(typx_execution_safety_begin_release(&safety, generation));
  CHECK(!typx_execution_safety_complete_release(
      &safety, generation, true, false));
  CHECK(!snapshot(&safety).ready);
  CHECK(snapshot(&safety).error ==
      TYPX_HID_SAFETY_ERROR_CONSUMER_REPORT_FAILED);
}

static void test_retry_is_bounded_and_manual_retry_resets_attempts(void) {
  fake_io_t io = {0};
  typx_execution_safety_t safety = initialized(&io);
  uint32_t generation = typx_execution_safety_connected(&safety);
  unsigned attempt;
  typx_execution_safety_authenticated(&safety, true);
  for (attempt = 0u; attempt < TYPX_EXECUTION_RELEASE_MAX_ATTEMPTS; ++attempt) {
    CHECK(typx_execution_safety_begin_release(&safety, generation));
    CHECK(!typx_execution_safety_complete_release(
        &safety, generation, false, false));
  }
  CHECK(!typx_execution_safety_begin_release(&safety, generation));
  CHECK(snapshot(&safety).attempts == TYPX_EXECUTION_RELEASE_MAX_ATTEMPTS);
  CHECK(typx_execution_safety_request_retry(&safety));
  CHECK(snapshot(&safety).attempts == 0u);
  CHECK(snapshot(&safety).state == TYPX_HID_SAFETY_WAITING_FOR_REPORT_PATH);
  CHECK(TYPX_EXECUTION_RELEASE_INITIAL_DELAY_MS >= 100u);
  CHECK(TYPX_EXECUTION_RELEASE_RETRY_DELAY_MS >= 100u);
}

static void test_disconnect_and_generation_reject_stale_retry(void) {
  fake_io_t io = {0};
  typx_execution_safety_t safety = initialized(&io);
  uint32_t old_generation = typx_execution_safety_connected(&safety);
  uint32_t new_generation;
  typx_execution_safety_authenticated(&safety, true);
  CHECK(typx_execution_safety_begin_release(&safety, old_generation));
  typx_execution_safety_disconnected(&safety);
  CHECK(snapshot(&safety).state == TYPX_HID_SAFETY_DISCONNECTED);
  CHECK(!typx_execution_safety_complete_release(
      &safety, old_generation, true, true));
  new_generation = typx_execution_safety_connected(&safety);
  typx_execution_safety_authenticated(&safety, true);
  CHECK(new_generation != old_generation);
  CHECK(!typx_execution_safety_begin_release(&safety, old_generation));
  CHECK(typx_execution_safety_begin_release(&safety, new_generation));
  CHECK(!typx_execution_safety_complete_release(
      &safety, old_generation, true, true));
  CHECK(!snapshot(&safety).ready);
  CHECK(typx_execution_safety_complete_release(
      &safety, new_generation, true, true));
  CHECK(snapshot(&safety).ready);
}

static void test_authentication_failure_is_visible_and_retryable(void) {
  fake_io_t io = {0};
  typx_execution_safety_t safety = initialized(&io);
  (void)typx_execution_safety_connected(&safety);
  typx_execution_safety_authenticated(&safety, false);
  CHECK(snapshot(&safety).state == TYPX_HID_SAFETY_FAILED_RETRYABLE);
  CHECK(snapshot(&safety).error == TYPX_HID_SAFETY_ERROR_AUTH_FAILED);
  CHECK(!typx_execution_safety_request_retry(&safety));
}

static void test_status_codes_and_timing_contract(void) {
  CHECK(strcmp(typx_hid_safety_state_code(
      TYPX_HID_SAFETY_CONFIRMED), "CONFIRMED") == 0);
  CHECK(strcmp(typx_hid_safety_error_code(
      TYPX_HID_SAFETY_ERROR_REPORTS_FAILED), "REPORTS_FAILED") == 0);
  CHECK(TYPX_DEDICATED_COUNTDOWN_MS == 8000u);
  CHECK(TYPX_BLE_SUPERVISION_TIMEOUT_MS == 3000u);
}

int main(void) {
  test_interrupted_boot_and_marker_lifecycle();
  test_connection_waits_for_authentication();
  test_report_path_retry_and_confirmation();
  test_either_report_failure_blocks_start();
  test_retry_is_bounded_and_manual_retry_resets_attempts();
  test_disconnect_and_generation_reject_stale_retry();
  test_authentication_failure_is_visible_and_retryable();
  test_status_codes_and_timing_contract();
  printf(
      "Execution safety tests: %u assertions, %u failures\n",
      assertions, failures);
  return failures == 0u ? 0 : 1;
}
