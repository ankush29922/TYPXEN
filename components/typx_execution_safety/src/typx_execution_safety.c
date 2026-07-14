#include "typx_execution_safety.h"

#include <string.h>

bool typx_execution_safety_init(
    typx_execution_safety_t *safety,
    const typx_execution_marker_io_t *marker) {
  bool active = false;
  if (safety == NULL || marker == NULL || marker->read_active == NULL ||
      marker->write_active == NULL) {
    return false;
  }
  memset(safety, 0, sizeof(*safety));
  safety->marker = *marker;
  safety->hid_state = TYPX_HID_SAFETY_DISCONNECTED;
  safety->hid_error = TYPX_HID_SAFETY_ERROR_HID_NOT_CONNECTED;
  if (!safety->marker.read_active(safety->marker.context, &active)) {
    return false;
  }
  if (active) {
    safety->interrupted_reset = true;
    if (!safety->marker.write_active(safety->marker.context, false)) {
      return false;
    }
  }
  return true;
}

bool typx_execution_safety_begin(typx_execution_safety_t *safety) {
  return safety != NULL && safety->marker.write_active != NULL &&
      safety->marker.write_active(safety->marker.context, true);
}

bool typx_execution_safety_finish(typx_execution_safety_t *safety) {
  return safety != NULL && safety->marker.write_active != NULL &&
      safety->marker.write_active(safety->marker.context, false);
}

bool typx_execution_safety_interrupted(
    const typx_execution_safety_t *safety) {
  return safety != NULL && safety->interrupted_reset;
}

uint32_t typx_execution_safety_connected(
    typx_execution_safety_t *safety) {
  if (safety == NULL) return 0u;
  ++safety->connection_generation;
  if (safety->connection_generation == 0u) {
    ++safety->connection_generation;
  }
  safety->connected = true;
  safety->authenticated = false;
  safety->release_attempts = 0u;
  safety->hid_state = TYPX_HID_SAFETY_WAITING_FOR_AUTH;
  safety->hid_error = TYPX_HID_SAFETY_ERROR_AUTH_PENDING;
  return safety->connection_generation;
}

void typx_execution_safety_disconnected(
    typx_execution_safety_t *safety) {
  if (safety == NULL) return;
  ++safety->connection_generation;
  if (safety->connection_generation == 0u) {
    ++safety->connection_generation;
  }
  safety->connected = false;
  safety->authenticated = false;
  safety->release_attempts = 0u;
  safety->hid_state = TYPX_HID_SAFETY_DISCONNECTED;
  safety->hid_error = TYPX_HID_SAFETY_ERROR_HID_NOT_CONNECTED;
}

void typx_execution_safety_authenticated(
    typx_execution_safety_t *safety, bool success) {
  if (safety == NULL || !safety->connected) return;
  safety->authenticated = success;
  safety->release_attempts = 0u;
  safety->hid_state = success
      ? TYPX_HID_SAFETY_WAITING_FOR_REPORT_PATH
      : TYPX_HID_SAFETY_FAILED_RETRYABLE;
  safety->hid_error = success
      ? TYPX_HID_SAFETY_ERROR_REPORT_PATH_PENDING
      : TYPX_HID_SAFETY_ERROR_AUTH_FAILED;
}

bool typx_execution_safety_begin_release(
    typx_execution_safety_t *safety, uint32_t generation) {
  if (safety == NULL || !safety->connected || !safety->authenticated ||
      generation == 0u || generation != safety->connection_generation ||
      safety->release_attempts >= TYPX_EXECUTION_RELEASE_MAX_ATTEMPTS ||
      (safety->hid_state != TYPX_HID_SAFETY_WAITING_FOR_REPORT_PATH &&
       safety->hid_state != TYPX_HID_SAFETY_FAILED_RETRYABLE)) {
    return false;
  }
  ++safety->release_attempts;
  safety->hid_state = TYPX_HID_SAFETY_RELEASING;
  return true;
}

bool typx_execution_safety_complete_release(
    typx_execution_safety_t *safety,
    uint32_t generation,
    bool keyboard_released,
    bool consumer_released) {
  if (safety == NULL || generation == 0u ||
      generation != safety->connection_generation || !safety->connected ||
      !safety->authenticated ||
      safety->hid_state != TYPX_HID_SAFETY_RELEASING) {
    return false;
  }
  if (keyboard_released && consumer_released) {
    safety->hid_state = TYPX_HID_SAFETY_CONFIRMED;
    safety->hid_error = TYPX_HID_SAFETY_ERROR_NONE;
    return true;
  }
  safety->hid_state = TYPX_HID_SAFETY_FAILED_RETRYABLE;
  safety->hid_error = !keyboard_released && !consumer_released
      ? TYPX_HID_SAFETY_ERROR_REPORTS_FAILED
      : (!keyboard_released
          ? TYPX_HID_SAFETY_ERROR_KEYBOARD_REPORT_FAILED
          : TYPX_HID_SAFETY_ERROR_CONSUMER_REPORT_FAILED);
  return false;
}

bool typx_execution_safety_request_retry(
    typx_execution_safety_t *safety) {
  if (safety == NULL || !safety->connected || !safety->authenticated ||
      safety->hid_state == TYPX_HID_SAFETY_RELEASING) {
    return false;
  }
  if (safety->hid_state == TYPX_HID_SAFETY_CONFIRMED) return true;
  safety->release_attempts = 0u;
  safety->hid_state = TYPX_HID_SAFETY_WAITING_FOR_REPORT_PATH;
  safety->hid_error = TYPX_HID_SAFETY_ERROR_REPORT_PATH_PENDING;
  return true;
}

void typx_execution_safety_snapshot(
    const typx_execution_safety_t *safety,
    typx_hid_safety_snapshot_t *snapshot) {
  if (snapshot == NULL) return;
  memset(snapshot, 0, sizeof(*snapshot));
  if (safety == NULL) {
    snapshot->state = TYPX_HID_SAFETY_DISCONNECTED;
    snapshot->error = TYPX_HID_SAFETY_ERROR_HID_NOT_CONNECTED;
    return;
  }
  snapshot->connected = safety->connected;
  snapshot->authenticated = safety->authenticated;
  snapshot->ready = safety->connected && safety->authenticated &&
      safety->hid_state == TYPX_HID_SAFETY_CONFIRMED;
  snapshot->state = safety->hid_state;
  snapshot->error = safety->hid_error;
  snapshot->attempts = safety->release_attempts;
  snapshot->generation = safety->connection_generation;
}

const char *typx_hid_safety_state_code(typx_hid_safety_state_t state) {
  static const char *codes[] = {
    "DISCONNECTED", "WAITING_FOR_AUTH", "WAITING_FOR_REPORT_PATH",
    "RELEASING", "CONFIRMED", "FAILED_RETRYABLE"
  };
  size_t count = sizeof(codes) / sizeof(codes[0]);
  return (unsigned)state < count ? codes[state] : "UNKNOWN";
}

const char *typx_hid_safety_error_code(typx_hid_safety_error_t error) {
  static const char *codes[] = {
    "OK", "HID_NOT_CONNECTED", "AUTH_PENDING", "AUTH_FAILED",
    "REPORT_PATH_PENDING", "KEYBOARD_REPORT_FAILED",
    "CONSUMER_REPORT_FAILED", "REPORTS_FAILED"
  };
  size_t count = sizeof(codes) / sizeof(codes[0]);
  return (unsigned)error < count ? codes[error] : "UNKNOWN_HID_SAFETY_ERROR";
}
