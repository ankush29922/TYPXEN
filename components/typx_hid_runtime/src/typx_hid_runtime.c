#include "typx_hid_runtime.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char CALIBRATION_PATTERN[] =
    "abcdefghijklmnopqrstuvwxyz0123456789";

static bool io_valid(const typx_hid_runtime_io_t *io) {
  return io != NULL && io->send_report != NULL && io->wait_ms != NULL;
}

static typx_hid_error_t wait_ms(
    typx_hid_runtime_t *runtime, uint32_t delay_ms) {
  if (runtime->stop_requested || !runtime->connected) {
    return runtime->connected ? TYPX_HID_STOPPED : TYPX_HID_DISCONNECTED;
  }
  return runtime->io.wait_ms(runtime->io.context, delay_ms)
      ? TYPX_HID_OK
      : TYPX_HID_WAIT_FAILED;
}

static bool character_report(char character, uint8_t report[8]) {
  memset(report, 0, 8u);
  if (character >= 'a' && character <= 'z') {
    report[2] = (uint8_t)(4 + character - 'a');
    return true;
  }
  if (character >= 'A' && character <= 'Z') {
    report[0] = 0x02u;
    report[2] = (uint8_t)(4 + character - 'A');
    return true;
  }
  if (character >= '1' && character <= '9') {
    report[2] = (uint8_t)(0x1eu + character - '1');
    return true;
  }
  if (character == '0') {
    report[2] = 0x27u;
    return true;
  }
  switch (character) {
    case '!': report[0] = 0x02u; report[2] = 0x1eu; return true;
    case '_': report[0] = 0x02u; report[2] = 0x2du; return true;
    case '{': report[0] = 0x02u; report[2] = 0x2fu; return true;
    case '}': report[0] = 0x02u; report[2] = 0x30u; return true;
    default: return false;
  }
}

void typx_hid_runtime_init(
    typx_hid_runtime_t *runtime, const typx_hid_runtime_io_t *io) {
  if (runtime == NULL) {
    return;
  }
  memset(runtime, 0, sizeof(*runtime));
  if (io != NULL) {
    runtime->io = *io;
  }
}

void typx_hid_runtime_set_connected(
    typx_hid_runtime_t *runtime, bool connected) {
  if (runtime == NULL) {
    return;
  }
  runtime->connected = connected;
  if (!connected) {
    runtime->stop_requested = true;
    runtime->running = false;
  }
}

void typx_hid_runtime_request_stop(typx_hid_runtime_t *runtime) {
  if (runtime != NULL) {
    runtime->stop_requested = true;
  }
}

bool typx_hid_runtime_is_idle(const typx_hid_runtime_t *runtime) {
  return runtime != NULL && !runtime->running;
}

bool typx_hid_runtime_can_clear_bonds(const typx_hid_runtime_t *runtime) {
  return typx_hid_runtime_is_idle(runtime) && !runtime->connected;
}

typx_hid_error_t typx_hid_key_down(
    typx_hid_runtime_t *runtime, uint8_t modifier, uint8_t keycode) {
  uint8_t report[8] = {modifier, 0u, keycode, 0u, 0u, 0u, 0u, 0u};
  if (runtime == NULL || !io_valid(&runtime->io)) {
    return TYPX_HID_INVALID_ARGUMENT;
  }
  if (!runtime->connected) {
    return TYPX_HID_DISCONNECTED;
  }
  return runtime->io.send_report(runtime->io.context, report)
      ? TYPX_HID_OK
      : TYPX_HID_SEND_FAILED;
}

typx_hid_error_t typx_hid_release_all(typx_hid_runtime_t *runtime) {
  uint8_t report[8] = {0};
  unsigned attempt;
  if (runtime == NULL || !io_valid(&runtime->io)) {
    return TYPX_HID_INVALID_ARGUMENT;
  }
  if (!runtime->connected) {
    return TYPX_HID_DISCONNECTED;
  }
  for (attempt = 0; attempt < TYPX_HID_RELEASE_MAX_ATTEMPTS; ++attempt) {
    if (runtime->io.send_report(runtime->io.context, report)) {
      return TYPX_HID_OK;
    }
  }
  return TYPX_HID_SEND_FAILED;
}

static typx_hid_error_t send_character(
    typx_hid_runtime_t *runtime, char character, uint32_t hold_ms) {
  uint8_t report[8];
  typx_hid_error_t error;
  if (!character_report(character, report)) {
    return TYPX_HID_INVALID_ARGUMENT;
  }
  error = typx_hid_key_down(runtime, report[0], report[2]);
  if (error != TYPX_HID_OK) {
    (void)typx_hid_release_all(runtime);
    return error;
  }
  error = wait_ms(runtime, hold_ms);
  if (error != TYPX_HID_OK) {
    (void)typx_hid_release_all(runtime);
    return error;
  }
  error = typx_hid_release_all(runtime);
  if (error != TYPX_HID_OK) {
    (void)typx_hid_release_all(runtime);
    return error;
  }
  error = wait_ms(runtime, TYPX_CALIBRATION_WAIT_AFTER_RELEASE_MS);
  if (error != TYPX_HID_OK) {
    (void)typx_hid_release_all(runtime);
  }
  return error;
}

static typx_hid_error_t begin_test(typx_hid_runtime_t *runtime) {
  unsigned seconds;
  typx_hid_error_t error;
  if (runtime == NULL || !io_valid(&runtime->io)) {
    return TYPX_HID_INVALID_ARGUMENT;
  }
  if (!runtime->connected) {
    return TYPX_HID_DISCONNECTED;
  }
  if (runtime->running) {
    return TYPX_HID_BUSY;
  }
  runtime->running = true;
  runtime->stop_requested = false;
  error = typx_hid_release_all(runtime);
  if (error != TYPX_HID_OK) {
    runtime->running = false;
    return error;
  }
  for (seconds = 5u; seconds > 0u; --seconds) {
    if (runtime->io.countdown != NULL) {
      runtime->io.countdown(runtime->io.context, seconds);
    }
    error = wait_ms(runtime, 1000u);
    if (error != TYPX_HID_OK) {
      (void)typx_hid_release_all(runtime);
      runtime->running = false;
      return error;
    }
  }
  return TYPX_HID_OK;
}

static typx_hid_error_t finish_test(
    typx_hid_runtime_t *runtime, typx_hid_error_t error) {
  typx_hid_error_t release_error = typx_hid_release_all(runtime);
  runtime->running = false;
  runtime->stop_requested = false;
  if (error == TYPX_HID_OK && release_error != TYPX_HID_OK) {
    return release_error;
  }
  return error;
}

typx_hid_error_t typx_hid_run_test(
    typx_hid_runtime_t *runtime, uint32_t hold_ms, uint32_t character_count) {
  uint32_t index;
  typx_hid_error_t error;
  if (!typx_hid_hold_supported(hold_ms) || character_count == 0u ||
      character_count > TYPX_CALIBRATION_MAX_CHARACTERS) {
    return TYPX_HID_INVALID_ARGUMENT;
  }
  error = begin_test(runtime);
  if (error != TYPX_HID_OK) {
    return error;
  }
  for (index = 0; index < character_count; ++index) {
    if (runtime->stop_requested || !runtime->connected) {
      return finish_test(
          runtime,
          runtime->connected ? TYPX_HID_STOPPED : TYPX_HID_DISCONNECTED);
    }
    error = send_character(
        runtime,
        CALIBRATION_PATTERN[index % (sizeof(CALIBRATION_PATTERN) - 1u)],
        hold_ms);
    if (error != TYPX_HID_OK) {
      return finish_test(runtime, error);
    }
  }
  return finish_test(runtime, TYPX_HID_OK);
}

typx_hid_error_t typx_hid_run_shift_test(
    typx_hid_runtime_t *runtime, uint32_t hold_ms) {
  static const char sequence[] = "Aa!_{}";
  size_t index;
  typx_hid_error_t error;
  if (!typx_hid_hold_supported(hold_ms)) {
    return TYPX_HID_INVALID_ARGUMENT;
  }
  error = begin_test(runtime);
  if (error != TYPX_HID_OK) {
    return error;
  }
  for (index = 0; index < sizeof(sequence) - 1u; ++index) {
    if (runtime->stop_requested || !runtime->connected) {
      return finish_test(
          runtime,
          runtime->connected ? TYPX_HID_STOPPED : TYPX_HID_DISCONNECTED);
    }
    error = send_character(runtime, sequence[index], hold_ms);
    if (error != TYPX_HID_OK) {
      return finish_test(runtime, error);
    }
  }
  return finish_test(runtime, TYPX_HID_OK);
}

bool typx_hid_hold_supported(uint32_t hold_ms) {
  return hold_ms == 8u || hold_ms == 12u || hold_ms == 16u ||
      hold_ms == 20u || hold_ms == 30u;
}

static bool parse_uint(const char *text, uint32_t *value, int *consumed) {
  unsigned parsed;
  int length;
  if (sscanf(text, "%u%n", &parsed, &length) != 1) {
    return false;
  }
  *value = (uint32_t)parsed;
  *consumed = length;
  return true;
}

bool typx_console_parse(
    const char *line, typx_console_command_t *command) {
  char name[16];
  const char *cursor;
  int name_length = 0;
  int consumed;
  uint32_t first;
  uint32_t second;
  if (line == NULL || command == NULL ||
      sscanf(line, " %15s%n", name, &name_length) != 1) {
    return false;
  }
  memset(command, 0, sizeof(*command));
  cursor = line + name_length;
  while (isspace((unsigned char)*cursor)) {
    ++cursor;
  }
#define SIMPLE_COMMAND(text, value) \
  if (strcmp(name, text) == 0 && *cursor == '\0') { \
    command->type = value; return true; \
  }
  SIMPLE_COMMAND("help", TYPX_COMMAND_HELP)
  SIMPLE_COMMAND("status", TYPX_COMMAND_STATUS)
  SIMPLE_COMMAND("release", TYPX_COMMAND_RELEASE)
  SIMPLE_COMMAND("stop", TYPX_COMMAND_STOP)
  SIMPLE_COMMAND("clear-bonds", TYPX_COMMAND_CLEAR_BONDS)
  SIMPLE_COMMAND("run-standard", TYPX_COMMAND_RUN_STANDARD)
  SIMPLE_COMMAND("run-personal", TYPX_COMMAND_RUN_PERSONAL)
#undef SIMPLE_COMMAND
  if (strcmp(name, "test-shift") == 0) {
    if (!parse_uint(cursor, &first, &consumed)) {
      return false;
    }
    cursor += consumed;
    while (isspace((unsigned char)*cursor)) {
      ++cursor;
    }
    if (*cursor != '\0' || !typx_hid_hold_supported(first)) {
      return false;
    }
    command->type = TYPX_COMMAND_TEST_SHIFT;
    command->hold_ms = first;
    return true;
  }
  if (strcmp(name, "test") != 0 ||
      !parse_uint(cursor, &first, &consumed)) {
    return false;
  }
  cursor += consumed;
  while (isspace((unsigned char)*cursor)) {
    ++cursor;
  }
  if (!parse_uint(cursor, &second, &consumed)) {
    return false;
  }
  cursor += consumed;
  while (isspace((unsigned char)*cursor)) {
    ++cursor;
  }
  if (*cursor != '\0' || !typx_hid_hold_supported(first) || second == 0u ||
      second > TYPX_CALIBRATION_MAX_CHARACTERS) {
    return false;
  }
  command->type = TYPX_COMMAND_TEST;
  command->hold_ms = first;
  command->character_count = second;
  return true;
}

static bool parse_uint_argument(const char *text, uint32_t *value) {
  char *end;
  unsigned long parsed;
  if (text == NULL || *text == '\0') {
    return false;
  }
  errno = 0;
  parsed = strtoul(text, &end, 10);
  if (errno != 0 || *end != '\0' || parsed > UINT32_MAX) {
    return false;
  }
  *value = (uint32_t)parsed;
  return true;
}

bool typx_console_parse_argv(
    int argc,
    const char *const argv[],
    typx_console_command_t *command) {
  uint32_t hold_ms;
  uint32_t character_count;
  if (argc < 1 || argv == NULL || argv[0] == NULL || command == NULL) {
    return false;
  }
  memset(command, 0, sizeof(*command));
#define SIMPLE_ARGV_COMMAND(text, value) \
  if (strcmp(argv[0], text) == 0) { \
    if (argc != 1) return false; \
    command->type = value; return true; \
  }
  SIMPLE_ARGV_COMMAND("help", TYPX_COMMAND_HELP)
  SIMPLE_ARGV_COMMAND("status", TYPX_COMMAND_STATUS)
  SIMPLE_ARGV_COMMAND("release", TYPX_COMMAND_RELEASE)
  SIMPLE_ARGV_COMMAND("stop", TYPX_COMMAND_STOP)
  SIMPLE_ARGV_COMMAND("clear-bonds", TYPX_COMMAND_CLEAR_BONDS)
  SIMPLE_ARGV_COMMAND("run-standard", TYPX_COMMAND_RUN_STANDARD)
  SIMPLE_ARGV_COMMAND("run-personal", TYPX_COMMAND_RUN_PERSONAL)
#undef SIMPLE_ARGV_COMMAND
  if (strcmp(argv[0], "test-shift") == 0) {
    if (argc != 2 || !parse_uint_argument(argv[1], &hold_ms) ||
        !typx_hid_hold_supported(hold_ms)) {
      return false;
    }
    command->type = TYPX_COMMAND_TEST_SHIFT;
    command->hold_ms = hold_ms;
    return true;
  }
  if (strcmp(argv[0], "test") != 0 || argc != 3 ||
      !parse_uint_argument(argv[1], &hold_ms) ||
      !parse_uint_argument(argv[2], &character_count) ||
      !typx_hid_hold_supported(hold_ms) || character_count == 0u ||
      character_count > TYPX_CALIBRATION_MAX_CHARACTERS) {
    return false;
  }
  command->type = TYPX_COMMAND_TEST;
  command->hold_ms = hold_ms;
  command->character_count = character_count;
  return true;
}

const char *typx_hid_error_code(typx_hid_error_t error) {
  static const char *codes[] = {
      "OK", "DISCONNECTED", "BUSY", "INVALID_ARGUMENT",
      "SEND_FAILED", "WAIT_FAILED", "STOPPED"};
  size_t count = sizeof(codes) / sizeof(codes[0]);
  return (unsigned)error < count ? codes[error] : "UNKNOWN_HID_ERROR";
}
