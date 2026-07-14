#ifndef TYPX_BLE_IDENTITY_H
#define TYPX_BLE_IDENTITY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TYPX_IDENTITY_DEVICE_NAME_BYTES 30u
#define TYPX_IDENTITY_TEXT_BYTES 33u
#define TYPX_IDENTITY_REVISION_BYTES 17u
#define TYPX_IDENTITY_FIRMWARE_BYTES 33u
#define TYPX_IDENTITY_BOARD_ID_BYTES 15u
#define TYPX_IDENTITY_BATTERY_MAX_INTERVAL_MINUTES 1440u
#define TYPX_BLE_LEGACY_PACKET_MAX_BYTES 31u
#define TYPX_BLE_ADVERTISING_PACKET_BYTES 31u
#define TYPX_BLE_LOCAL_NAME_OVERHEAD_BYTES 2u

typedef struct {
  char device_name[TYPX_IDENTITY_DEVICE_NAME_BYTES];
  char manufacturer_name[TYPX_IDENTITY_TEXT_BYTES];
  char model_number[TYPX_IDENTITY_TEXT_BYTES];
  char serial_number[TYPX_IDENTITY_TEXT_BYTES];
  char hardware_revision[TYPX_IDENTITY_REVISION_BYTES];
  char software_revision[TYPX_IDENTITY_REVISION_BYTES];
  bool battery_simulation_enabled;
  uint8_t battery_minimum_percent;
  uint8_t battery_maximum_percent;
  uint8_t battery_current_percent;
  uint16_t battery_drop_active_minutes;
} typx_ble_identity_config_t;

typedef struct {
  bool device_name;
  bool manufacturer_name;
  bool model_number;
  bool serial_number;
  bool hardware_revision;
  bool software_revision;
  bool battery_simulation_enabled;
  bool battery_minimum_percent;
  bool battery_maximum_percent;
  bool battery_current_percent;
  bool battery_drop_active_minutes;
  bool battery_active_ms;
  typx_ble_identity_config_t values;
  uint64_t active_ms;
} typx_ble_identity_overrides_t;

typedef enum {
  TYPX_IDENTITY_VALID = 0,
  TYPX_IDENTITY_INVALID_DEVICE_NAME,
  TYPX_IDENTITY_INVALID_MANUFACTURER,
  TYPX_IDENTITY_INVALID_MODEL,
  TYPX_IDENTITY_INVALID_SERIAL,
  TYPX_IDENTITY_INVALID_HARDWARE_REVISION,
  TYPX_IDENTITY_INVALID_SOFTWARE_REVISION,
  TYPX_IDENTITY_INVALID_BATTERY_RANGE,
  TYPX_IDENTITY_INVALID_BATTERY_CURRENT,
  TYPX_IDENTITY_INVALID_BATTERY_INTERVAL
} typx_ble_identity_validation_t;

typedef struct {
  typx_ble_identity_config_t config;
  char firmware_revision[TYPX_IDENTITY_FIRMWARE_BYTES];
  char board_identifier[TYPX_IDENTITY_BOARD_ID_BYTES];
  bool source_device_name_nvs;
  bool source_manufacturer_nvs;
  bool source_model_nvs;
  bool source_serial_nvs;
  bool restart_required;
  uint64_t battery_active_ms;
  uint64_t last_success_ms;
  bool activity_open;
} typx_ble_identity_state_t;

void typx_ble_identity_generated_serial(
    const uint8_t chip_identity[6], char output[TYPX_IDENTITY_TEXT_BYTES]);
void typx_ble_identity_board_identifier(
    const uint8_t chip_identity[6], char output[TYPX_IDENTITY_BOARD_ID_BYTES]);
void typx_ble_identity_load(
    typx_ble_identity_state_t *state,
    const uint8_t chip_identity[6],
    const char *firmware_revision,
    const typx_ble_identity_overrides_t *overrides);
typx_ble_identity_validation_t typx_ble_identity_validate(
    const typx_ble_identity_config_t *config, bool serial_may_be_empty);
bool typx_ble_identity_prepare_update(
    const typx_ble_identity_state_t *state,
    const typx_ble_identity_config_t *requested,
    typx_ble_identity_config_t *effective,
    bool *restart_required,
    typx_ble_identity_validation_t *validation);
bool typx_ble_identity_record_successful_report(
    typx_ble_identity_state_t *state,
    uint64_t now_ms,
    bool execution_active);
void typx_ble_identity_end_activity(typx_ble_identity_state_t *state);
bool typx_ble_identity_reset_battery(typx_ble_identity_state_t *state);
size_t typx_ble_identity_scan_response_bytes(const char *device_name);
bool typx_ble_identity_advertising_fits(const char *device_name);
bool typx_ble_identity_restart_clears_bonds(
    const typx_ble_identity_state_t *state);
const char *typx_ble_identity_validation_code(
    typx_ble_identity_validation_t validation);

#endif
