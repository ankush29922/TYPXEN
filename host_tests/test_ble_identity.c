#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "keyboard_identity_defaults.h"
#include "typx_ble_identity.h"

static unsigned assertions;
static unsigned failures;

#define CHECK(condition) do { \
  ++assertions; \
  if (!(condition)) { \
    ++failures; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
  } \
} while (0)

static const uint8_t CHIP_ID[6] = {0x24, 0x6f, 0x28, 0x12, 0x34, 0x56};

static typx_ble_identity_state_t default_state(void) {
  typx_ble_identity_state_t state;
  typx_ble_identity_load(&state, CHIP_ID, "1.2.3-test", NULL);
  return state;
}

static void test_defaults_and_stable_identity(void) {
  typx_ble_identity_state_t first = default_state();
  typx_ble_identity_state_t second = default_state();
  CHECK(strcmp(first.config.device_name, "Typxen Keyboard") == 0);
  CHECK(strcmp(first.config.manufacturer_name, "Typxen") == 0);
  CHECK(strcmp(first.config.model_number, "Typxen ESP32-CAM") == 0);
  CHECK(strcmp(first.config.serial_number, second.config.serial_number) == 0);
  CHECK(strncmp(first.config.serial_number, "TYPX-", 5u) == 0);
  CHECK(strcmp(first.firmware_revision, "1.2.3-test") == 0);
  CHECK(first.config.battery_minimum_percent == 50u);
  CHECK(first.config.battery_maximum_percent == 90u);
  CHECK(first.config.battery_current_percent == 90u);
  CHECK(first.config.battery_drop_active_minutes == 20u);
}

static void test_overrides_and_invalid_fallback(void) {
  typx_ble_identity_overrides_t overrides = {0};
  typx_ble_identity_state_t state;
  overrides.device_name = true;
  overrides.model_number = true;
  strcpy(overrides.values.device_name, "Desk Keyboard");
  strcpy(overrides.values.model_number, "Desk Board");
  strcpy(overrides.values.manufacturer_name, "Typxen");
  strcpy(overrides.values.serial_number, "TYPX-SAVED");
  strcpy(overrides.values.hardware_revision, "1.0");
  strcpy(overrides.values.software_revision, "1.0.0");
  overrides.values.battery_minimum_percent = 50u;
  overrides.values.battery_maximum_percent = 90u;
  overrides.values.battery_current_percent = 90u;
  overrides.values.battery_drop_active_minutes = 20u;
  typx_ble_identity_load(&state, CHIP_ID, "build", &overrides);
  CHECK(strcmp(state.config.device_name, "Desk Keyboard") == 0);
  CHECK(strcmp(state.config.model_number, "Desk Board") == 0);
  CHECK(state.source_device_name_nvs);
  CHECK(state.source_model_nvs);

  memset(overrides.values.device_name, 'X',
      sizeof(overrides.values.device_name));
  typx_ble_identity_load(&state, CHIP_ID, "build", &overrides);
  CHECK(strcmp(state.config.device_name,
      TYPX_IDENTITY_DEFAULT_DEVICE_NAME) == 0);
  CHECK(!state.source_device_name_nvs);
}

static void test_validation_and_atomic_prepare(void) {
  typx_ble_identity_state_t state = default_state();
  typx_ble_identity_config_t requested = state.config;
  typx_ble_identity_config_t effective;
  typx_ble_identity_validation_t validation;
  bool restart = false;
  strcpy(requested.device_name, " New name");
  CHECK(!typx_ble_identity_prepare_update(
      &state, &requested, &effective, &restart, &validation));
  CHECK(validation == TYPX_IDENTITY_INVALID_DEVICE_NAME);
  CHECK(strcmp(state.config.device_name, "Typxen Keyboard") == 0);

  requested = state.config;
  requested.serial_number[0] = '\0';
  CHECK(typx_ble_identity_prepare_update(
      &state, &requested, &effective, &restart, &validation));
  CHECK(strncmp(effective.serial_number, "TYPX-", 5u) == 0);
  CHECK(!restart);
  CHECK(strcmp(state.firmware_revision, "1.2.3-test") == 0);

  requested = state.config;
  strcpy(requested.device_name, "New Keyboard");
  CHECK(typx_ble_identity_prepare_update(
      &state, &requested, &effective, &restart, &validation));
  CHECK(restart);

  requested = state.config;
  requested.battery_minimum_percent = 90u;
  requested.battery_maximum_percent = 90u;
  CHECK(!typx_ble_identity_prepare_update(
      &state, &requested, &effective, &restart, &validation));
  CHECK(validation == TYPX_IDENTITY_INVALID_BATTERY_RANGE);
}

static void test_advertising_packet_limits_and_bond_policy(void) {
  typx_ble_identity_state_t state = default_state();
  typx_ble_identity_config_t requested = state.config;
  typx_ble_identity_config_t effective;
  typx_ble_identity_validation_t validation;
  bool restart = false;
  char maximum_name[TYPX_IDENTITY_DEVICE_NAME_BYTES];

  memset(maximum_name, 'K', sizeof(maximum_name) - 1u);
  maximum_name[sizeof(maximum_name) - 1u] = '\0';
  CHECK(strlen(maximum_name) == 29u);
  CHECK(TYPX_BLE_ADVERTISING_PACKET_BYTES == 31u);
  CHECK(typx_ble_identity_scan_response_bytes(maximum_name) == 31u);
  CHECK(typx_ble_identity_advertising_fits(maximum_name));
  strcpy(requested.device_name, maximum_name);
  CHECK(typx_ble_identity_prepare_update(
      &state, &requested, &effective, &restart, &validation));
  CHECK(restart);
  CHECK(!typx_ble_identity_restart_clears_bonds(&state));
  state.restart_required = restart;
  CHECK(typx_ble_identity_restart_clears_bonds(&state));

  state = default_state();
  requested = state.config;
  requested.battery_current_percent = 80u;
  CHECK(typx_ble_identity_prepare_update(
      &state, &requested, &effective, &restart, &validation));
  CHECK(!restart);
  CHECK(!typx_ble_identity_restart_clears_bonds(&state));
  CHECK(TYPX_IDENTITY_NEUTRAL_PNP_VENDOR_ID == 0xffffu);
}

static void test_battery_activity(void) {
  typx_ble_identity_state_t state = default_state();
  state.config.battery_drop_active_minutes = 1u;
  CHECK(!typx_ble_identity_record_successful_report(
      &state, 1000u, true));
  CHECK(!typx_ble_identity_record_successful_report(
      &state, 31000u, true));
  CHECK(state.config.battery_current_percent == 90u);
  CHECK(typx_ble_identity_record_successful_report(
      &state, 61000u, true));
  CHECK(state.config.battery_current_percent == 89u);
  CHECK(!typx_ble_identity_record_successful_report(
      &state, 5000000u, false));
  CHECK(state.config.battery_current_percent == 89u);
  typx_ble_identity_end_activity(&state);
  CHECK(!typx_ble_identity_record_successful_report(
      &state, 999999u, false));
  CHECK(!typx_ble_identity_record_successful_report(
      &state, 1000000u, true));
  CHECK(state.config.battery_current_percent == 89u);

  state.config.battery_current_percent = state.config.battery_minimum_percent;
  state.battery_active_ms = 60000u;
  state.activity_open = true;
  state.last_success_ms = 2000000u;
  CHECK(typx_ble_identity_record_successful_report(
      &state, 2000000u, true));
  CHECK(state.config.battery_current_percent == 90u);
  CHECK(typx_ble_identity_reset_battery(&state) == false);
  CHECK(state.battery_active_ms == 0u);
}

int main(void) {
  test_defaults_and_stable_identity();
  test_overrides_and_invalid_fallback();
  test_validation_and_atomic_prepare();
  test_advertising_packet_limits_and_bond_policy();
  test_battery_activity();
  printf("BLE identity tests: %u assertions, %u failures\n",
      assertions, failures);
  return failures == 0u ? 0 : 1;
}
