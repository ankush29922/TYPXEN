#ifndef TYPX_WIFI_SERVICE_H
#define TYPX_WIFI_SERVICE_H

#include <stdbool.h>

#include "esp_err.h"

esp_err_t typx_wifi_service_init(void);
esp_err_t typx_wifi_service_start(void);
esp_err_t typx_wifi_service_start_station(void);
esp_err_t typx_wifi_service_start_ap(void);
esp_err_t typx_wifi_service_stop(void);
esp_err_t typx_wifi_service_configure_station(
    const char *ssid, const char *password);
esp_err_t typx_wifi_service_forget_station(void);
void typx_wifi_service_print_info(void);
bool typx_wifi_service_job_locked(void);

#endif
