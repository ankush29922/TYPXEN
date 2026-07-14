#ifndef TYPX_WIFI_STATION_H
#define TYPX_WIFI_STATION_H

#include <stdbool.h>

#define TYPX_WIFI_STA_SSID_BYTES 33u
#define TYPX_WIFI_STA_PASSWORD_BYTES 65u

typedef struct {
  char ssid[TYPX_WIFI_STA_SSID_BYTES];
  char password[TYPX_WIFI_STA_PASSWORD_BYTES];
  bool configured;
} typx_wifi_station_config_t;

typedef enum {
  TYPX_WIFI_STA_RETRY_NOW = 0,
  TYPX_WIFI_STA_RETRY_DELAYED,
  TYPX_WIFI_STA_RECOVERY_AP
} typx_wifi_station_retry_action_t;

bool typx_wifi_station_credentials_valid(
    const char *ssid, const char *password);
bool typx_wifi_station_config_set(
    typx_wifi_station_config_t *config,
    const char *ssid, const char *password);
void typx_wifi_station_config_clear(typx_wifi_station_config_t *config);
typx_wifi_station_retry_action_t typx_wifi_station_retry_action(
    bool post_job_reconnect,
    unsigned int attempts, unsigned int retry_limit);

#endif
