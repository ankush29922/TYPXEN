#ifndef TYPX_EXECUTION_SAFETY_H
#define TYPX_EXECUTION_SAFETY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TYPX_EXECUTION_RELEASE_MAX_ATTEMPTS 6u
#define TYPX_EXECUTION_RELEASE_INITIAL_DELAY_MS 250u
#define TYPX_EXECUTION_RELEASE_RETRY_DELAY_MS 500u
#define TYPX_DEDICATED_COUNTDOWN_SECONDS 8u
#define TYPX_DEDICATED_COUNTDOWN_MS \
  (TYPX_DEDICATED_COUNTDOWN_SECONDS * 1000u)
#define TYPX_BLE_SUPERVISION_TIMEOUT_UNITS 300u
#define TYPX_BLE_SUPERVISION_TIMEOUT_MS \
  (TYPX_BLE_SUPERVISION_TIMEOUT_UNITS * 10u)

typedef bool (*typx_execution_marker_read_fn)(
    void *context, bool *active);
typedef bool (*typx_execution_marker_write_fn)(
    void *context, bool active);
typedef bool (*typx_execution_keyboard_release_fn)(
    void *context, const uint8_t report[8]);
typedef bool (*typx_execution_consumer_release_fn)(
    void *context, const uint8_t report[2]);

typedef enum {
  TYPX_HID_SAFETY_DISCONNECTED = 0,
  TYPX_HID_SAFETY_WAITING_FOR_AUTH,
  TYPX_HID_SAFETY_WAITING_FOR_REPORT_PATH,
  TYPX_HID_SAFETY_RELEASING,
  TYPX_HID_SAFETY_CONFIRMED,
  TYPX_HID_SAFETY_FAILED_RETRYABLE
} typx_hid_safety_state_t;

typedef enum {
  TYPX_HID_SAFETY_ERROR_NONE = 0,
  TYPX_HID_SAFETY_ERROR_HID_NOT_CONNECTED,
  TYPX_HID_SAFETY_ERROR_AUTH_PENDING,
  TYPX_HID_SAFETY_ERROR_AUTH_FAILED,
  TYPX_HID_SAFETY_ERROR_REPORT_PATH_PENDING,
  TYPX_HID_SAFETY_ERROR_KEYBOARD_REPORT_FAILED,
  TYPX_HID_SAFETY_ERROR_CONSUMER_REPORT_FAILED,
  TYPX_HID_SAFETY_ERROR_REPORTS_FAILED
} typx_hid_safety_error_t;

typedef struct {
  void *context;
  typx_execution_marker_read_fn read_active;
  typx_execution_marker_write_fn write_active;
} typx_execution_marker_io_t;

typedef struct {
  typx_execution_marker_io_t marker;
  bool interrupted_reset;
  bool connected;
  bool authenticated;
  typx_hid_safety_state_t hid_state;
  typx_hid_safety_error_t hid_error;
  unsigned release_attempts;
  uint32_t connection_generation;
} typx_execution_safety_t;

typedef struct {
  bool connected;
  bool authenticated;
  bool ready;
  typx_hid_safety_state_t state;
  typx_hid_safety_error_t error;
  unsigned attempts;
  uint32_t generation;
} typx_hid_safety_snapshot_t;

bool typx_execution_safety_init(
    typx_execution_safety_t *safety,
    const typx_execution_marker_io_t *marker);
bool typx_execution_safety_begin(typx_execution_safety_t *safety);
bool typx_execution_safety_finish(typx_execution_safety_t *safety);
bool typx_execution_safety_interrupted(
    const typx_execution_safety_t *safety);
uint32_t typx_execution_safety_connected(
    typx_execution_safety_t *safety);
void typx_execution_safety_disconnected(
    typx_execution_safety_t *safety);
void typx_execution_safety_authenticated(
    typx_execution_safety_t *safety, bool success);
bool typx_execution_safety_begin_release(
    typx_execution_safety_t *safety, uint32_t generation);
bool typx_execution_safety_complete_release(
    typx_execution_safety_t *safety,
    uint32_t generation,
    bool keyboard_released,
    bool consumer_released);
bool typx_execution_safety_request_retry(
    typx_execution_safety_t *safety);
void typx_execution_safety_snapshot(
    const typx_execution_safety_t *safety,
    typx_hid_safety_snapshot_t *snapshot);
const char *typx_hid_safety_state_code(typx_hid_safety_state_t state);
const char *typx_hid_safety_error_code(typx_hid_safety_error_t error);

#endif
