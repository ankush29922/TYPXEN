#include "esp_app_desc.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "typx_ble_hid.h"
#include "typx_console.h"
#include "typx_protocol.h"
#include "typx_state_machine.h"
#include "typx_wifi_service.h"

static const char *TAG = "typx_firmware";

void app_main(void) {
  const esp_app_desc_t *description = esp_app_get_description();
  typx_protocol_limits_t limits = typx_protocol_v1_esp32_cam_limits();
  typx_state_machine_t state;
  typx_state_machine_boot(&state);

  ESP_LOGI(
      TAG, "Typx ESP32-CAM firmware %s",
      description == NULL ? "unknown" : description->version);
  ESP_LOGI(TAG, "ESP-IDF %s; protocol 1.0; schedule format 1", IDF_VER);
  ESP_LOGI(
      TAG,
      "Target esp32; schedule limit %llu bytes; record limit %lu",
      (unsigned long long)limits.max_encoded_bytes,
      (unsigned long)limits.max_record_count);
  ESP_ERROR_CHECK(typx_ble_hid_init());
  ESP_ERROR_CHECK(typx_wifi_service_init());
  ESP_ERROR_CHECK(typx_console_start());
  ESP_LOGI(
      TAG,
      "BLE-only device %s ready; calibration requires an explicit UART command",
      typx_ble_hid_device_name());
}
