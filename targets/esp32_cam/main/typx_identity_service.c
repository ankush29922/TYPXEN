#include "typx_identity_service.h"

#include <string.h>

#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

#define IDENTITY_NAMESPACE "ble_identity"

static const char *TAG = "typx_identity";

static typx_ble_identity_state_t s_state;
static SemaphoreHandle_t s_mutex;
static esp_hidd_dev_t *s_hid_device;
static uint8_t s_chip_identity[6];

static esp_err_t get_optional_string(
    nvs_handle_t handle, const char *key,
    char *value, size_t capacity, bool *present) {
  size_t length = capacity;
  esp_err_t error = nvs_get_str(handle, key, value, &length);
  if (error == ESP_ERR_NVS_NOT_FOUND) {
    *present = false;
    return ESP_OK;
  }
  *present = error == ESP_OK;
  return error;
}

static esp_err_t get_optional_u8(
    nvs_handle_t handle, const char *key, uint8_t *value, bool *present) {
  esp_err_t error = nvs_get_u8(handle, key, value);
  if (error == ESP_ERR_NVS_NOT_FOUND) {
    *present = false;
    return ESP_OK;
  }
  *present = error == ESP_OK;
  return error;
}

static esp_err_t get_optional_u16(
    nvs_handle_t handle, const char *key, uint16_t *value, bool *present) {
  esp_err_t error = nvs_get_u16(handle, key, value);
  if (error == ESP_ERR_NVS_NOT_FOUND) {
    *present = false;
    return ESP_OK;
  }
  *present = error == ESP_OK;
  return error;
}

static esp_err_t load_overrides(typx_ble_identity_overrides_t *overrides) {
  nvs_handle_t handle;
  uint8_t value;
  esp_err_t error;
  memset(overrides, 0, sizeof(*overrides));
  ESP_RETURN_ON_ERROR(
      nvs_open(IDENTITY_NAMESPACE, NVS_READONLY, &handle),
      "typx_identity", "open identity NVS");
#define GET_STRING(key, flag, field) do { \
  error = get_optional_string(handle, key, overrides->values.field, \
      sizeof(overrides->values.field), &overrides->flag); \
  if (error != ESP_OK) goto done; \
} while (0)
  GET_STRING("name", device_name, device_name);
  GET_STRING("manufacturer", manufacturer_name, manufacturer_name);
  GET_STRING("model", model_number, model_number);
  GET_STRING("serial", serial_number, serial_number);
  GET_STRING("hw_rev", hardware_revision, hardware_revision);
  GET_STRING("sw_rev", software_revision, software_revision);
#undef GET_STRING
#define GET_U8(key, flag, field) do { \
  error = get_optional_u8(handle, key, &value, &overrides->flag); \
  if (error != ESP_OK) goto done; \
  if (overrides->flag) overrides->values.field = value; \
} while (0)
  GET_U8("batt_enabled", battery_simulation_enabled,
      battery_simulation_enabled);
  GET_U8("batt_min", battery_minimum_percent, battery_minimum_percent);
  GET_U8("batt_max", battery_maximum_percent, battery_maximum_percent);
  GET_U8("batt_current", battery_current_percent, battery_current_percent);
#undef GET_U8
  error = get_optional_u16(
      handle, "batt_interval",
      &overrides->values.battery_drop_active_minutes,
      &overrides->battery_drop_active_minutes);
  if (error != ESP_OK) goto done;
  error = nvs_get_u64(handle, "batt_active_ms", &overrides->active_ms);
  if (error == ESP_ERR_NVS_NOT_FOUND) {
    overrides->battery_active_ms = false;
    error = ESP_OK;
  } else {
    overrides->battery_active_ms = error == ESP_OK;
  }
done:
  nvs_close(handle);
  return error;
}

static esp_err_t persist_activity_locked(void) {
  nvs_handle_t handle;
  esp_err_t error;
  ESP_RETURN_ON_ERROR(
      nvs_open(IDENTITY_NAMESPACE, NVS_READWRITE, &handle),
      "typx_identity", "open battery NVS");
  error = nvs_set_u8(
      handle, "batt_current", s_state.config.battery_current_percent);
  if (error == ESP_OK)
    error = nvs_set_u64(handle, "batt_active_ms", s_state.battery_active_ms);
  if (error == ESP_OK) error = nvs_commit(handle);
  nvs_close(handle);
  return error;
}

esp_err_t typx_identity_service_init(void) {
  typx_ble_identity_overrides_t overrides;
  const esp_app_desc_t *description = esp_app_get_description();
  esp_err_t error;
  error = esp_read_mac(s_chip_identity, ESP_MAC_BT);
  if (error != ESP_OK) return error;
  s_mutex = xSemaphoreCreateMutex();
  if (s_mutex == NULL) return ESP_ERR_NO_MEM;
  error = load_overrides(&overrides);
  if (error == ESP_ERR_NVS_NOT_FOUND) {
    memset(&overrides, 0, sizeof(overrides));
    error = ESP_OK;
  }
  if (error != ESP_OK) return error;
  typx_ble_identity_load(
      &s_state, s_chip_identity,
      description == NULL ? "unknown" : description->version,
      &overrides);
  ESP_LOGI(
      TAG, "Identity ready; firmware=%s batterySimulation=%s level=%u%%",
      s_state.firmware_revision,
      s_state.config.battery_simulation_enabled ? "enabled" : "disabled",
      s_state.config.battery_current_percent);
  {
    nvs_handle_t handle = 0u;
    if (nvs_open(IDENTITY_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
      esp_err_t clear_error = nvs_erase_key(handle, "restart_req");
      if (clear_error == ESP_OK) (void)nvs_commit(handle);
      nvs_close(handle);
    }
  }
  return ESP_OK;
}

void typx_identity_service_snapshot(typx_ble_identity_state_t *snapshot) {
  if (snapshot == NULL || s_mutex == NULL) return;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  *snapshot = s_state;
  xSemaphoreGive(s_mutex);
}

static esp_err_t save_config_locked(
    const typx_ble_identity_config_t *requested,
    const typx_ble_identity_config_t *effective,
    bool restart_required) {
  nvs_handle_t handle = 0u;
  esp_err_t error;
  ESP_RETURN_ON_ERROR(
      nvs_open(IDENTITY_NAMESPACE, NVS_READWRITE, &handle),
      "typx_identity", "open identity NVS");
#define SET_STRING(key, value) do { \
  if (error == ESP_OK) error = nvs_set_str(handle, key, value); \
} while (0)
  error = ESP_OK;
  SET_STRING("name", effective->device_name);
  SET_STRING("manufacturer", effective->manufacturer_name);
  SET_STRING("model", effective->model_number);
  if (error == ESP_OK) {
    error = requested->serial_number[0] == '\0'
        ? nvs_erase_key(handle, "serial")
        : nvs_set_str(handle, "serial", effective->serial_number);
    if (error == ESP_ERR_NVS_NOT_FOUND) error = ESP_OK;
  }
  SET_STRING("hw_rev", effective->hardware_revision);
  SET_STRING("sw_rev", effective->software_revision);
#undef SET_STRING
  if (error == ESP_OK) error = nvs_set_u8(
      handle, "batt_enabled", effective->battery_simulation_enabled ? 1u : 0u);
  if (error == ESP_OK) error = nvs_set_u8(
      handle, "batt_min", effective->battery_minimum_percent);
  if (error == ESP_OK) error = nvs_set_u8(
      handle, "batt_max", effective->battery_maximum_percent);
  if (error == ESP_OK) error = nvs_set_u8(
      handle, "batt_current", effective->battery_current_percent);
  if (error == ESP_OK) error = nvs_set_u16(
      handle, "batt_interval", effective->battery_drop_active_minutes);
  if (error == ESP_OK) error = nvs_set_u64(handle, "batt_active_ms", 0u);
  if (error == ESP_OK) error = nvs_set_u8(
      handle, "restart_req", restart_required ? 1u : 0u);
  if (error == ESP_OK) error = nvs_commit(handle);
  nvs_close(handle);
  return error;
}

esp_err_t typx_identity_service_save(
    const typx_ble_identity_config_t *requested,
    typx_ble_identity_validation_t *validation) {
  typx_ble_identity_config_t effective;
  bool restart_required;
  uint8_t previous_battery;
  esp_err_t error;
  if (requested == NULL || s_mutex == NULL) return ESP_ERR_INVALID_ARG;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  previous_battery = s_state.config.battery_current_percent;
  if (!typx_ble_identity_prepare_update(
          &s_state, requested, &effective,
          &restart_required, validation)) {
    xSemaphoreGive(s_mutex);
    return ESP_ERR_INVALID_ARG;
  }
  error = save_config_locked(requested, &effective, restart_required);
  if (error == ESP_OK) {
    s_state.config = effective;
    s_state.restart_required = restart_required;
    s_state.battery_active_ms = 0u;
    s_state.activity_open = false;
    s_state.source_device_name_nvs = true;
    s_state.source_manufacturer_nvs = true;
    s_state.source_model_nvs = true;
    s_state.source_serial_nvs = requested->serial_number[0] != '\0';
    if (s_hid_device != NULL &&
        previous_battery != s_state.config.battery_current_percent) {
      (void)esp_hidd_dev_battery_set(
          s_hid_device, s_state.config.battery_current_percent);
    }
  }
  xSemaphoreGive(s_mutex);
  return error;
}

static esp_err_t erase_identity_keys(nvs_handle_t handle) {
  static const char *keys[] = {
    "name", "manufacturer", "model", "serial", "hw_rev", "sw_rev",
    "batt_enabled", "batt_min", "batt_max", "batt_current",
    "batt_interval", "batt_active_ms", "restart_req"};
  size_t index;
  for (index = 0u; index < sizeof(keys) / sizeof(keys[0]); ++index) {
    esp_err_t error = nvs_erase_key(handle, keys[index]);
    if (error != ESP_OK && error != ESP_ERR_NVS_NOT_FOUND) return error;
  }
  return ESP_OK;
}

esp_err_t typx_identity_service_reset(void) {
  typx_ble_identity_state_t defaults;
  nvs_handle_t handle = 0u;
  bool restart_required;
  uint8_t previous_battery;
  esp_err_t error;
  if (s_mutex == NULL) return ESP_ERR_INVALID_STATE;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  previous_battery = s_state.config.battery_current_percent;
  typx_ble_identity_load(
      &defaults, s_chip_identity, s_state.firmware_revision, NULL);
  restart_required = strcmp(
      s_state.config.device_name, defaults.config.device_name) != 0 ||
      strcmp(s_state.config.manufacturer_name,
          defaults.config.manufacturer_name) != 0 ||
      strcmp(s_state.config.model_number, defaults.config.model_number) != 0 ||
      strcmp(s_state.config.serial_number, defaults.config.serial_number) != 0 ||
      strcmp(s_state.config.hardware_revision,
          defaults.config.hardware_revision) != 0 ||
      strcmp(s_state.config.software_revision,
          defaults.config.software_revision) != 0;
  error = nvs_open(IDENTITY_NAMESPACE, NVS_READWRITE, &handle);
  if (error == ESP_OK) error = erase_identity_keys(handle);
  if (error == ESP_OK && restart_required)
    error = nvs_set_u8(handle, "restart_req", 1u);
  if (error == ESP_OK) error = nvs_commit(handle);
  if (handle != 0u) nvs_close(handle);
  if (error == ESP_OK) {
    s_state = defaults;
    s_state.restart_required = restart_required;
    if (s_hid_device != NULL &&
        previous_battery != s_state.config.battery_current_percent)
      (void)esp_hidd_dev_battery_set(
          s_hid_device, s_state.config.battery_current_percent);
  }
  xSemaphoreGive(s_mutex);
  return error;
}

esp_err_t typx_identity_service_battery_reset(void) {
  bool changed;
  esp_err_t error;
  if (s_mutex == NULL) return ESP_ERR_INVALID_STATE;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  changed = typx_ble_identity_reset_battery(&s_state);
  error = persist_activity_locked();
  if (error == ESP_OK && changed && s_hid_device != NULL)
    error = esp_hidd_dev_battery_set(
        s_hid_device, s_state.config.battery_current_percent);
  xSemaphoreGive(s_mutex);
  return error;
}

esp_err_t typx_identity_service_bind_hid(esp_hidd_dev_t *device) {
  esp_err_t error = ESP_OK;
  if (s_mutex == NULL) return ESP_ERR_INVALID_STATE;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  s_hid_device = device;
  if (device != NULL)
    error = esp_hidd_dev_battery_set(
        device, s_state.config.battery_current_percent);
  xSemaphoreGive(s_mutex);
  return error;
}

void typx_identity_service_record_successful_report(
    uint64_t now_ms, bool execution_active) {
  bool changed;
  if (s_mutex == NULL) return;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  changed = typx_ble_identity_record_successful_report(
      &s_state, now_ms, execution_active);
  if (changed) {
    (void)persist_activity_locked();
    if (s_hid_device != NULL)
      (void)esp_hidd_dev_battery_set(
          s_hid_device, s_state.config.battery_current_percent);
  }
  xSemaphoreGive(s_mutex);
}

void typx_identity_service_end_activity(void) {
  if (s_mutex == NULL) return;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  typx_ble_identity_end_activity(&s_state);
  (void)persist_activity_locked();
  xSemaphoreGive(s_mutex);
}
