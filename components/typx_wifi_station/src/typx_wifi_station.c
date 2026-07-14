#include "typx_wifi_station.h"

#include <stddef.h>
#include <string.h>

bool typx_wifi_station_credentials_valid(
    const char *ssid, const char *password) {
  size_t ssid_length = ssid == NULL ? 0u : strlen(ssid);
  size_t password_length = password == NULL ? 0u : strlen(password);
  return ssid_length >= 1u && ssid_length < TYPX_WIFI_STA_SSID_BYTES &&
      password_length >= 8u && password_length <= 63u;
}

bool typx_wifi_station_config_set(
    typx_wifi_station_config_t *config,
    const char *ssid, const char *password) {
  if (config == NULL ||
      !typx_wifi_station_credentials_valid(ssid, password)) return false;
  strcpy(config->ssid, ssid);
  strcpy(config->password, password);
  config->configured = true;
  return true;
}

void typx_wifi_station_config_clear(typx_wifi_station_config_t *config) {
  if (config == NULL) return;
  memset(config, 0, sizeof(*config));
}

typx_wifi_station_retry_action_t typx_wifi_station_retry_action(
    bool post_job_reconnect,
    unsigned int attempts, unsigned int retry_limit) {
  if (attempts < retry_limit) return TYPX_WIFI_STA_RETRY_NOW;
  return post_job_reconnect
      ? TYPX_WIFI_STA_RETRY_DELAYED
      : TYPX_WIFI_STA_RECOVERY_AP;
}
