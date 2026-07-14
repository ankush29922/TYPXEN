#include <stdio.h>
#include <string.h>

#include "typx_discovery.h"

#define CHECK(condition) do { \
  if (!(condition)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
    return 1; \
  } \
} while (0)

int main(void) {
  const uint8_t first[6] = {0xaa, 0xbb, 0xcc, 0x12, 0x34, 0x56};
  const uint8_t second[6] = {0xaa, 0xbb, 0xcc, 0x65, 0x43, 0x21};
  char first_name[TYPX_MDNS_HOSTNAME_BYTES];
  char repeated_name[TYPX_MDNS_HOSTNAME_BYTES];
  char second_name[TYPX_MDNS_HOSTNAME_BYTES];
  size_t count;
  size_t index;
  const typx_discovery_txt_t *txt;
  typx_discovery_hostname(first, first_name);
  typx_discovery_hostname(first, repeated_name);
  typx_discovery_hostname(second, second_name);
  CHECK(strcmp(first_name, "typxen-esp32cam-123456") == 0);
  CHECK(strcmp(first_name, repeated_name) == 0);
  CHECK(strcmp(first_name, second_name) != 0);
  CHECK(strcmp(TYPX_MDNS_SERVICE, "_typx") == 0);
  CHECK(strcmp(TYPX_MDNS_PROTOCOL, "_tcp") == 0);
  CHECK(!typx_discovery_should_start(false, false, false));
  CHECK(typx_discovery_should_start(true, false, false));
  CHECK(!typx_discovery_should_start(true, true, false));
  CHECK(typx_discovery_should_start(true, true, true));
  txt = typx_discovery_txt(&count);
  CHECK(count == 3u);
  CHECK(strcmp(txt[0].key, "board") == 0);
  CHECK(strcmp(txt[0].value, "esp32_cam") == 0);
  CHECK(strcmp(txt[1].key, "protocol") == 0);
  CHECK(strcmp(txt[1].value, "1.0") == 0);
  CHECK(strcmp(txt[2].key, "schedule") == 0);
  CHECK(strcmp(txt[2].value, "1") == 0);
  for (index = 0u; index < count; ++index) {
    CHECK(strstr(txt[index].key, "password") == NULL);
    CHECK(strstr(txt[index].key, "job") == NULL);
    CHECK(strstr(txt[index].key, "source") == NULL);
    CHECK(strstr(txt[index].key, "mac") == NULL);
  }
  puts("Discovery tests passed");
  return 0;
}
