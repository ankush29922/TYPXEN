#include <stdio.h>
#include <string.h>

#include "typx_wifi_station.h"

#define CHECK(condition) do { \
  if (!(condition)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
    return 1; \
  } \
} while (0)

int main(void) {
  typx_wifi_station_config_t config = {0};
  char max_ssid[33];
  char max_password[64];
  memset(max_ssid, 'S', sizeof(max_ssid) - 1u);
  max_ssid[sizeof(max_ssid) - 1u] = '\0';
  memset(max_password, 'P', sizeof(max_password) - 1u);
  max_password[sizeof(max_password) - 1u] = '\0';

  CHECK(!typx_wifi_station_credentials_valid(NULL, "password"));
  CHECK(!typx_wifi_station_credentials_valid("", "password"));
  CHECK(!typx_wifi_station_credentials_valid("Phone", "short"));
  CHECK(typx_wifi_station_credentials_valid("Phone", "password"));
  CHECK(typx_wifi_station_credentials_valid(max_ssid, max_password));
  CHECK(!typx_wifi_station_credentials_valid(
      "123456789012345678901234567890123", "password"));
  CHECK(!typx_wifi_station_credentials_valid(
      "Phone", "1234567890123456789012345678901234567890123456789012345678901234"));

  CHECK(typx_wifi_station_config_set(&config, "MyPhone", "secret123"));
  CHECK(config.configured);
  CHECK(strcmp(config.ssid, "MyPhone") == 0);
  CHECK(strcmp(config.password, "secret123") == 0);
  CHECK(!typx_wifi_station_config_set(&config, "MyPhone", "bad"));
  CHECK(strcmp(config.password, "secret123") == 0);
  typx_wifi_station_config_clear(&config);
  CHECK(!config.configured);
  CHECK(config.ssid[0] == '\0');
  CHECK(config.password[0] == '\0');
  CHECK(typx_wifi_station_retry_action(false, 0u, 5u) ==
      TYPX_WIFI_STA_RETRY_NOW);
  CHECK(typx_wifi_station_retry_action(true, 4u, 5u) ==
      TYPX_WIFI_STA_RETRY_NOW);
  CHECK(typx_wifi_station_retry_action(false, 5u, 5u) ==
      TYPX_WIFI_STA_RECOVERY_AP);
  CHECK(typx_wifi_station_retry_action(true, 5u, 5u) ==
      TYPX_WIFI_STA_RETRY_DELAYED);
  CHECK(typx_wifi_station_retry_action(true, 50u, 5u) ==
      TYPX_WIFI_STA_RETRY_DELAYED);
  puts("Wi-Fi station config tests passed");
  return 0;
}
