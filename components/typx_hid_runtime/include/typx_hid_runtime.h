#ifndef TYPX_HID_RUNTIME_H
#define TYPX_HID_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TYPX_CALIBRATION_MAX_CHARACTERS 1080u
#define TYPX_CALIBRATION_WAIT_AFTER_RELEASE_MS 60u
#define TYPX_HID_RELEASE_MAX_ATTEMPTS 3u

typedef bool (*typx_hid_send_report_fn)(
    void *context, const uint8_t report[8]);
typedef bool (*typx_hid_wait_ms_fn)(void *context, uint32_t delay_ms);
typedef void (*typx_hid_countdown_fn)(void *context, unsigned seconds_left);

typedef struct {
  void *context;
  typx_hid_send_report_fn send_report;
  typx_hid_wait_ms_fn wait_ms;
  typx_hid_countdown_fn countdown;
} typx_hid_runtime_io_t;

typedef struct {
  typx_hid_runtime_io_t io;
  volatile bool connected;
  volatile bool running;
  volatile bool stop_requested;
} typx_hid_runtime_t;

typedef enum {
  TYPX_HID_OK = 0,
  TYPX_HID_DISCONNECTED,
  TYPX_HID_BUSY,
  TYPX_HID_INVALID_ARGUMENT,
  TYPX_HID_SEND_FAILED,
  TYPX_HID_WAIT_FAILED,
  TYPX_HID_STOPPED
} typx_hid_error_t;

typedef enum {
  TYPX_COMMAND_INVALID = 0,
  TYPX_COMMAND_HELP,
  TYPX_COMMAND_STATUS,
  TYPX_COMMAND_RELEASE,
  TYPX_COMMAND_STOP,
  TYPX_COMMAND_CLEAR_BONDS,
  TYPX_COMMAND_TEST,
  TYPX_COMMAND_TEST_SHIFT,
  TYPX_COMMAND_RUN_STANDARD,
  TYPX_COMMAND_RUN_PERSONAL
} typx_command_type_t;

typedef struct {
  typx_command_type_t type;
  uint32_t hold_ms;
  uint32_t character_count;
} typx_console_command_t;

void typx_hid_runtime_init(
    typx_hid_runtime_t *runtime, const typx_hid_runtime_io_t *io);
void typx_hid_runtime_set_connected(
    typx_hid_runtime_t *runtime, bool connected);
void typx_hid_runtime_request_stop(typx_hid_runtime_t *runtime);
bool typx_hid_runtime_is_idle(const typx_hid_runtime_t *runtime);
bool typx_hid_runtime_can_clear_bonds(const typx_hid_runtime_t *runtime);

typx_hid_error_t typx_hid_key_down(
    typx_hid_runtime_t *runtime, uint8_t modifier, uint8_t keycode);
typx_hid_error_t typx_hid_release_all(typx_hid_runtime_t *runtime);
typx_hid_error_t typx_hid_run_test(
    typx_hid_runtime_t *runtime, uint32_t hold_ms, uint32_t character_count);
typx_hid_error_t typx_hid_run_shift_test(
    typx_hid_runtime_t *runtime, uint32_t hold_ms);

bool typx_hid_hold_supported(uint32_t hold_ms);
bool typx_console_parse(
    const char *line, typx_console_command_t *command);
bool typx_console_parse_argv(
    int argc,
    const char *const argv[],
    typx_console_command_t *command);
const char *typx_hid_error_code(typx_hid_error_t error);

#endif
