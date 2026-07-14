idf_component_register(
  SRCS "main.c" "typx_ble_hid.c" "typx_console.c" "typx_embedded_schedules.c"
       "typx_identity_http.c" "typx_identity_service.c" "typx_wifi_service.c"
  INCLUDE_DIRS "."
  EMBED_FILES "schedules/standard.bin" "schedules/personal.bin"
  REQUIRES bt console esp_app_format esp_event esp_hid esp_http_server esp_hw_support esp_netif
           esp_partition esp_wifi json mbedtls nvs_flash typx_ble_identity typx_protocol
           typx_discovery typx_execution_safety typx_executor typx_hid_runtime typx_local_schedule
           typx_wifi_station typx_wifi_upload mdns
)
