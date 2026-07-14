#include "typx_ble_identity.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "keyboard_identity_defaults.h"

static uint32_t identity_hash(const uint8_t chip_identity[6]) {
  static const uint8_t salt[] = "TypxenIdentityV1";
  uint32_t hash = 2166136261u;
  size_t index;
  for (index = 0u; index < sizeof(salt) - 1u; ++index) {
    hash = (hash ^ salt[index]) * 16777619u;
  }
  for (index = 0u; index < 6u; ++index) {
    hash = (hash ^ chip_identity[index]) * 16777619u;
  }
  return hash;
}

void typx_ble_identity_generated_serial(
    const uint8_t chip_identity[6], char output[TYPX_IDENTITY_TEXT_BYTES]) {
  snprintf(output, TYPX_IDENTITY_TEXT_BYTES, "TYPX-%08lX",
      (unsigned long)identity_hash(chip_identity));
}

void typx_ble_identity_board_identifier(
    const uint8_t chip_identity[6], char output[TYPX_IDENTITY_BOARD_ID_BYTES]) {
  snprintf(output, TYPX_IDENTITY_BOARD_ID_BYTES, "BOARD-%08lX",
      (unsigned long)identity_hash(chip_identity));
}

static bool text_valid(
    const char *text, size_t capacity, bool empty_allowed) {
  size_t length;
  size_t index;
  if (text == NULL) return false;
  for (length = 0u; length < capacity && text[length] != '\0'; ++length) {}
  if (length >= capacity || (!empty_allowed && length == 0u)) return false;
  if (length > 0u &&
      (isspace((unsigned char)text[0]) ||
       isspace((unsigned char)text[length - 1u]))) return false;
  for (index = 0u; index < length; ++index) {
    unsigned char value = (unsigned char)text[index];
    if (value < 0x20u || value > 0x7eu) return false;
  }
  return true;
}

typx_ble_identity_validation_t typx_ble_identity_validate(
    const typx_ble_identity_config_t *config, bool serial_may_be_empty) {
  if (config == NULL || !text_valid(
          config->device_name, sizeof(config->device_name), false) ||
      !typx_ble_identity_advertising_fits(config->device_name))
    return TYPX_IDENTITY_INVALID_DEVICE_NAME;
  if (!text_valid(config->manufacturer_name,
          sizeof(config->manufacturer_name), false))
    return TYPX_IDENTITY_INVALID_MANUFACTURER;
  if (!text_valid(config->model_number, sizeof(config->model_number), false))
    return TYPX_IDENTITY_INVALID_MODEL;
  if (!text_valid(config->serial_number, sizeof(config->serial_number),
          serial_may_be_empty))
    return TYPX_IDENTITY_INVALID_SERIAL;
  if (!text_valid(config->hardware_revision,
          sizeof(config->hardware_revision), false))
    return TYPX_IDENTITY_INVALID_HARDWARE_REVISION;
  if (!text_valid(config->software_revision,
          sizeof(config->software_revision), false))
    return TYPX_IDENTITY_INVALID_SOFTWARE_REVISION;
  if (config->battery_minimum_percent > 99u ||
      config->battery_maximum_percent < 1u ||
      config->battery_maximum_percent > 100u ||
      config->battery_minimum_percent >= config->battery_maximum_percent)
    return TYPX_IDENTITY_INVALID_BATTERY_RANGE;
  if (config->battery_current_percent < config->battery_minimum_percent ||
      config->battery_current_percent > config->battery_maximum_percent)
    return TYPX_IDENTITY_INVALID_BATTERY_CURRENT;
  if (config->battery_drop_active_minutes < 1u ||
      config->battery_drop_active_minutes >
          TYPX_IDENTITY_BATTERY_MAX_INTERVAL_MINUTES)
    return TYPX_IDENTITY_INVALID_BATTERY_INTERVAL;
  return TYPX_IDENTITY_VALID;
}

static void defaults(
    typx_ble_identity_state_t *state,
    const uint8_t chip_identity[6], const char *firmware_revision) {
  memset(state, 0, sizeof(*state));
  strcpy(state->config.device_name, TYPX_IDENTITY_DEFAULT_DEVICE_NAME);
  strcpy(state->config.manufacturer_name, TYPX_IDENTITY_DEFAULT_MANUFACTURER);
  strcpy(state->config.model_number, TYPX_IDENTITY_DEFAULT_MODEL);
  typx_ble_identity_generated_serial(
      chip_identity, state->config.serial_number);
  strcpy(state->config.hardware_revision,
      TYPX_IDENTITY_DEFAULT_HARDWARE_REVISION);
  strcpy(state->config.software_revision,
      TYPX_IDENTITY_DEFAULT_SOFTWARE_REVISION);
  state->config.battery_simulation_enabled =
      TYPX_IDENTITY_DEFAULT_BATTERY_ENABLED;
  state->config.battery_minimum_percent =
      TYPX_IDENTITY_DEFAULT_BATTERY_MINIMUM;
  state->config.battery_maximum_percent =
      TYPX_IDENTITY_DEFAULT_BATTERY_MAXIMUM;
  state->config.battery_current_percent =
      TYPX_IDENTITY_DEFAULT_BATTERY_CURRENT;
  state->config.battery_drop_active_minutes =
      TYPX_IDENTITY_DEFAULT_BATTERY_INTERVAL_MINUTES;
  snprintf(state->firmware_revision, sizeof(state->firmware_revision), "%s",
      firmware_revision == NULL ? "unknown" : firmware_revision);
  typx_ble_identity_board_identifier(
      chip_identity, state->board_identifier);
}

#define APPLY_TEXT(flag, field) do { \
  if (overrides->flag) { \
    memcpy(candidate.config.field, overrides->values.field, \
        sizeof(candidate.config.field)); \
  } \
} while (0)

void typx_ble_identity_load(
    typx_ble_identity_state_t *state,
    const uint8_t chip_identity[6],
    const char *firmware_revision,
    const typx_ble_identity_overrides_t *overrides) {
  typx_ble_identity_state_t candidate;
  if (state == NULL || chip_identity == NULL) return;
  defaults(&candidate, chip_identity, firmware_revision);
  if (overrides != NULL) {
    APPLY_TEXT(device_name, device_name);
    APPLY_TEXT(manufacturer_name, manufacturer_name);
    APPLY_TEXT(model_number, model_number);
    APPLY_TEXT(serial_number, serial_number);
    APPLY_TEXT(hardware_revision, hardware_revision);
    APPLY_TEXT(software_revision, software_revision);
    if (overrides->battery_simulation_enabled)
      candidate.config.battery_simulation_enabled =
          overrides->values.battery_simulation_enabled;
    if (overrides->battery_minimum_percent)
      candidate.config.battery_minimum_percent =
          overrides->values.battery_minimum_percent;
    if (overrides->battery_maximum_percent)
      candidate.config.battery_maximum_percent =
          overrides->values.battery_maximum_percent;
    if (overrides->battery_current_percent)
      candidate.config.battery_current_percent =
          overrides->values.battery_current_percent;
    if (overrides->battery_drop_active_minutes)
      candidate.config.battery_drop_active_minutes =
          overrides->values.battery_drop_active_minutes;
    if (overrides->battery_active_ms)
      candidate.battery_active_ms = overrides->active_ms;
    candidate.source_device_name_nvs = overrides->device_name;
    candidate.source_manufacturer_nvs = overrides->manufacturer_name;
    candidate.source_model_nvs = overrides->model_number;
    candidate.source_serial_nvs = overrides->serial_number;
  }
  if (typx_ble_identity_validate(&candidate.config, false) !=
      TYPX_IDENTITY_VALID) {
    defaults(&candidate, chip_identity, firmware_revision);
  }
  *state = candidate;
}

#undef APPLY_TEXT

static bool identity_text_changed(
    const typx_ble_identity_config_t *old_config,
    const typx_ble_identity_config_t *new_config) {
  return strcmp(old_config->device_name, new_config->device_name) != 0 ||
      strcmp(old_config->manufacturer_name, new_config->manufacturer_name) != 0 ||
      strcmp(old_config->model_number, new_config->model_number) != 0 ||
      strcmp(old_config->serial_number, new_config->serial_number) != 0 ||
      strcmp(old_config->hardware_revision, new_config->hardware_revision) != 0 ||
      strcmp(old_config->software_revision, new_config->software_revision) != 0;
}

bool typx_ble_identity_prepare_update(
    const typx_ble_identity_state_t *state,
    const typx_ble_identity_config_t *requested,
    typx_ble_identity_config_t *effective,
    bool *restart_required,
    typx_ble_identity_validation_t *validation) {
  typx_ble_identity_validation_t result;
  if (state == NULL || requested == NULL || effective == NULL ||
      restart_required == NULL) return false;
  result = typx_ble_identity_validate(requested, true);
  if (validation != NULL) *validation = result;
  if (result != TYPX_IDENTITY_VALID) return false;
  *effective = *requested;
  if (effective->serial_number[0] == '\0') {
    snprintf(effective->serial_number, sizeof(effective->serial_number),
        "TYPX-%s", state->board_identifier + 6);
  }
  *restart_required = state->restart_required ||
      identity_text_changed(&state->config, effective);
  return true;
}

bool typx_ble_identity_record_successful_report(
    typx_ble_identity_state_t *state,
    uint64_t now_ms,
    bool execution_active) {
  uint64_t interval_ms;
  uint64_t delta;
  bool changed = false;
  if (state == NULL || !execution_active ||
      !state->config.battery_simulation_enabled) {
    if (state != NULL) state->activity_open = false;
    return false;
  }
  if (!state->activity_open) {
    state->last_success_ms = now_ms;
    state->activity_open = true;
    return false;
  }
  delta = now_ms - state->last_success_ms;
  state->last_success_ms = now_ms;
  state->battery_active_ms += delta;
  interval_ms =
      (uint64_t)state->config.battery_drop_active_minutes * 60000u;
  while (state->battery_active_ms >= interval_ms) {
    state->battery_active_ms -= interval_ms;
    state->config.battery_current_percent =
        state->config.battery_current_percent <=
                state->config.battery_minimum_percent
            ? state->config.battery_maximum_percent
            : (uint8_t)(state->config.battery_current_percent - 1u);
    changed = true;
  }
  return changed;
}

void typx_ble_identity_end_activity(typx_ble_identity_state_t *state) {
  if (state != NULL) state->activity_open = false;
}

bool typx_ble_identity_reset_battery(typx_ble_identity_state_t *state) {
  bool changed;
  if (state == NULL) return false;
  changed = state->config.battery_current_percent !=
      state->config.battery_maximum_percent;
  state->config.battery_current_percent =
      state->config.battery_maximum_percent;
  state->battery_active_ms = 0u;
  state->activity_open = false;
  return changed;
}

size_t typx_ble_identity_scan_response_bytes(const char *device_name) {
  if (device_name == NULL) return 0u;
  return strlen(device_name) + TYPX_BLE_LOCAL_NAME_OVERHEAD_BYTES;
}

bool typx_ble_identity_advertising_fits(const char *device_name) {
  return TYPX_BLE_ADVERTISING_PACKET_BYTES <=
          TYPX_BLE_LEGACY_PACKET_MAX_BYTES &&
      typx_ble_identity_scan_response_bytes(device_name) <=
          TYPX_BLE_LEGACY_PACKET_MAX_BYTES;
}

bool typx_ble_identity_restart_clears_bonds(
    const typx_ble_identity_state_t *state) {
  return state != NULL && state->restart_required;
}

const char *typx_ble_identity_validation_code(
    typx_ble_identity_validation_t validation) {
  static const char *codes[] = {
    "OK", "INVALID_DEVICE_NAME", "INVALID_MANUFACTURER", "INVALID_MODEL",
    "INVALID_SERIAL", "INVALID_HARDWARE_REVISION",
    "INVALID_SOFTWARE_REVISION", "INVALID_BATTERY_RANGE",
    "INVALID_BATTERY_CURRENT", "INVALID_BATTERY_INTERVAL"};
  size_t count = sizeof(codes) / sizeof(codes[0]);
  return (unsigned)validation < count ? codes[validation] : "INVALID_CONFIG";
}
