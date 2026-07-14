#include "typx_wifi_service.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_partition.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include "mdns.h"
#include "nvs.h"
#include "typx_ble_hid.h"
#include "typx_discovery.h"
#include "typx_execution_safety.h"
#include "typx_identity_http.h"
#include "typx_sha256.h"
#include "typx_wifi_station.h"
#include "typx_wifi_upload.h"

#define JSON_BODY_MAX 512u
#define QUERY_MAX 256u
#define HTTP_READ_BYTES 256u
#define STA_CONNECT_RETRIES 5u
#define POST_JOB_STA_RETRY_DELAY_MS 10000u
#define ACTIVE_JOB_STA_RETRY_DELAY_MS 2000u

typedef enum {
  TYPX_WIFI_STOPPED = 0,
  TYPX_WIFI_AP,
  TYPX_WIFI_STA
} typx_wifi_mode_t;

static const char *TAG = "typx_wifi";
static const char *STAGING_LABEL = "typx_stage";
static const char PASSWORD_ALPHABET[] =
    "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";

static const esp_partition_t *s_partition;
static typx_wifi_upload_t s_upload;
static SemaphoreHandle_t s_mutex;
static httpd_handle_t s_http;
static esp_netif_t *s_ap_netif;
static esp_netif_t *s_sta_netif;
static bool s_wifi_initialized;
static bool s_wifi_started;
static bool s_execution_wifi_pause;
static bool s_fallback_task_pending;
static bool s_http_restart_task_pending;
static bool s_mdns_started;
static bool s_post_job_station_reconnect;
static unsigned int s_sta_connect_attempts;
static typx_wifi_mode_t s_wifi_mode;
static char s_ap_ssid[33];
static char s_ap_password[17];
static typx_wifi_station_config_t s_station;
static char s_active_ip[16] = "0.0.0.0";
static char s_mdns_hostname[TYPX_MDNS_HOSTNAME_BYTES];
static uint16_t s_http_port = 80u;
static TaskHandle_t s_execution_task;
static const typx_verified_schedule_v1_t *s_pending_schedule;

static esp_err_t start_http(void);
static esp_err_t stop_http(void);
static esp_err_t initialize_wifi(void);
static esp_err_t start_mode(typx_wifi_mode_t mode);
static void schedule_station_fallback(void);

static void stop_mdns(void) {
  if (!s_mdns_started) return;
  mdns_free();
  s_mdns_started = false;
}

static void start_mdns(void) {
  mdns_txt_item_t txt[3];
  const typx_discovery_txt_t *source;
  size_t count;
  size_t index;
  esp_err_t error;
  if (s_mdns_started || !s_wifi_started ||
      !typx_discovery_should_start(
          s_http != NULL, s_wifi_mode == TYPX_WIFI_STA,
          strcmp(s_active_ip, "0.0.0.0") != 0)) return;
  source = typx_discovery_txt(&count);
  if (count > sizeof(txt) / sizeof(txt[0])) return;
  for (index = 0u; index < count; ++index) {
    txt[index].key = source[index].key;
    txt[index].value = source[index].value;
  }
  error = mdns_init();
  if (error == ESP_OK) error = mdns_hostname_set(s_mdns_hostname);
  if (error == ESP_OK) error = mdns_instance_name_set(s_mdns_hostname);
  if (error == ESP_OK) {
    error = mdns_service_add(
        s_mdns_hostname, TYPX_MDNS_SERVICE, TYPX_MDNS_PROTOCOL,
        s_http_port, txt, count);
  }
  if (error != ESP_OK) {
    ESP_LOGW(TAG, "mDNS unavailable: %s", esp_err_to_name(error));
    mdns_free();
    return;
  }
  s_mdns_started = true;
  ESP_LOGI(
      TAG, "mDNS: %s.local %s.%s port %u",
      s_mdns_hostname, TYPX_MDNS_SERVICE, TYPX_MDNS_PROTOCOL, s_http_port);
}

static void http_restart_task(void *context) {
  esp_err_t stop_error;
  esp_err_t start_error = ESP_OK;
  (void)context;
  stop_error = stop_http();
  if (s_wifi_mode == TYPX_WIFI_STA &&
      s_wifi_started && strcmp(s_active_ip, "0.0.0.0") != 0) {
    start_error = start_http();
    if (start_error == ESP_OK) start_mdns();
  }
  if (stop_error != ESP_OK || start_error != ESP_OK) {
    ESP_LOGE(
        TAG, "HTTP restart after station IP failed: stop=%s start=%s",
        esp_err_to_name(stop_error), esp_err_to_name(start_error));
  }
  s_http_restart_task_pending = false;
  vTaskDelete(NULL);
}

static void schedule_http_restart(void) {
  if (s_http_restart_task_pending) return;
  s_http_restart_task_pending = true;
  if (xTaskCreate(
          http_restart_task, "http_restart", 3072, NULL, 4,
          NULL) != pdPASS) {
    s_http_restart_task_pending = false;
    ESP_LOGE(TAG, "Unable to schedule HTTP restart");
  }
}

static void station_fallback_task(void *context) {
  esp_err_t error = ESP_OK;
  bool retry_needed = false;
  (void)context;
  if (s_execution_task != NULL && s_station.configured) {
    vTaskDelay(pdMS_TO_TICKS(ACTIVE_JOB_STA_RETRY_DELAY_MS));
    if (s_execution_task != NULL && s_wifi_mode == TYPX_WIFI_STA &&
        s_wifi_started && strcmp(s_active_ip, "0.0.0.0") == 0) {
      ESP_LOGW(TAG, "Retrying control Wi-Fi during active job");
      error = esp_wifi_connect();
      retry_needed = true;
    }
  } else if (s_post_job_station_reconnect && s_station.configured) {
    vTaskDelay(pdMS_TO_TICKS(POST_JOB_STA_RETRY_DELAY_MS));
    if (s_post_job_station_reconnect && s_station.configured &&
        !s_execution_wifi_pause && s_execution_task == NULL) {
      s_sta_connect_attempts = 0u;
      ESP_LOGI(TAG, "Retrying saved phone hotspot after post-job delay");
      error = s_wifi_mode == TYPX_WIFI_STA && s_wifi_started
          ? esp_wifi_connect()
          : typx_wifi_service_start_station();
      retry_needed = error != ESP_OK;
    }
  } else if (s_wifi_mode == TYPX_WIFI_STA && !s_execution_wifi_pause) {
    ESP_LOGW(TAG, "Phone hotspot unavailable; starting recovery AP");
    error = start_mode(TYPX_WIFI_AP);
  }
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "Wi-Fi recovery action failed: %s", esp_err_to_name(error));
  }
  s_fallback_task_pending = false;
  if (retry_needed &&
      (s_execution_task != NULL || s_post_job_station_reconnect)) {
    schedule_station_fallback();
  }
  vTaskDelete(NULL);
}

static void schedule_station_fallback(void) {
  if (s_fallback_task_pending) return;
  s_fallback_task_pending = true;
  if (xTaskCreate(
          station_fallback_task, "wifi_fallback", 3072, NULL, 4,
          NULL) != pdPASS) {
    s_fallback_task_pending = false;
    ESP_LOGE(TAG, "Unable to schedule recovery AP");
  }
}

static void wifi_event_handler(
    void *context, esp_event_base_t event_base,
    int32_t event_id, void *event_data) {
  (void)context;
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START &&
      s_wifi_mode == TYPX_WIFI_STA && !s_execution_wifi_pause) {
    s_sta_connect_attempts = 0u;
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_connect());
  } else if (
      event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED &&
      s_wifi_mode == TYPX_WIFI_STA && !s_execution_wifi_pause) {
    strcpy(s_active_ip, "0.0.0.0");
    typx_wifi_station_retry_action_t action =
        typx_wifi_station_retry_action(
            s_post_job_station_reconnect,
            s_sta_connect_attempts, STA_CONNECT_RETRIES);
    if (action == TYPX_WIFI_STA_RETRY_NOW) {
      ++s_sta_connect_attempts;
      ESP_LOGW(
          TAG, "Phone hotspot reconnect attempt %u/%u",
          s_sta_connect_attempts, STA_CONNECT_RETRIES);
      ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_connect());
    } else {
      schedule_station_fallback();
    }
  } else if (
      event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP &&
      s_wifi_mode == TYPX_WIFI_STA) {
    const ip_event_got_ip_t *event = event_data;
    snprintf(
        s_active_ip, sizeof(s_active_ip), IPSTR,
        IP2STR(&event->ip_info.ip));
    s_sta_connect_attempts = 0u;
    s_post_job_station_reconnect = false;
    ESP_LOGI(TAG, "Connected to phone hotspot at %s", s_active_ip);
    schedule_http_restart();
  }
}

static bool storage_erase(void *context) {
  const esp_partition_t *partition = context;
  return partition != NULL &&
      esp_partition_erase_range(partition, 0u, partition->size) == ESP_OK;
}

static bool storage_erase_range(
    void *context, uint32_t offset, size_t length) {
  const esp_partition_t *partition = context;
  return partition != NULL && offset <= partition->size &&
      length <= partition->size - offset &&
      esp_partition_erase_range(partition, offset, length) == ESP_OK;
}

static bool storage_write(
    void *context, uint32_t offset, const uint8_t *data, size_t length) {
  const esp_partition_t *partition = context;
  return partition != NULL && data != NULL && offset <= partition->size &&
      length <= partition->size - offset &&
      esp_partition_write(partition, offset, data, length) == ESP_OK;
}

static bool storage_read(
    void *context, uint32_t offset, uint8_t *data, size_t length) {
  const esp_partition_t *partition = context;
  return partition != NULL && data != NULL && offset <= partition->size &&
      length <= partition->size - offset &&
      esp_partition_read(partition, offset, data, length) == ESP_OK;
}

static bool parse_hex(
    const char *text, uint8_t *output, size_t output_length) {
  size_t index;
  if (text == NULL || strlen(text) != output_length * 2u) return false;
  for (index = 0u; index < output_length; ++index) {
    char pair[3] = {text[index * 2u], text[index * 2u + 1u], '\0'};
    char *end;
    unsigned long value;
    errno = 0;
    value = strtoul(pair, &end, 16);
    if (errno != 0 || *end != '\0' || value > 255u) return false;
    output[index] = (uint8_t)value;
  }
  return true;
}

static void format_hex(
    const uint8_t *data, size_t length, char *output) {
  static const char HEX[] = "0123456789abcdef";
  size_t index;
  for (index = 0u; index < length; ++index) {
    output[index * 2u] = HEX[data[index] >> 4];
    output[index * 2u + 1u] = HEX[data[index] & 0x0fu];
  }
  output[length * 2u] = '\0';
}

static bool parse_u32(const char *text, int base, uint32_t *value) {
  char *end;
  unsigned long parsed;
  if (text == NULL || *text == '\0' || value == NULL) return false;
  errno = 0;
  parsed = strtoul(text, &end, base);
  if (errno != 0 || *end != '\0' || parsed > UINT32_MAX) return false;
  *value = (uint32_t)parsed;
  return true;
}

static esp_err_t send_json(
    httpd_req_t *request, const char *status, const char *json) {
  ESP_RETURN_ON_ERROR(httpd_resp_set_status(request, status), TAG, "status");
  ESP_RETURN_ON_ERROR(
      httpd_resp_set_type(request, "application/json"), TAG, "content type");
  return httpd_resp_send(request, json, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t send_upload_error(
    httpd_req_t *request, typx_upload_error_t error) {
  char json[192];
  bool retryable = error == TYPX_UPLOAD_HID_NOT_CONNECTED ||
      error == TYPX_UPLOAD_HID_SAFETY_PENDING ||
      error == TYPX_UPLOAD_HID_SAFETY_FAILED;
  snprintf(
      json, sizeof(json),
      "{\"ok\":false,\"error\":\"%s\",\"state\":\"%s\","
      "\"retryable\":%s}",
      typx_wifi_upload_error_code(error),
      typx_wifi_upload_status_code(s_upload.status),
      retryable ? "true" : "false");
  return send_json(
      request,
      error == TYPX_UPLOAD_INVALID_STATE || retryable
          ? "409 Conflict" : "400 Bad Request",
      json);
}

static cJSON *read_json(httpd_req_t *request) {
  char body[JSON_BODY_MAX + 1u];
  int received = 0;
  if (request->content_len <= 0 || request->content_len > JSON_BODY_MAX)
    return NULL;
  while (received < request->content_len) {
    int result = httpd_req_recv(
        request, body + received, request->content_len - received);
    if (result <= 0) return NULL;
    received += result;
  }
  body[received] = '\0';
  return cJSON_ParseWithLength(body, (size_t)received);
}

static bool json_job_id(cJSON *root, uint8_t job_id[16]) {
  cJSON *item = cJSON_GetObjectItemCaseSensitive(root, "jobId");
  return cJSON_IsString(item) && parse_hex(item->valuestring, job_id, 16u);
}

static esp_err_t info_handler(httpd_req_t *request) {
  const esp_app_desc_t *description = esp_app_get_description();
  cJSON *root;
  char *json;
  esp_err_t error;
  const char *network_ssid =
      s_wifi_mode == TYPX_WIFI_STA ? s_station.ssid : s_ap_ssid;
  root = cJSON_CreateObject();
  if (root == NULL) return ESP_ERR_NO_MEM;
  cJSON_AddBoolToObject(root, "ok", true);
  cJSON_AddStringToObject(root, "protocol", "1.0");
  cJSON_AddNumberToObject(root, "scheduleFormat", 1);
  cJSON_AddStringToObject(
      root, "firmwareVersion",
      description == NULL ? "unknown" : description->version);
  cJSON_AddNumberToObject(root, "maxBytes", TYPX_UPLOAD_MAX_BYTES);
  cJSON_AddNumberToObject(
      root, "maxRecords", TYPX_ESP32_CAM_MAX_RECORDS);
  cJSON_AddNumberToObject(root, "chunkBytes", TYPX_UPLOAD_CHUNK_BYTES);
  cJSON_AddStringToObject(root, "ssid", network_ssid);
  cJSON_AddStringToObject(root, "ip", s_active_ip);
  {
    cJSON *capabilities = cJSON_CreateObject();
    if (capabilities == NULL) {
      cJSON_Delete(root);
      return ESP_ERR_NO_MEM;
    }
    cJSON_AddBoolToObject(capabilities, "deviceIdentityConfig", true);
    cJSON_AddBoolToObject(capabilities, "batterySimulationConfig", true);
    cJSON_AddBoolToObject(capabilities, "bleRestart", true);
    cJSON_AddBoolToObject(capabilities, "dedicatedRunControl", true);
    cJSON_AddItemToObject(root, "capabilities", capabilities);
  }
  json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (json == NULL) return ESP_ERR_NO_MEM;
  error = send_json(request, "200 OK", json);
  cJSON_free(json);
  return error;
}

static esp_err_t status_handler(httpd_req_t *request) {
  char job[33];
  char json[768];
  bool start_allowed;
  typx_hid_safety_snapshot_t hid;
  typx_ble_hid_safety_status(&hid);
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  format_hex(s_upload.job_id, 16u, job);
  start_allowed = s_upload.status == TYPX_UPLOAD_READY && hid.ready;
  snprintf(
      json, sizeof(json),
      "{\"ok\":true,\"state\":\"%s\",\"jobId\":\"%s\","
      "\"receivedBytes\":%lu,\"expectedBytes\":%lu,"
      "\"hidConnected\":%s,\"hidAuthenticated\":%s,"
      "\"hidSafetyState\":\"%s\",\"hidSafetyReady\":%s,"
      "\"hidSafetyAttempts\":%u,\"hidSafetyError\":\"%s\","
      "\"hidSafetyRetryAvailable\":%s,"
      "\"startAllowed\":%s,"
      "\"countdownMs\":%u,\"completedRecords\":%lu,"
      "\"totalRecords\":%lu,\"currentSourceIndex\":%lu,"
      "\"error\":\"%s\"}",
      typx_wifi_upload_status_code(s_upload.status), job,
      (unsigned long)s_upload.received_bytes,
      (unsigned long)s_upload.expected_size,
      hid.connected ? "true" : "false",
      hid.authenticated ? "true" : "false",
      typx_hid_safety_state_code(hid.state),
      hid.ready ? "true" : "false",
      hid.attempts,
      typx_hid_safety_error_code(hid.error),
      hid.state == TYPX_HID_SAFETY_FAILED_RETRYABLE &&
              (hid.error == TYPX_HID_SAFETY_ERROR_AUTH_FAILED ||
               hid.attempts >= TYPX_EXECUTION_RELEASE_MAX_ATTEMPTS)
          ? "true" : "false",
      start_allowed ? "true" : "false",
      TYPX_DEDICATED_COUNTDOWN_MS,
      (unsigned long)s_upload.completed_records,
      (unsigned long)s_upload.total_records,
      (unsigned long)s_upload.current_source_index,
      typx_wifi_upload_error_code(s_upload.last_error));
  xSemaphoreGive(s_mutex);
  return send_json(request, "200 OK", json);
}

static esp_err_t result_handler(httpd_req_t *request) {
  char json[384];
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  snprintf(
      json, sizeof(json),
      "{\"ok\":true,\"state\":\"%s\",\"uploadError\":\"%s\","
      "\"protocolError\":\"%s\",\"executorError\":\"%s\","
      "\"completedRecords\":%lu,\"totalRecords\":%lu}",
      typx_wifi_upload_status_code(s_upload.status),
      typx_wifi_upload_error_code(s_upload.last_error),
      typx_protocol_error_code(s_upload.protocol_error),
      typx_executor_error_code(s_upload.executor_error),
      (unsigned long)s_upload.completed_records,
      (unsigned long)s_upload.total_records);
  xSemaphoreGive(s_mutex);
  return send_json(request, "200 OK", json);
}

static esp_err_t create_job_handler(httpd_req_t *request) {
  cJSON *root = read_json(request);
  cJSON *size_item;
  cJSON *sha_item;
  uint8_t id[16];
  uint8_t sha[32];
  uint32_t size;
  typx_upload_error_t error;
  if (root == NULL || !json_job_id(root, id)) {
    if (root != NULL) cJSON_Delete(root);
    return send_upload_error(request, TYPX_UPLOAD_INVALID_ARGUMENT);
  }
  size_item = cJSON_GetObjectItemCaseSensitive(root, "size");
  sha_item = cJSON_GetObjectItemCaseSensitive(root, "sha256");
  if (!cJSON_IsNumber(size_item) || size_item->valuedouble < 1.0 ||
      size_item->valuedouble > TYPX_UPLOAD_MAX_BYTES ||
      size_item->valuedouble != (double)size_item->valueint ||
      !cJSON_IsString(sha_item) ||
      !parse_hex(sha_item->valuestring, sha, 32u)) {
    cJSON_Delete(root);
    return send_upload_error(request, TYPX_UPLOAD_INVALID_ARGUMENT);
  }
  size = (uint32_t)size_item->valueint;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  error = typx_wifi_upload_begin(&s_upload, id, size, sha);
  xSemaphoreGive(s_mutex);
  cJSON_Delete(root);
  return error == TYPX_UPLOAD_OK
      ? send_json(request, "201 Created", "{\"ok\":true,\"state\":\"UPLOADING\"}")
      : send_upload_error(request, error);
}

static bool query_value(
    const char *query, const char *key, char *value, size_t length) {
  return httpd_query_key_value(query, key, value, length) == ESP_OK;
}

static esp_err_t chunk_handler(httpd_req_t *request) {
  char query[QUERY_MAX];
  char job_text[33], sequence_text[12], offset_text[12];
  char length_text[12], crc_text[12];
  uint8_t id[16], buffer[HTTP_READ_BYTES];
  uint32_t sequence, offset, length, crc;
  uint32_t received = 0u;
  typx_upload_chunk_session_t session;
  typx_upload_error_t error;
  if (request->content_len <= 0 ||
      request->content_len > TYPX_UPLOAD_CHUNK_BYTES ||
      httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK ||
      !query_value(query, "jobId", job_text, sizeof(job_text)) ||
      !query_value(query, "sequence", sequence_text, sizeof(sequence_text)) ||
      !query_value(query, "offset", offset_text, sizeof(offset_text)) ||
      !query_value(query, "length", length_text, sizeof(length_text)) ||
      !query_value(query, "crc32", crc_text, sizeof(crc_text)) ||
      !parse_hex(job_text, id, 16u) ||
      !parse_u32(sequence_text, 10, &sequence) ||
      !parse_u32(offset_text, 10, &offset) ||
      !parse_u32(length_text, 10, &length) ||
      !parse_u32(crc_text, 16, &crc) ||
      length != (uint32_t)request->content_len) {
    return send_upload_error(request, TYPX_UPLOAD_INVALID_ARGUMENT);
  }
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  error = typx_wifi_upload_chunk_begin(
      &s_upload, id, sequence, offset, length, crc, &session);
  while (error == TYPX_UPLOAD_OK && received < length) {
    size_t wanted = length - received;
    int count;
    if (wanted > sizeof(buffer)) wanted = sizeof(buffer);
    count = httpd_req_recv(request, (char *)buffer, wanted);
    if (count <= 0) {
      error = TYPX_UPLOAD_INCOMPLETE;
      break;
    }
    error = typx_wifi_upload_chunk_write(&session, buffer, (size_t)count);
    received += (uint32_t)count;
  }
  if (error == TYPX_UPLOAD_OK)
    error = typx_wifi_upload_chunk_finish(&session);
  xSemaphoreGive(s_mutex);
  return error == TYPX_UPLOAD_OK
      ? send_json(request, "200 OK", "{\"ok\":true}")
      : send_upload_error(request, error);
}

static esp_err_t finalize_handler(httpd_req_t *request) {
  cJSON *root = read_json(request);
  uint8_t id[16];
  uint8_t expected_sha[32];
  cJSON *size_item;
  cJSON *sha_item;
  mbedtls_sha256_context sha_context;
  typx_sha256_provider_t sha = typx_sha256_mbedtls_provider(&sha_context);
  typx_upload_error_t error;
  if (root == NULL || !json_job_id(root, id)) {
    if (root != NULL) cJSON_Delete(root);
    return send_upload_error(request, TYPX_UPLOAD_INVALID_ARGUMENT);
  }
  size_item = cJSON_GetObjectItemCaseSensitive(root, "size");
  sha_item = cJSON_GetObjectItemCaseSensitive(root, "sha256");
  if (!cJSON_IsNumber(size_item) || size_item->valuedouble < 1.0 ||
      size_item->valuedouble > TYPX_UPLOAD_MAX_BYTES ||
      size_item->valuedouble != (double)size_item->valueint ||
      !cJSON_IsString(sha_item) ||
      !parse_hex(sha_item->valuestring, expected_sha, 32u)) {
    cJSON_Delete(root);
    return send_upload_error(request, TYPX_UPLOAD_INVALID_ARGUMENT);
  }
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  error = typx_wifi_upload_finalize(
      &s_upload, id, (uint32_t)size_item->valueint, expected_sha, &sha);
  xSemaphoreGive(s_mutex);
  cJSON_Delete(root);
  return error == TYPX_UPLOAD_OK
      ? send_json(request, "200 OK", "{\"ok\":true,\"state\":\"READY\"}")
      : send_upload_error(request, error);
}

static void execution_countdown_complete(void *context) {
  (void)context;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  (void)typx_wifi_upload_mark_executing(&s_upload);
  xSemaphoreGive(s_mutex);
}

static void execution_progress(
    void *context,
    uint32_t source_index,
    uint32_t completed_records,
    uint32_t total_records) {
  (void)context;
  typx_wifi_upload_record_progress(
      &s_upload, source_index, completed_records, total_records);
}

static void execution_task(void *context) {
  typx_executor_error_t executor_error;
  typx_ble_hid_execution_observer_t observer = {
    .context = NULL,
    .countdown_complete = execution_countdown_complete,
    .progress = execution_progress
  };
  (void)context;
  (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  stop_mdns();
  executor_error = typx_ble_hid_run_schedule_countdown_observed(
      s_pending_schedule, TYPX_DEDICATED_COUNTDOWN_SECONDS, &observer);
  if (!typx_ble_hid_mark_execution_terminal()) {
    ESP_LOGE(TAG, "Execution marker clear failed");
    executor_error = TYPX_EXECUTOR_WAIT_FAILED;
  }
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  typx_wifi_upload_complete(&s_upload, executor_error);
  xSemaphoreGive(s_mutex);
  s_execution_task = NULL;
  start_mdns();
  vTaskDelete(NULL);
}

static esp_err_t start_handler(httpd_req_t *request) {
  cJSON *root = read_json(request);
  uint8_t id[16];
  typx_upload_error_t error;
  typx_upload_hid_readiness_t hid_readiness;
  typx_hid_safety_snapshot_t hid;
  esp_err_t response_error;
  if (root == NULL || !json_job_id(root, id)) {
    if (root != NULL) cJSON_Delete(root);
    return send_upload_error(request, TYPX_UPLOAD_INVALID_ARGUMENT);
  }
  typx_ble_hid_safety_status(&hid);
  if (!hid.connected) {
    hid_readiness = TYPX_UPLOAD_HID_DISCONNECTED;
  } else if (hid.ready) {
    hid_readiness = TYPX_UPLOAD_HID_READY;
  } else if (
      hid.state == TYPX_HID_SAFETY_FAILED_RETRYABLE &&
      (hid.error == TYPX_HID_SAFETY_ERROR_AUTH_FAILED ||
       hid.attempts >= TYPX_EXECUTION_RELEASE_MAX_ATTEMPTS)) {
    hid_readiness = TYPX_UPLOAD_HID_FAILED;
  } else {
    hid_readiness = TYPX_UPLOAD_HID_PENDING;
  }
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  error = typx_wifi_upload_prepare_start(
      &s_upload, id, hid_readiness, &s_pending_schedule);
  xSemaphoreGive(s_mutex);
  cJSON_Delete(root);
  if (error != TYPX_UPLOAD_OK) return send_upload_error(request, error);
  if (!typx_ble_hid_mark_execution_active()) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    (void)typx_wifi_upload_start_failed(&s_upload);
    xSemaphoreGive(s_mutex);
    return send_upload_error(request, TYPX_UPLOAD_STORAGE_FAILURE);
  }
  if (xTaskCreate(
          execution_task, "wifi_schedule", 6144, NULL, 5,
          &s_execution_task) != pdPASS) {
    (void)typx_ble_hid_mark_execution_terminal();
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    (void)typx_wifi_upload_start_failed(&s_upload);
    xSemaphoreGive(s_mutex);
    return send_upload_error(request, TYPX_UPLOAD_INVALID_STATE);
  }
  response_error = send_json(
      request, "202 Accepted",
      "{\"ok\":true,\"acknowledged\":true,\"state\":\"COUNTDOWN\","
      "\"countdownMs\":8000}");
  if (response_error == ESP_OK) {
    xTaskNotifyGive(s_execution_task);
  } else {
    vTaskDelete(s_execution_task);
    s_execution_task = NULL;
    (void)typx_ble_hid_mark_execution_terminal();
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    (void)typx_wifi_upload_start_failed(&s_upload);
    xSemaphoreGive(s_mutex);
  }
  return response_error;
}

static esp_err_t hid_safety_retry_handler(httpd_req_t *request) {
  char json[256];
  bool active;
  bool accepted;
  typx_hid_safety_snapshot_t hid;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  active = s_upload.status == TYPX_UPLOAD_COUNTDOWN ||
      s_upload.status == TYPX_UPLOAD_EXECUTING ||
      s_upload.status == TYPX_UPLOAD_STOPPING;
  xSemaphoreGive(s_mutex);
  if (active) return send_upload_error(request, TYPX_UPLOAD_INVALID_STATE);

  typx_ble_hid_safety_status(&hid);
  if (!hid.connected) {
    return send_upload_error(request, TYPX_UPLOAD_HID_NOT_CONNECTED);
  }
  if (!hid.authenticated) {
    return send_upload_error(request, TYPX_UPLOAD_HID_SAFETY_PENDING);
  }
  accepted = typx_ble_hid_retry_safety_release();
  typx_ble_hid_safety_status(&hid);
  if (!accepted) {
    return send_upload_error(request, TYPX_UPLOAD_HID_SAFETY_FAILED);
  }
  snprintf(
      json, sizeof(json),
      "{\"ok\":true,\"hidSafetyState\":\"%s\","
      "\"hidSafetyReady\":%s,\"hidSafetyAttempts\":%u,"
      "\"hidSafetyError\":\"%s\"}",
      typx_hid_safety_state_code(hid.state),
      hid.ready ? "true" : "false",
      hid.attempts,
      typx_hid_safety_error_code(hid.error));
  return send_json(
      request, hid.ready ? "200 OK" : "202 Accepted", json);
}

static esp_err_t stop_handler(httpd_req_t *request) {
  typx_upload_error_t error;
  const char *state;
  char json[128];
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  error = typx_wifi_upload_request_stop(&s_upload);
  state = typx_wifi_upload_status_code(s_upload.status);
  xSemaphoreGive(s_mutex);
  if (error != TYPX_UPLOAD_OK) return send_upload_error(request, error);
  typx_ble_hid_request_stop();
  snprintf(
      json, sizeof(json),
      "{\"ok\":true,\"acknowledged\":true,\"state\":\"%s\"}",
      state);
  return send_json(
      request,
      strcmp(state, "STOPPED") == 0 ? "200 OK" : "202 Accepted",
      json);
}

static esp_err_t cancel_handler(httpd_req_t *request) {
  cJSON *root = read_json(request);
  uint8_t id[16];
  typx_upload_error_t error;
  if (root == NULL || !json_job_id(root, id)) {
    if (root != NULL) cJSON_Delete(root);
    return send_upload_error(request, TYPX_UPLOAD_INVALID_ARGUMENT);
  }
  cJSON_Delete(root);
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  error = typx_wifi_upload_cancel(&s_upload, id);
  xSemaphoreGive(s_mutex);
  return error == TYPX_UPLOAD_OK
      ? send_json(request, "200 OK", "{\"ok\":true,\"state\":\"IDLE\"}")
      : send_upload_error(request, error);
}

static esp_err_t register_uri(
    httpd_handle_t server,
    const char *uri,
    httpd_method_t method,
    esp_err_t (*handler)(httpd_req_t *)) {
  httpd_uri_t config = {
    .uri = uri, .method = method, .handler = handler, .user_ctx = NULL};
  return httpd_register_uri_handler(server, &config);
}

static esp_err_t start_http(void) {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  esp_err_t error;
  if (s_http != NULL) return ESP_OK;
  config.max_uri_handlers = 16u;
  config.stack_size = 6144u;
  config.task_priority = 3u;
  s_http_port = config.server_port;
  error = httpd_start(&s_http, &config);
  if (error != ESP_OK) return error;
#define REGISTER_URI(path, method, handler) do { \
  error = register_uri(s_http, path, method, handler); \
  if (error != ESP_OK) goto registration_failed; \
} while (0)
  REGISTER_URI("/v1/info", HTTP_GET, info_handler);
  REGISTER_URI("/v1/jobs", HTTP_POST, create_job_handler);
  REGISTER_URI("/v1/jobs/chunks", HTTP_PUT, chunk_handler);
  REGISTER_URI("/v1/jobs/finalize", HTTP_POST, finalize_handler);
  REGISTER_URI("/v1/jobs/start", HTTP_POST, start_handler);
  REGISTER_URI("/v1/jobs/stop", HTTP_POST, stop_handler);
  REGISTER_URI("/v1/jobs/cancel", HTTP_POST, cancel_handler);
  REGISTER_URI("/v1/hid/safety-release", HTTP_POST, hid_safety_retry_handler);
  REGISTER_URI("/v1/status", HTTP_GET, status_handler);
  REGISTER_URI("/v1/result", HTTP_GET, result_handler);
  error = typx_identity_http_register(s_http);
  if (error != ESP_OK) goto registration_failed;
#undef REGISTER_URI
  return ESP_OK;

registration_failed:
  ESP_LOGE(TAG, "HTTP URI registration failed: %s", esp_err_to_name(error));
  ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_stop(s_http));
  s_http = NULL;
  return error;
}

static esp_err_t load_credentials(void) {
  nvs_handle_t handle;
  size_t ap_password_length = sizeof(s_ap_password);
  size_t sta_ssid_length = sizeof(s_station.ssid);
  size_t sta_password_length = sizeof(s_station.password);
  uint8_t mac[6];
  esp_err_t error;
  esp_err_t ssid_error;
  esp_err_t password_error;
  size_t index;
  ESP_RETURN_ON_ERROR(
      esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP), TAG, "read AP MAC");
  typx_discovery_hostname(mac, s_mdns_hostname);
  snprintf(
      s_ap_ssid, sizeof(s_ap_ssid),
      "Typxen-ESP32CAM-%02X%02X", mac[4], mac[5]);
  ESP_RETURN_ON_ERROR(
      nvs_open("typx_wifi", NVS_READWRITE, &handle), TAG, "open Wi-Fi NVS");
  error = nvs_get_str(
      handle, "ap_pass", s_ap_password, &ap_password_length);
  if (error == ESP_ERR_NVS_NOT_FOUND) {
    for (index = 0u; index < sizeof(s_ap_password) - 1u; ++index) {
      s_ap_password[index] = PASSWORD_ALPHABET[
          esp_random() % (sizeof(PASSWORD_ALPHABET) - 1u)];
    }
    s_ap_password[sizeof(s_ap_password) - 1u] = '\0';
    error = nvs_set_str(handle, "ap_pass", s_ap_password);
    if (error == ESP_OK) error = nvs_commit(handle);
  }
  if (error != ESP_OK) {
    nvs_close(handle);
    return error;
  }
  ssid_error = nvs_get_str(
      handle, "sta_ssid", s_station.ssid, &sta_ssid_length);
  password_error = nvs_get_str(
      handle, "sta_pass", s_station.password, &sta_password_length);
  if (ssid_error == ESP_OK && password_error == ESP_OK &&
      typx_wifi_station_credentials_valid(
          s_station.ssid, s_station.password)) {
    s_station.configured = true;
  } else if (
      (ssid_error == ESP_ERR_NVS_NOT_FOUND || ssid_error == ESP_OK) &&
      (password_error == ESP_ERR_NVS_NOT_FOUND || password_error == ESP_OK)) {
    typx_wifi_station_config_clear(&s_station);
  } else {
    error = ssid_error != ESP_OK ? ssid_error : password_error;
  }
  nvs_close(handle);
  return error;
}

static esp_err_t stop_http(void) {
  httpd_handle_t server;
  stop_mdns();
  if (s_http == NULL) return ESP_OK;
  server = s_http;
  s_http = NULL;
  return httpd_stop(server);
}

static esp_err_t initialize_wifi(void) {
  wifi_init_config_t init_config;
  esp_err_t error;
  if (s_wifi_initialized) return ESP_OK;
  error = esp_netif_init();
  if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) return error;
  error = esp_event_loop_create_default();
  if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) return error;
  s_ap_netif = esp_netif_create_default_wifi_ap();
  s_sta_netif = esp_netif_create_default_wifi_sta();
  if (s_ap_netif == NULL || s_sta_netif == NULL) return ESP_ERR_NO_MEM;
  init_config = (wifi_init_config_t)WIFI_INIT_CONFIG_DEFAULT();
  ESP_RETURN_ON_ERROR(esp_wifi_init(&init_config), TAG, "Wi-Fi init");
  ESP_RETURN_ON_ERROR(
      esp_event_handler_register(
          WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL),
      TAG, "Wi-Fi event handler");
  ESP_RETURN_ON_ERROR(
      esp_event_handler_register(
          IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL),
      TAG, "IP event handler");
  s_wifi_initialized = true;
  return ESP_OK;
}

static esp_err_t start_mode(typx_wifi_mode_t mode) {
  wifi_config_t config = {0};
  esp_netif_ip_info_t ip_info;
  esp_err_t error;
  esp_err_t ret = ESP_OK;
  if (s_execution_task != NULL && !s_execution_wifi_pause)
    return ESP_ERR_INVALID_STATE;
  if (mode == TYPX_WIFI_STA && !s_station.configured)
    return ESP_ERR_INVALID_STATE;
  ESP_RETURN_ON_ERROR(initialize_wifi(), TAG, "Wi-Fi initialize");
  s_execution_wifi_pause = true;
  error = stop_http();
  if (s_wifi_started) {
    esp_err_t stop_error;
    s_wifi_mode = TYPX_WIFI_STOPPED;
    stop_error = esp_wifi_stop();
    s_wifi_started = false;
    if (error == ESP_OK) error = stop_error;
  }
  if (error != ESP_OK) {
    s_execution_wifi_pause = false;
    return error;
  }

  strcpy(s_active_ip, "0.0.0.0");
  if (mode == TYPX_WIFI_AP) {
    memcpy(config.ap.ssid, s_ap_ssid, strlen(s_ap_ssid));
    config.ap.ssid_len = strlen(s_ap_ssid);
    memcpy(config.ap.password, s_ap_password, strlen(s_ap_password));
    config.ap.channel = 1u;
    config.ap.max_connection = 2u;
    config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    config.ap.pmf_cfg.capable = true;
    config.ap.pmf_cfg.required = false;
    ESP_GOTO_ON_ERROR(
        esp_wifi_set_mode(WIFI_MODE_AP), failed, TAG, "AP mode");
    ESP_GOTO_ON_ERROR(
        esp_wifi_set_config(WIFI_IF_AP, &config), failed, TAG, "AP config");
  } else {
    memcpy(config.sta.ssid, s_station.ssid, strlen(s_station.ssid));
    memcpy(
        config.sta.password, s_station.password,
        strlen(s_station.password));
    config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    config.sta.pmf_cfg.capable = true;
    config.sta.pmf_cfg.required = false;
    ESP_GOTO_ON_ERROR(
        esp_wifi_set_mode(WIFI_MODE_STA), failed, TAG, "station mode");
    ESP_GOTO_ON_ERROR(
        esp_wifi_set_config(WIFI_IF_STA, &config), failed, TAG,
        "station config");
  }
  s_wifi_mode = mode;
  s_sta_connect_attempts = 0u;
  s_execution_wifi_pause = false;
  ESP_GOTO_ON_ERROR(esp_wifi_start(), failed, TAG, "Wi-Fi start");
  s_wifi_started = true;
  if (mode == TYPX_WIFI_AP &&
      esp_netif_get_ip_info(s_ap_netif, &ip_info) == ESP_OK) {
    snprintf(
        s_active_ip, sizeof(s_active_ip), IPSTR,
        IP2STR(&ip_info.ip));
  }
  ESP_RETURN_ON_ERROR(start_http(), TAG, "HTTP start");
  if (mode == TYPX_WIFI_AP) start_mdns();
  ESP_LOGI(
      TAG, "%s mode started",
      mode == TYPX_WIFI_STA ? "Phone hotspot station" : "Recovery AP");
  return ESP_OK;

failed:
  s_execution_wifi_pause = false;
  s_wifi_mode = TYPX_WIFI_STOPPED;
  return ret;
}

esp_err_t typx_wifi_service_init(void) {
  typx_upload_storage_t storage;
  s_partition = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, 0x40, STAGING_LABEL);
  if (s_partition == NULL || s_partition->size != TYPX_UPLOAD_MAX_BYTES)
    return ESP_ERR_NOT_FOUND;
  s_mutex = xSemaphoreCreateMutex();
  if (s_mutex == NULL) return ESP_ERR_NO_MEM;
  storage.context = (void *)s_partition;
  storage.erase = storage_erase;
  storage.erase_range = storage_erase_range;
  storage.write = storage_write;
  storage.read = storage_read;
  typx_wifi_upload_init(&s_upload, &storage);
  if (typx_ble_hid_interrupted_reset()) {
    typx_wifi_upload_mark_interrupted_reset(&s_upload);
    ESP_LOGW(TAG, "Interrupted execution detected; schedule will not resume");
  }
  ESP_RETURN_ON_ERROR(load_credentials(), TAG, "Wi-Fi credentials");
  return typx_wifi_service_start();
}

esp_err_t typx_wifi_service_start(void) {
  return s_station.configured
      ? typx_wifi_service_start_station()
      : typx_wifi_service_start_ap();
}

esp_err_t typx_wifi_service_start_station(void) {
  return start_mode(TYPX_WIFI_STA);
}

esp_err_t typx_wifi_service_start_ap(void) {
  s_post_job_station_reconnect = false;
  return start_mode(TYPX_WIFI_AP);
}

esp_err_t typx_wifi_service_stop(void) {
  esp_err_t result;
  if (typx_wifi_service_job_locked()) return ESP_ERR_INVALID_STATE;
  s_post_job_station_reconnect = false;
  s_wifi_mode = TYPX_WIFI_STOPPED;
  s_execution_wifi_pause = true;
  result = stop_http();
  if (s_wifi_started) {
    esp_err_t wifi_error = esp_wifi_stop();
    s_wifi_started = false;
    if (result == ESP_OK) result = wifi_error;
  }
  strcpy(s_active_ip, "0.0.0.0");
  s_execution_wifi_pause = false;
  return result;
}

esp_err_t typx_wifi_service_configure_station(
    const char *ssid, const char *password) {
  nvs_handle_t handle;
  esp_err_t error;
  if (typx_wifi_service_job_locked()) return ESP_ERR_INVALID_STATE;
  if (!typx_wifi_station_credentials_valid(ssid, password))
    return ESP_ERR_INVALID_ARG;
  ESP_RETURN_ON_ERROR(
      nvs_open("typx_wifi", NVS_READWRITE, &handle), TAG,
      "open station NVS");
  error = nvs_set_str(handle, "sta_ssid", ssid);
  if (error == ESP_OK) error = nvs_set_str(handle, "sta_pass", password);
  if (error == ESP_OK) error = nvs_commit(handle);
  nvs_close(handle);
  if (error != ESP_OK) return error;
  (void)typx_wifi_station_config_set(&s_station, ssid, password);
  return ESP_OK;
}

esp_err_t typx_wifi_service_forget_station(void) {
  nvs_handle_t handle;
  esp_err_t error;
  esp_err_t erase_error;
  bool switch_to_ap;
  if (typx_wifi_service_job_locked()) return ESP_ERR_INVALID_STATE;
  s_post_job_station_reconnect = false;
  ESP_RETURN_ON_ERROR(
      nvs_open("typx_wifi", NVS_READWRITE, &handle), TAG,
      "open station NVS");
  error = nvs_erase_key(handle, "sta_ssid");
  if (error == ESP_ERR_NVS_NOT_FOUND) error = ESP_OK;
  erase_error = nvs_erase_key(handle, "sta_pass");
  if (erase_error == ESP_ERR_NVS_NOT_FOUND) erase_error = ESP_OK;
  if (error == ESP_OK) error = erase_error;
  if (error == ESP_OK) error = nvs_commit(handle);
  nvs_close(handle);
  if (error != ESP_OK) return error;
  switch_to_ap = s_wifi_mode == TYPX_WIFI_STA;
  typx_wifi_station_config_clear(&s_station);
  return switch_to_ap ? typx_wifi_service_start_ap() : ESP_OK;
}

void typx_wifi_service_print_info(void) {
  const char *mode = s_wifi_mode == TYPX_WIFI_STA
      ? "station"
      : (s_wifi_mode == TYPX_WIFI_AP ? "recovery AP" : "stopped");
  ESP_LOGI(TAG, "Mode: %s", mode);
  ESP_LOGI(TAG, "AP SSID: %s", s_ap_ssid);
  ESP_LOGI(TAG, "AP password: %s", s_ap_password);
  ESP_LOGI(
      TAG, "Phone hotspot: %s",
      s_station.configured ? s_station.ssid : "not configured");
  ESP_LOGI(TAG, "IP: %s", s_active_ip);
  ESP_LOGI(TAG, "Wi-Fi: %s", s_wifi_started ? "started" : "stopped");
}

bool typx_wifi_service_job_locked(void) {
  bool locked;
  if (s_mutex == NULL) return false;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  locked = typx_wifi_upload_job_locked(&s_upload);
  xSemaphoreGive(s_mutex);
  return locked;
}
