#ifndef TYPX_IDENTITY_SERVICE_H
#define TYPX_IDENTITY_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_hidd.h"
#include "typx_ble_identity.h"

esp_err_t typx_identity_service_init(void);
void typx_identity_service_snapshot(typx_ble_identity_state_t *snapshot);
esp_err_t typx_identity_service_save(
    const typx_ble_identity_config_t *requested,
    typx_ble_identity_validation_t *validation);
esp_err_t typx_identity_service_reset(void);
esp_err_t typx_identity_service_battery_reset(void);
esp_err_t typx_identity_service_bind_hid(esp_hidd_dev_t *device);
void typx_identity_service_record_successful_report(
    uint64_t now_ms, bool execution_active);
void typx_identity_service_end_activity(void);

#endif
