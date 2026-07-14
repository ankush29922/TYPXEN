#include "typx_embedded_schedules.h"

#include "mbedtls/sha256.h"
#include "typx_ble_hid.h"
#include "typx_sha256.h"

extern const uint8_t standard_start[] asm("_binary_standard_bin_start");
extern const uint8_t standard_end[] asm("_binary_standard_bin_end");
extern const uint8_t personal_start[] asm("_binary_personal_bin_start");
extern const uint8_t personal_end[] asm("_binary_personal_bin_end");

static typx_executor_error_t run_verified(
    void *context, const typx_verified_schedule_v1_t *schedule) {
  (void)context;
  return typx_ble_hid_run_schedule_countdown(schedule, 5u);
}

static typx_local_schedule_blob_t schedule_blob(
    typx_local_schedule_id_t schedule_id) {
  typx_local_schedule_blob_t blob;
  if (schedule_id == TYPX_LOCAL_STANDARD) {
    blob.data = standard_start;
    blob.size = (size_t)(standard_end - standard_start);
  } else {
    blob.data = personal_start;
    blob.size = (size_t)(personal_end - personal_start);
  }
  return blob;
}

typx_local_schedule_result_t typx_embedded_schedule_run(
    typx_local_schedule_id_t schedule_id) {
  typx_local_schedule_blob_t blob = schedule_blob(schedule_id);
  typx_protocol_limits_t limits = typx_protocol_v1_esp32_cam_limits();
  mbedtls_sha256_context sha_context;
  typx_sha256_provider_t sha =
      typx_sha256_mbedtls_provider(&sha_context);
  typx_local_schedule_runner_t runner = {NULL, run_verified};
  return typx_local_schedule_verify_and_run(
      &blob, &limits, &sha, &runner);
}

const char *typx_embedded_schedule_name(
    typx_local_schedule_id_t schedule_id) {
  return schedule_id == TYPX_LOCAL_STANDARD ? "standard" : "personal";
}
