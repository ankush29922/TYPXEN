#include "typx_identity_http.h"

#include <string.h>

#include "cJSON.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "typx_ble_hid.h"
#include "typx_identity_service.h"
#include "typx_wifi_service.h"

#define IDENTITY_JSON_MAX 1024u

static esp_err_t send_json(
    httpd_req_t *request, const char *status, cJSON *root) {
  char *json;
  esp_err_t error;
  if (root == NULL) return ESP_ERR_NO_MEM;
  json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (json == NULL) return ESP_ERR_NO_MEM;
  error = httpd_resp_set_status(request, status);
  if (error == ESP_OK)
    error = httpd_resp_set_type(request, "application/json");
  if (error == ESP_OK)
    error = httpd_resp_send(request, json, HTTPD_RESP_USE_STRLEN);
  cJSON_free(json);
  return error;
}

static esp_err_t send_error(
    httpd_req_t *request, const char *status,
    const char *code, const char *field) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "ok", false);
  cJSON_AddStringToObject(root, "error", code);
  if (field != NULL) cJSON_AddStringToObject(root, "field", field);
  return send_json(request, status, root);
}

static const char *validation_field(
    typx_ble_identity_validation_t validation) {
  switch (validation) {
    case TYPX_IDENTITY_INVALID_DEVICE_NAME: return "deviceName";
    case TYPX_IDENTITY_INVALID_MANUFACTURER: return "manufacturerName";
    case TYPX_IDENTITY_INVALID_MODEL: return "modelNumber";
    case TYPX_IDENTITY_INVALID_SERIAL: return "serialNumber";
    case TYPX_IDENTITY_INVALID_HARDWARE_REVISION: return "hardwareRevision";
    case TYPX_IDENTITY_INVALID_SOFTWARE_REVISION: return "softwareRevision";
    case TYPX_IDENTITY_INVALID_BATTERY_RANGE: return "battery";
    case TYPX_IDENTITY_INVALID_BATTERY_CURRENT: return "currentPercent";
    case TYPX_IDENTITY_INVALID_BATTERY_INTERVAL: return "dropActiveMinutes";
    default: return NULL;
  }
}

static cJSON *identity_json(void) {
  typx_ble_identity_state_t state;
  cJSON *root = cJSON_CreateObject();
  cJSON *battery = cJSON_CreateObject();
  cJSON *source = cJSON_CreateObject();
  if (root == NULL || battery == NULL || source == NULL) {
    cJSON_Delete(root);
    cJSON_Delete(battery);
    cJSON_Delete(source);
    return NULL;
  }
  typx_identity_service_snapshot(&state);
  cJSON_AddBoolToObject(root, "ok", true);
  cJSON_AddStringToObject(root, "deviceName", state.config.device_name);
  cJSON_AddStringToObject(
      root, "manufacturerName", state.config.manufacturer_name);
  cJSON_AddStringToObject(root, "modelNumber", state.config.model_number);
  cJSON_AddStringToObject(root, "serialNumber", state.config.serial_number);
  cJSON_AddStringToObject(
      root, "hardwareRevision", state.config.hardware_revision);
  cJSON_AddStringToObject(root, "firmwareRevision", state.firmware_revision);
  cJSON_AddStringToObject(
      root, "softwareRevision", state.config.software_revision);
  cJSON_AddStringToObject(root, "boardIdentifier", state.board_identifier);
  cJSON_AddBoolToObject(battery, "simulationEnabled",
      state.config.battery_simulation_enabled);
  cJSON_AddNumberToObject(
      battery, "minimumPercent", state.config.battery_minimum_percent);
  cJSON_AddNumberToObject(
      battery, "maximumPercent", state.config.battery_maximum_percent);
  cJSON_AddNumberToObject(
      battery, "currentPercent", state.config.battery_current_percent);
  cJSON_AddNumberToObject(battery, "dropActiveMinutes",
      state.config.battery_drop_active_minutes);
  cJSON_AddItemToObject(root, "battery", battery);
  cJSON_AddBoolToObject(root, "bleConnected", typx_ble_hid_connected());
  cJSON_AddBoolToObject(root, "restartRequired", state.restart_required);
  cJSON_AddStringToObject(source, "deviceName",
      state.source_device_name_nvs ? "nvs" : "default");
  cJSON_AddStringToObject(source, "manufacturerName",
      state.source_manufacturer_nvs ? "nvs" : "default");
  cJSON_AddStringToObject(source, "modelNumber",
      state.source_model_nvs ? "nvs" : "default");
  cJSON_AddStringToObject(source, "serialNumber",
      state.source_serial_nvs ? "nvs" : "generated");
  cJSON_AddItemToObject(root, "source", source);
  return root;
}

static esp_err_t get_handler(httpd_req_t *request) {
  return send_json(request, "200 OK", identity_json());
}

static cJSON *read_json(httpd_req_t *request) {
  char body[IDENTITY_JSON_MAX + 1u];
  int received = 0;
  if (request->content_len <= 0 ||
      request->content_len > IDENTITY_JSON_MAX) return NULL;
  while (received < request->content_len) {
    int count = httpd_req_recv(
        request, body + received, request->content_len - received);
    if (count <= 0) return NULL;
    received += count;
  }
  body[received] = '\0';
  return cJSON_ParseWithLength(body, (size_t)received);
}

static bool copy_string(
    cJSON *root, const char *name, char *output,
    size_t capacity, bool empty_allowed) {
  cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
  size_t length;
  if (!cJSON_IsString(item) || item->valuestring == NULL) return false;
  length = strlen(item->valuestring);
  if (length >= capacity || (!empty_allowed && length == 0u)) return false;
  memcpy(output, item->valuestring, length + 1u);
  return true;
}

static bool copy_integer(
    cJSON *root, const char *name, int minimum, int maximum, int *output) {
  cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
  if (!cJSON_IsNumber(item) || item->valuedouble != (double)item->valueint ||
      item->valueint < minimum || item->valueint > maximum) return false;
  *output = item->valueint;
  return true;
}

static bool parse_config(
    cJSON *root, typx_ble_identity_config_t *config) {
  cJSON *battery;
  cJSON *enabled;
  int value;
  memset(config, 0, sizeof(*config));
  if (!cJSON_IsObject(root) ||
      cJSON_HasObjectItem(root, "firmwareRevision") ||
      !copy_string(root, "deviceName", config->device_name,
          sizeof(config->device_name), false) ||
      !copy_string(root, "manufacturerName", config->manufacturer_name,
          sizeof(config->manufacturer_name), false) ||
      !copy_string(root, "modelNumber", config->model_number,
          sizeof(config->model_number), false) ||
      !copy_string(root, "serialNumber", config->serial_number,
          sizeof(config->serial_number), true) ||
      !copy_string(root, "hardwareRevision", config->hardware_revision,
          sizeof(config->hardware_revision), false) ||
      !copy_string(root, "softwareRevision", config->software_revision,
          sizeof(config->software_revision), false)) return false;
  battery = cJSON_GetObjectItemCaseSensitive(root, "battery");
  enabled = cJSON_GetObjectItemCaseSensitive(battery, "simulationEnabled");
  if (!cJSON_IsObject(battery) || !cJSON_IsBool(enabled)) return false;
  config->battery_simulation_enabled = cJSON_IsTrue(enabled);
  if (!copy_integer(battery, "minimumPercent", 0, 99, &value)) return false;
  config->battery_minimum_percent = (uint8_t)value;
  if (!copy_integer(battery, "maximumPercent", 1, 100, &value)) return false;
  config->battery_maximum_percent = (uint8_t)value;
  if (!copy_integer(battery, "currentPercent", 0, 100, &value)) return false;
  config->battery_current_percent = (uint8_t)value;
  if (!copy_integer(battery, "dropActiveMinutes", 1,
          TYPX_IDENTITY_BATTERY_MAX_INTERVAL_MINUTES, &value)) return false;
  config->battery_drop_active_minutes = (uint16_t)value;
  return true;
}

static esp_err_t put_handler(httpd_req_t *request) {
  typx_ble_identity_config_t config;
  typx_ble_identity_validation_t validation = TYPX_IDENTITY_VALID;
  cJSON *root = read_json(request);
  esp_err_t error;
  if (typx_wifi_service_job_locked())
    return send_error(
        request, "409 Conflict", "DEDICATED_JOB_ACTIVE", NULL);
  if (root == NULL || !parse_config(root, &config)) {
    cJSON_Delete(root);
    return send_error(
        request, "400 Bad Request", "INVALID_IDENTITY_REQUEST", NULL);
  }
  cJSON_Delete(root);
  error = typx_identity_service_save(&config, &validation);
  if (error == ESP_ERR_INVALID_ARG)
    return send_error(request, "422 Unprocessable Entity",
        typx_ble_identity_validation_code(validation),
        validation_field(validation));
  if (error != ESP_OK)
    return send_error(request, "500 Internal Server Error", "SAVE_FAILED", NULL);
  return send_json(request, "200 OK", identity_json());
}

static esp_err_t reset_handler(httpd_req_t *request) {
  if (typx_wifi_service_job_locked())
    return send_error(
        request, "409 Conflict", "DEDICATED_JOB_ACTIVE", NULL);
  esp_err_t error = typx_identity_service_reset();
  if (error != ESP_OK)
    return send_error(
        request, "500 Internal Server Error", "RESET_FAILED", NULL);
  return send_json(request, "200 OK", identity_json());
}

static esp_err_t battery_reset_handler(httpd_req_t *request) {
  if (typx_wifi_service_job_locked())
    return send_error(
        request, "409 Conflict", "DEDICATED_JOB_ACTIVE", NULL);
  esp_err_t error = typx_identity_service_battery_reset();
  if (error != ESP_OK)
    return send_error(request, "500 Internal Server Error",
        "BATTERY_RESET_FAILED", NULL);
  return send_json(request, "200 OK", identity_json());
}

static void reboot_task(void *context) {
  (void)context;
  (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  vTaskDelay(pdMS_TO_TICKS(500u));
  esp_restart();
}

static esp_err_t restart_handler(httpd_req_t *request) {
  TaskHandle_t task = NULL;
  typx_ble_identity_state_t identity;
  bool clear_bonds;
  cJSON *root;
  esp_err_t error;
  if (typx_wifi_service_job_locked())
    return send_error(
        request, "409 Conflict", "DEDICATED_JOB_ACTIVE", NULL);
  if (!typx_hid_runtime_is_idle(typx_ble_hid_runtime()))
    return send_error(request, "409 Conflict", "HID_EXECUTION_ACTIVE", NULL);
  typx_identity_service_snapshot(&identity);
  clear_bonds = typx_ble_identity_restart_clears_bonds(&identity);
  if (xTaskCreate(reboot_task, "ble_reboot", 2048, NULL, 5, &task) != pdPASS)
    return send_error(request, "500 Internal Server Error",
        "RESTART_UNAVAILABLE", NULL);
  if (!typx_ble_hid_prepare_reboot()) {
    vTaskDelete(task);
    return send_error(request, "500 Internal Server Error",
        "HID_RELEASE_FAILED", NULL);
  }
  if (clear_bonds && !typx_ble_hid_clear_identity_bonds()) {
    vTaskDelete(task);
    return send_error(request, "500 Internal Server Error",
        "BOND_CLEAR_FAILED", NULL);
  }
  root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "ok", true);
  cJSON_AddBoolToObject(root, "rebooting", true);
  cJSON_AddStringToObject(root, "restartMode", "boardReboot");
  cJSON_AddBoolToObject(root, "bondsClearing", clear_bonds);
  error = send_json(request, "202 Accepted", root);
  if (error == ESP_OK) {
    xTaskNotifyGive(task);
  } else {
    vTaskDelete(task);
  }
  return error;
}

static esp_err_t register_uri(
    httpd_handle_t server, const char *uri, httpd_method_t method,
    esp_err_t (*handler)(httpd_req_t *)) {
  httpd_uri_t config = {
    .uri = uri, .method = method, .handler = handler, .user_ctx = NULL};
  return httpd_register_uri_handler(server, &config);
}

esp_err_t typx_identity_http_register(httpd_handle_t server) {
  esp_err_t error;
#define REGISTER(path, method, handler) do { \
  error = register_uri(server, path, method, handler); \
  if (error != ESP_OK) return error; \
} while (0)
  REGISTER("/v1/device-identity", HTTP_GET, get_handler);
  REGISTER("/v1/device-identity", HTTP_PUT, put_handler);
  REGISTER("/v1/device-identity/reset", HTTP_POST, reset_handler);
  REGISTER("/v1/device-identity/battery-reset", HTTP_POST,
      battery_reset_handler);
  REGISTER("/v1/ble/restart", HTTP_POST, restart_handler);
#undef REGISTER
  return ESP_OK;
}
