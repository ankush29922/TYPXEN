#include "typx_ble_hid.h"

#include <stdlib.h>
#include <string.h>

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_check.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_hid_common.h"
#include "esp_hidd.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "keyboard_identity_defaults.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "typx_execution_safety.h"
#include "typx_identity_service.h"

static const char *TAG = "typx_ble_hid";
static esp_hidd_dev_t *s_hid_device;
static typx_hid_runtime_t s_runtime;
static bool s_adv_data_ready;
static bool s_scan_response_ready;
static bool s_hid_ready;
static bool s_authenticated;
static typx_ble_identity_state_t s_identity;
static typx_execution_safety_t s_execution_safety;
static const typx_ble_hid_execution_observer_t *s_execution_observer;
static portMUX_TYPE s_safety_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_safety_task_generation;

static const uint8_t KEYBOARD_REPORT_MAP[] = {
  0x05, 0x01, 0x09, 0x06, 0xa1, 0x01, 0x85, 0x01,
  0x05, 0x07, 0x19, 0xe0, 0x29, 0xe7, 0x15, 0x00,
  0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
  0x95, 0x01, 0x75, 0x08, 0x81, 0x03, 0x95, 0x05,
  0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05,
  0x91, 0x02, 0x95, 0x01, 0x75, 0x03, 0x91, 0x03,
  0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65,
  0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00,
  0xc0,
  0x05, 0x0c, 0x09, 0x01, 0xa1, 0x01, 0x85, 0x02,
  0x15, 0x00, 0x26, 0xff, 0x03, 0x19, 0x00, 0x2a,
  0xff, 0x03, 0x75, 0x10, 0x95, 0x01, 0x81, 0x00,
  0xc0
};

static esp_hid_raw_report_map_t s_report_maps[] = {{
  .data = KEYBOARD_REPORT_MAP,
  .len = sizeof(KEYBOARD_REPORT_MAP)
}};

static esp_hid_device_config_t s_hid_config = {
  .vendor_id = TYPX_IDENTITY_NEUTRAL_PNP_VENDOR_ID,
  .product_id = TYPX_IDENTITY_NEUTRAL_PNP_PRODUCT_ID,
  .version = TYPX_IDENTITY_NEUTRAL_PNP_VERSION,
  .report_maps = s_report_maps,
  .report_maps_len = 1
};

static esp_ble_adv_params_t s_adv_params = {
  .adv_int_min = 0x20,
  .adv_int_max = 0x30,
  .adv_type = ADV_TYPE_IND,
  .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
  .channel_map = ADV_CHNL_ALL,
  .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY
};

static bool send_report(void *context, const uint8_t report[8]) {
  esp_err_t error;
  (void)context;
  if (s_hid_device == NULL || !s_runtime.connected) {
    return false;
  }
  error = esp_hidd_dev_input_set(
      s_hid_device, 0u, 1u, (uint8_t *)report, 8u);
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "HID report failed: %s", esp_err_to_name(error));
    return false;
  }
  typx_identity_service_record_successful_report(
      (uint64_t)esp_timer_get_time() / 1000u, s_runtime.running);
  return true;
}

static bool send_consumer_report(
    void *context, const uint8_t report[2]) {
  esp_err_t error;
  (void)context;
  if (s_hid_device == NULL || !s_runtime.connected) return false;
  error = esp_hidd_dev_input_set(
      s_hid_device, 0u, 2u, (uint8_t *)report, 2u);
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "Consumer release report failed: %s", esp_err_to_name(error));
    return false;
  }
  return true;
}

static bool release_all_reports(void) {
  static const uint8_t consumer_release[2] = {0};
  unsigned attempt;
  bool consumer_released = false;
  if (typx_hid_release_all(&s_runtime) != TYPX_HID_OK) return false;
  for (attempt = 0u; attempt < TYPX_HID_RELEASE_MAX_ATTEMPTS; ++attempt) {
    if (send_consumer_report(NULL, consumer_release)) {
      consumer_released = true;
      break;
    }
  }
  return consumer_released;
}

static bool marker_read(void *context, bool *active) {
  nvs_handle_t handle;
  uint8_t value = 0u;
  esp_err_t error;
  (void)context;
  if (active == NULL ||
      nvs_open("typx_exec", NVS_READWRITE, &handle) != ESP_OK) {
    return false;
  }
  error = nvs_get_u8(handle, "active", &value);
  nvs_close(handle);
  if (error != ESP_OK && error != ESP_ERR_NVS_NOT_FOUND) return false;
  *active = error == ESP_OK && value != 0u;
  return true;
}

static bool marker_write(void *context, bool active) {
  nvs_handle_t handle;
  esp_err_t error;
  (void)context;
  if (nvs_open("typx_exec", NVS_READWRITE, &handle) != ESP_OK) return false;
  error = nvs_set_u8(handle, "active", active ? 1u : 0u);
  if (error == ESP_OK) error = nvs_commit(handle);
  nvs_close(handle);
  return error == ESP_OK;
}

static void safety_release_task(void *context) {
  static const uint8_t keyboard_release[8] = {0};
  static const uint8_t consumer_release[2] = {0};
  uint32_t generation = (uint32_t)(uintptr_t)context;
  typx_hid_safety_snapshot_t status;
  bool begin;
  bool keyboard_released;
  bool consumer_released;

  vTaskDelay(pdMS_TO_TICKS(TYPX_EXECUTION_RELEASE_INITIAL_DELAY_MS));
  for (;;) {
    portENTER_CRITICAL(&s_safety_lock);
    begin = typx_execution_safety_begin_release(
        &s_execution_safety, generation);
    portEXIT_CRITICAL(&s_safety_lock);
    if (!begin) break;

    keyboard_released = send_report(NULL, keyboard_release);
    consumer_released = send_consumer_report(NULL, consumer_release);

    portENTER_CRITICAL(&s_safety_lock);
    (void)typx_execution_safety_complete_release(
        &s_execution_safety, generation,
        keyboard_released, consumer_released);
    typx_execution_safety_snapshot(&s_execution_safety, &status);
    portEXIT_CRITICAL(&s_safety_lock);

    if (status.ready) {
      ESP_LOGI(
          TAG, "BLE HID safety release confirmed after %u attempt(s)",
          status.attempts);
      break;
    }
    if (status.generation != generation || !status.connected ||
        status.attempts >= TYPX_EXECUTION_RELEASE_MAX_ATTEMPTS) {
      if (status.generation == generation && status.connected) {
        ESP_LOGW(
            TAG, "BLE HID safety release paused after %u attempts: %s",
            status.attempts,
            typx_hid_safety_error_code(status.error));
      }
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(TYPX_EXECUTION_RELEASE_RETRY_DELAY_MS));
  }

  portENTER_CRITICAL(&s_safety_lock);
  if (s_safety_task_generation == generation) {
    s_safety_task_generation = 0u;
  }
  portEXIT_CRITICAL(&s_safety_lock);
  vTaskDelete(NULL);
}

static bool schedule_safety_release(uint32_t generation) {
  bool schedule = false;
  if (generation == 0u) return false;
  portENTER_CRITICAL(&s_safety_lock);
  if (s_safety_task_generation == generation) {
    portEXIT_CRITICAL(&s_safety_lock);
    return true;
  }
  if (s_execution_safety.connection_generation == generation &&
      s_execution_safety.connected && s_execution_safety.authenticated) {
    s_safety_task_generation = generation;
    schedule = true;
  }
  portEXIT_CRITICAL(&s_safety_lock);
  if (!schedule) return false;
  if (xTaskCreate(
          safety_release_task, "hid_safety", 3072,
          (void *)(uintptr_t)generation, 4, NULL) != pdPASS) {
    portENTER_CRITICAL(&s_safety_lock);
    if (s_safety_task_generation == generation) {
      s_safety_task_generation = 0u;
    }
    portEXIT_CRITICAL(&s_safety_lock);
    ESP_LOGE(TAG, "Unable to schedule BLE HID safety release");
    return false;
  }
  return true;
}

static bool scheduler_wait_ticks(TickType_t total_ticks) {
  TickType_t last_wake = xTaskGetTickCount();
  const TickType_t max_slice = pdMS_TO_TICKS(10u);
  while (total_ticks > 0u) {
    TickType_t slice = total_ticks > max_slice ? max_slice : total_ticks;
    if (s_runtime.stop_requested || !s_runtime.connected) {
      return false;
    }
    (void)xTaskDelayUntil(&last_wake, slice);
    total_ticks -= slice;
  }
  return !s_runtime.stop_requested && s_runtime.connected;
}

static bool wait_ms(void *context, uint32_t delay_ms) {
  TickType_t ticks;
  (void)context;
  if (delay_ms == 0u) {
    return true;
  }
  ticks = pdMS_TO_TICKS(delay_ms);
  return scheduler_wait_ticks(ticks == 0u ? 1u : ticks);
}

static void countdown(void *context, unsigned seconds_left) {
  (void)context;
  ESP_LOGI(TAG, "Calibration starts in %u", seconds_left);
}

static esp_err_t start_advertising(void) {
  esp_err_t error = esp_ble_gap_start_advertising(&s_adv_params);
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "Advertising start failed: %s", esp_err_to_name(error));
  }
  return error;
}

static void start_advertising_when_ready(void) {
  if (s_adv_data_ready && s_scan_response_ready && s_hid_ready &&
      !s_runtime.connected) {
    (void)start_advertising();
  }
}

static void gap_callback(
    esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *parameter) {
  switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
      s_adv_data_ready = true;
      start_advertising_when_ready();
      break;
    case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
      s_scan_response_ready = true;
      start_advertising_when_ready();
      break;
    case ESP_GAP_BLE_AUTH_CMPL_EVT:
      if (parameter->ble_security.auth_cmpl.success) {
        uint32_t generation;
        s_authenticated = true;
        portENTER_CRITICAL(&s_safety_lock);
        typx_execution_safety_authenticated(&s_execution_safety, true);
        generation = s_execution_safety.connection_generation;
        portEXIT_CRITICAL(&s_safety_lock);
        ESP_LOGI(TAG, "BLE authentication succeeded; bond retained in NVS");
        (void)schedule_safety_release(generation);
      } else {
        s_authenticated = false;
        portENTER_CRITICAL(&s_safety_lock);
        typx_execution_safety_authenticated(&s_execution_safety, false);
        portEXIT_CRITICAL(&s_safety_lock);
        ESP_LOGE(
            TAG, "BLE authentication failed: 0x%x",
            parameter->ble_security.auth_cmpl.fail_reason);
      }
      break;
    case ESP_GAP_BLE_SEC_REQ_EVT:
      ESP_ERROR_CHECK_WITHOUT_ABORT(esp_ble_gap_security_rsp(
          parameter->ble_security.ble_req.bd_addr, true));
      break;
    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
      ESP_LOGI(
          TAG,
          "BLE connection params: interval=%u units latency=%u timeout=%u ms",
          parameter->update_conn_params.conn_int,
          parameter->update_conn_params.latency,
          parameter->update_conn_params.timeout * 10u);
      break;
    case ESP_GAP_BLE_REMOVE_BOND_DEV_COMPLETE_EVT:
      ESP_LOGI(
          TAG, "Bond removal result: %s",
          parameter->remove_bond_dev_cmpl.status == ESP_BT_STATUS_SUCCESS
              ? "OK" : "FAILED");
      break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
      ESP_LOGI(
          TAG, "BLE advertising: %s",
          parameter->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS
              ? "started" : "failed");
      break;
    default:
      break;
  }
}

static void hid_callback(
    void *handler_args,
    esp_event_base_t base,
    int32_t id,
    void *event_data) {
  esp_hidd_event_t event = (esp_hidd_event_t)id;
  esp_hidd_event_data_t *parameter = event_data;
  (void)handler_args;
  (void)base;
  switch (event) {
    case ESP_HIDD_START_EVENT:
      s_hid_ready = true;
      if (typx_identity_service_bind_hid(s_hid_device) == ESP_OK) {
        ESP_LOGI(
            TAG,
            "BLE HID, Battery Service, and Device Information Service ready");
      } else {
        ESP_LOGE(TAG, "Battery Service initial value failed");
      }
      ESP_LOGI(TAG, "No key will be sent automatically");
      start_advertising_when_ready();
      break;
    case ESP_HIDD_CONNECT_EVENT:
      typx_hid_runtime_set_connected(&s_runtime, true);
      portENTER_CRITICAL(&s_safety_lock);
      (void)typx_execution_safety_connected(&s_execution_safety);
      portEXIT_CRITICAL(&s_safety_lock);
      ESP_LOGI(TAG, "BLE HID host connected; safety release pending authentication");
      break;
    case ESP_HIDD_DISCONNECT_EVENT:
      s_authenticated = false;
      portENTER_CRITICAL(&s_safety_lock);
      typx_execution_safety_disconnected(&s_execution_safety);
      portEXIT_CRITICAL(&s_safety_lock);
      typx_hid_runtime_request_stop(&s_runtime);
      typx_hid_runtime_set_connected(&s_runtime, false);
      ESP_LOGW(TAG, "BLE HID host disconnected; execution state cleared");
      (void)start_advertising();
      break;
    case ESP_HIDD_PROTOCOL_MODE_EVENT:
      ESP_LOGI(
          TAG, "HID protocol mode: %s",
          parameter->protocol_mode.protocol_mode ? "report" : "boot");
      break;
    case ESP_HIDD_STOP_EVENT:
      ESP_LOGI(TAG, "BLE HID stopped");
      break;
    default:
      break;
  }
}

static void gatts_callback(
    esp_gatts_cb_event_t event,
    esp_gatt_if_t gatts_if,
    esp_ble_gatts_cb_param_t *parameter) {
  if (event == ESP_GATTS_CONNECT_EVT) {
    esp_ble_conn_update_params_t params = {0};
    memcpy(params.bda, parameter->connect.remote_bda, sizeof(params.bda));
    params.min_int = 0x10u;
    params.max_int = 0x20u;
    params.latency = 0u;
    params.timeout = TYPX_BLE_SUPERVISION_TIMEOUT_UNITS;
    if (esp_ble_gap_update_conn_params(&params) != ESP_OK) {
      ESP_LOGW(TAG, "BLE connection-parameter request was rejected locally");
    } else {
      ESP_LOGI(
          TAG,
          "Requested BLE supervision timeout %u ms; host may override",
          TYPX_BLE_SUPERVISION_TIMEOUT_MS);
    }
  }
  esp_hidd_gatts_event_handler(event, gatts_if, parameter);
}

static void shutdown_release(void) {
  if (s_runtime.connected) {
    (void)release_all_reports();
  }
}

esp_err_t typx_ble_hid_init(void) {
  static const uint8_t hid_service_uuid[] = {
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
    0x00, 0x10, 0x00, 0x00, 0x12, 0x18, 0x00, 0x00
  };
  esp_ble_adv_data_t adv_data = {
    .set_scan_rsp = false,
    .include_name = false,
    .include_txpower = false,
    .min_interval = 0x0006,
    .max_interval = 0x0010,
    .appearance = ESP_HID_APPEARANCE_KEYBOARD,
    .service_uuid_len = sizeof(hid_service_uuid),
    .p_service_uuid = (uint8_t *)hid_service_uuid,
    .flag = ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT
  };
  esp_ble_adv_data_t scan_response = {
    .set_scan_rsp = true,
    .include_name = true,
    .include_txpower = false
  };
  esp_bt_controller_config_t controller_config =
      BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  esp_bluedroid_config_t bluedroid_config = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
  typx_hid_runtime_io_t io = {
    .context = NULL,
    .send_report = send_report,
    .wait_ms = wait_ms,
    .countdown = countdown
  };
  esp_ble_auth_req_t auth = ESP_LE_AUTH_BOND;
  esp_ble_io_cap_t io_capability = ESP_IO_CAP_NONE;
  uint8_t key_mask = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
  uint8_t key_size = 16u;
  esp_err_t error;
  typx_execution_marker_io_t marker = {
    .context = NULL,
    .read_active = marker_read,
    .write_active = marker_write
  };

  typx_hid_runtime_init(&s_runtime, &io);
  error = nvs_flash_init();
  if (error == ESP_ERR_NVS_NO_FREE_PAGES ||
      error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "NVS recovery erase failed");
    error = nvs_flash_init();
  }
  ESP_RETURN_ON_ERROR(error, TAG, "NVS init failed");
  if (!typx_execution_safety_init(&s_execution_safety, &marker)) {
    return ESP_FAIL;
  }
  ESP_RETURN_ON_ERROR(
      typx_identity_service_init(), TAG, "BLE identity init failed");
  typx_identity_service_snapshot(&s_identity);
  s_hid_config.device_name = s_identity.config.device_name;
  s_hid_config.manufacturer_name = s_identity.config.manufacturer_name;
  s_hid_config.serial_number = s_identity.config.serial_number;
  ESP_RETURN_ON_ERROR(
      esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT),
      TAG, "Classic Bluetooth memory release failed");
  controller_config.mode = ESP_BT_MODE_BLE;
  ESP_RETURN_ON_ERROR(
      esp_bt_controller_init(&controller_config), TAG, "BT controller init failed");
  ESP_RETURN_ON_ERROR(
      esp_bt_controller_enable(ESP_BT_MODE_BLE), TAG, "BLE enable failed");
  bluedroid_config.ssp_en = false;
  ESP_RETURN_ON_ERROR(
      esp_bluedroid_init_with_cfg(&bluedroid_config), TAG, "Bluedroid init failed");
  ESP_RETURN_ON_ERROR(esp_bluedroid_enable(), TAG, "Bluedroid enable failed");
  ESP_RETURN_ON_ERROR(
      esp_ble_gap_register_callback(gap_callback), TAG, "GAP callback failed");
  ESP_RETURN_ON_ERROR(
      esp_ble_gatts_register_callback(gatts_callback),
      TAG, "GATTS callback failed");
  ESP_RETURN_ON_ERROR(
      esp_ble_gap_set_security_param(
          ESP_BLE_SM_AUTHEN_REQ_MODE, &auth, sizeof(auth)),
      TAG, "BLE auth config failed");
  ESP_RETURN_ON_ERROR(
      esp_ble_gap_set_security_param(
          ESP_BLE_SM_IOCAP_MODE, &io_capability, sizeof(io_capability)),
      TAG, "BLE IO capability config failed");
  ESP_RETURN_ON_ERROR(
      esp_ble_gap_set_security_param(
          ESP_BLE_SM_SET_INIT_KEY, &key_mask, sizeof(key_mask)),
      TAG, "BLE initiator key config failed");
  ESP_RETURN_ON_ERROR(
      esp_ble_gap_set_security_param(
          ESP_BLE_SM_SET_RSP_KEY, &key_mask, sizeof(key_mask)),
      TAG, "BLE responder key config failed");
  ESP_RETURN_ON_ERROR(
      esp_ble_gap_set_security_param(
          ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(key_size)),
      TAG, "BLE key size config failed");
  ESP_RETURN_ON_ERROR(
      esp_ble_gap_set_device_name(s_identity.config.device_name),
      TAG, "BLE device name failed");
  ESP_RETURN_ON_ERROR(
      esp_ble_gap_config_adv_data(&adv_data), TAG, "BLE advertising config failed");
  ESP_RETURN_ON_ERROR(
      esp_ble_gap_config_adv_data(&scan_response),
      TAG, "BLE scan-response config failed");
  ESP_RETURN_ON_ERROR(
      esp_hidd_dev_init(
          &s_hid_config,
          ESP_HID_TRANSPORT_BLE,
          hid_callback,
          &s_hid_device),
      TAG, "BLE HID init failed");
  ESP_RETURN_ON_ERROR(
      esp_register_shutdown_handler(shutdown_release),
      TAG, "Shutdown release handler failed");
  return ESP_OK;
}

bool typx_ble_hid_connected(void) {
  return s_runtime.connected;
}

bool typx_ble_hid_safety_ready(void) {
  typx_hid_safety_snapshot_t status;
  typx_ble_hid_safety_status(&status);
  return status.ready;
}

void typx_ble_hid_safety_status(typx_hid_safety_snapshot_t *status) {
  if (status == NULL) return;
  portENTER_CRITICAL(&s_safety_lock);
  typx_execution_safety_snapshot(&s_execution_safety, status);
  portEXIT_CRITICAL(&s_safety_lock);
}

bool typx_ble_hid_retry_safety_release(void) {
  typx_hid_safety_snapshot_t status;
  bool accepted;
  uint32_t generation;
  portENTER_CRITICAL(&s_safety_lock);
  accepted = typx_execution_safety_request_retry(&s_execution_safety);
  typx_execution_safety_snapshot(&s_execution_safety, &status);
  generation = status.generation;
  portEXIT_CRITICAL(&s_safety_lock);
  if (!accepted || status.ready) return accepted;
  return schedule_safety_release(generation);
}

bool typx_ble_hid_interrupted_reset(void) {
  return typx_execution_safety_interrupted(&s_execution_safety);
}

bool typx_ble_hid_mark_execution_active(void) {
  return typx_execution_safety_begin(&s_execution_safety);
}

bool typx_ble_hid_mark_execution_terminal(void) {
  return typx_execution_safety_finish(&s_execution_safety);
}

void typx_ble_hid_request_stop(void) {
  typx_hid_runtime_request_stop(&s_runtime);
}

const char *typx_ble_hid_device_name(void) {
  return s_identity.config.device_name;
}

bool typx_ble_hid_prepare_reboot(void) {
  typx_hid_runtime_request_stop(&s_runtime);
  if (s_runtime.connected && !release_all_reports()) return false;
  (void)esp_ble_gap_stop_advertising();
  return true;
}

void typx_ble_hid_end_activity(void) {
  typx_identity_service_end_activity();
}

static bool clear_bonds(bool require_disconnected) {
  int count;
  int index;
  esp_ble_bond_dev_t *devices;
  bool success = true;
  if (!typx_hid_runtime_is_idle(&s_runtime) ||
      (require_disconnected && s_runtime.connected)) {
    return false;
  }
  count = esp_ble_get_bond_device_num();
  if (count <= 0) {
    ESP_LOGI(TAG, "No BLE bonds to clear");
    return true;
  }
  devices = calloc((size_t)count, sizeof(*devices));
  if (devices == NULL) {
    return false;
  }
  if (esp_ble_get_bond_device_list(&count, devices) != ESP_OK) {
    free(devices);
    return false;
  }
  for (index = 0; index < count; ++index) {
    if (esp_ble_remove_bond_device(devices[index].bd_addr) != ESP_OK) {
      success = false;
    }
  }
  free(devices);
  ESP_LOGI(TAG, "Bond removal requested for %d device(s)", count);
  return success;
}

bool typx_ble_hid_clear_bonds(void) {
  return clear_bonds(true);
}

bool typx_ble_hid_clear_identity_bonds(void) {
  return clear_bonds(false);
}

typx_hid_runtime_t *typx_ble_hid_runtime(void) {
  return &s_runtime;
}

static uint64_t executor_now_us(void *context) {
  (void)context;
  return (uint64_t)esp_timer_get_time();
}

static bool executor_wait_us(void *context, uint32_t delay_us) {
  uint64_t scaled_ticks;
  TickType_t ticks;
  (void)context;
  if (delay_us == 0u) {
    return true;
  }
  scaled_ticks = (uint64_t)delay_us * (uint64_t)configTICK_RATE_HZ;
  ticks = (TickType_t)((scaled_ticks + 999999u) / 1000000u);
  return scheduler_wait_ticks(ticks == 0u ? 1u : ticks);
}

static bool executor_key_down(
    void *context, uint8_t modifier, uint8_t keycode) {
  (void)context;
  return typx_hid_key_down(&s_runtime, modifier, keycode) == TYPX_HID_OK;
}

static bool executor_release(void *context) {
  (void)context;
  return release_all_reports();
}

static bool executor_stop_requested(void *context) {
  (void)context;
  return s_runtime.stop_requested || !s_runtime.connected;
}

static void executor_progress(
    void *context,
    uint32_t source_index,
    uint32_t completed_records,
    uint32_t total_records) {
  (void)context;
  (void)source_index;
  if (s_execution_observer != NULL &&
      s_execution_observer->progress != NULL) {
    s_execution_observer->progress(
        s_execution_observer->context,
        source_index, completed_records, total_records);
  }
  ESP_LOGD(TAG, "Progress %lu/%lu", (unsigned long)completed_records,
      (unsigned long)total_records);
}

static void executor_terminal(
    void *context,
    typx_terminal_status_t status,
    typx_executor_error_t error,
    typx_protocol_error_t protocol_error) {
  (void)context;
  ESP_LOGI(
      TAG, "Execution terminal status=%d error=%s protocol=%s",
      (int)status,
      typx_executor_error_code(error),
      typx_protocol_error_code(protocol_error));
}

static typx_executor_io_t executor_io(void) {
  typx_executor_io_t io = {
    .context = NULL,
    .now_us = executor_now_us,
    .wait_us = executor_wait_us,
    .key_down = executor_key_down,
    .release_all = executor_release,
    .stop_requested = executor_stop_requested,
    .progress = executor_progress,
    .terminal = executor_terminal
  };
  return io;
}

typx_executor_error_t typx_ble_hid_run_schedule_countdown_observed(
    const typx_verified_schedule_v1_t *schedule,
    unsigned countdown_seconds,
    const typx_ble_hid_execution_observer_t *observer) {
  typx_executor_io_t io = executor_io();
  typx_executor_error_t result;
  unsigned seconds_left;
  if (!typx_protocol_v1_is_verified(schedule)) {
    return TYPX_EXECUTOR_NOT_VERIFIED;
  }
  if (!s_runtime.connected || s_runtime.running) {
    return TYPX_EXECUTOR_WAIT_FAILED;
  }
  s_runtime.running = true;
  s_runtime.stop_requested = false;
  s_execution_observer = observer;
  if (!release_all_reports()) {
    s_runtime.running = false;
    s_execution_observer = NULL;
    typx_identity_service_end_activity();
    return TYPX_EXECUTOR_RELEASE_FAILED;
  }
  for (seconds_left = countdown_seconds; seconds_left > 0u; --seconds_left) {
    ESP_LOGI(TAG, "Schedule starts in %u", seconds_left);
    if (!wait_ms(NULL, 1000u)) {
      result = s_runtime.stop_requested
          ? TYPX_EXECUTOR_STOPPED
          : TYPX_EXECUTOR_WAIT_FAILED;
      (void)release_all_reports();
      s_runtime.running = false;
      s_runtime.stop_requested = false;
      s_execution_observer = NULL;
      typx_identity_service_end_activity();
      return result;
    }
  }
  if (observer != NULL && observer->countdown_complete != NULL) {
    observer->countdown_complete(observer->context);
  }
  result = typx_executor_run(schedule, &io);
  if (!release_all_reports()) {
    result = TYPX_EXECUTOR_RELEASE_FAILED;
  }
  s_runtime.running = false;
  s_runtime.stop_requested = false;
  s_execution_observer = NULL;
  typx_identity_service_end_activity();
  return result;
}

typx_executor_error_t typx_ble_hid_run_schedule_countdown(
    const typx_verified_schedule_v1_t *schedule,
    unsigned countdown_seconds) {
  return typx_ble_hid_run_schedule_countdown_observed(
      schedule, countdown_seconds, NULL);
}

typx_executor_error_t typx_ble_hid_run_schedule(
    const typx_verified_schedule_v1_t *schedule) {
  return typx_ble_hid_run_schedule_countdown(schedule, 0u);
}
