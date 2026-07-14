#ifndef TYPX_DISCOVERY_H
#define TYPX_DISCOVERY_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#define TYPX_MDNS_SERVICE "_typx"
#define TYPX_MDNS_PROTOCOL "_tcp"
#define TYPX_MDNS_HOSTNAME_BYTES 32u

typedef struct {
  const char *key;
  const char *value;
} typx_discovery_txt_t;

void typx_discovery_hostname(
    const uint8_t identity[6], char hostname[TYPX_MDNS_HOSTNAME_BYTES]);
const typx_discovery_txt_t *typx_discovery_txt(size_t *count);
bool typx_discovery_should_start(
    bool http_available, bool station_mode, bool station_has_ip);

#endif
