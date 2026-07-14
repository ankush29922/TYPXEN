#ifndef TYPX_BLE_HID_H
#define TYPX_BLE_HID_H

#include "esp_err.h"
#include "typx_execution_safety.h"
#include "typx_executor.h"
#include "typx_hid_runtime.h"

typedef struct {
  void *context;
  void (*countdown_complete)(void *context);
  void (*progress)(
      void *context,
      uint32_t source_index,
      uint32_t completed_records,
      uint32_t total_records);
} typx_ble_hid_execution_observer_t;

esp_err_t typx_ble_hid_init(void);
bool typx_ble_hid_connected(void);
bool typx_ble_hid_safety_ready(void);
void typx_ble_hid_safety_status(typx_hid_safety_snapshot_t *status);
bool typx_ble_hid_retry_safety_release(void);
bool typx_ble_hid_interrupted_reset(void);
bool typx_ble_hid_mark_execution_active(void);
bool typx_ble_hid_mark_execution_terminal(void);
void typx_ble_hid_request_stop(void);
bool typx_ble_hid_clear_bonds(void);
bool typx_ble_hid_clear_identity_bonds(void);
const char *typx_ble_hid_device_name(void);
bool typx_ble_hid_prepare_reboot(void);
void typx_ble_hid_end_activity(void);
typx_hid_runtime_t *typx_ble_hid_runtime(void);
typx_executor_error_t typx_ble_hid_run_schedule(
    const typx_verified_schedule_v1_t *schedule);
typx_executor_error_t typx_ble_hid_run_schedule_countdown(
    const typx_verified_schedule_v1_t *schedule,
    unsigned countdown_seconds);
typx_executor_error_t typx_ble_hid_run_schedule_countdown_observed(
    const typx_verified_schedule_v1_t *schedule,
    unsigned countdown_seconds,
    const typx_ble_hid_execution_observer_t *observer);

#endif
