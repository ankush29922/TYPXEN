#include "typx_discovery.h"

#include <stdio.h>

static const typx_discovery_txt_t TXT[] = {
  {"board", "esp32_cam"},
  {"protocol", "1.0"},
  {"schedule", "1"},
};

void typx_discovery_hostname(
    const uint8_t identity[6], char hostname[TYPX_MDNS_HOSTNAME_BYTES]) {
  if (identity == NULL || hostname == NULL) return;
  snprintf(
      hostname, TYPX_MDNS_HOSTNAME_BYTES,
      "typxen-esp32cam-%02x%02x%02x",
      identity[3], identity[4], identity[5]);
}

const typx_discovery_txt_t *typx_discovery_txt(size_t *count) {
  if (count != NULL) *count = sizeof(TXT) / sizeof(TXT[0]);
  return TXT;
}

bool typx_discovery_should_start(
    bool http_available, bool station_mode, bool station_has_ip) {
  return http_available && (!station_mode || station_has_ip);
}
