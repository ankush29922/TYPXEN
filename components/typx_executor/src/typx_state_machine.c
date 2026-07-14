#include "typx_state_machine.h"

#include <stddef.h>

static bool transition_allowed(typx_state_t from, typx_state_t to) {
  switch (from) {
    case TYPX_STATE_BOOTING:
      return to == TYPX_STATE_WIFI_READY || to == TYPX_STATE_ERROR;
    case TYPX_STATE_WIFI_READY:
      return to == TYPX_STATE_BLUETOOTH_ADVERTISING ||
          to == TYPX_STATE_RECEIVING || to == TYPX_STATE_ERROR;
    case TYPX_STATE_BLUETOOTH_ADVERTISING:
      return to == TYPX_STATE_BLUETOOTH_CONNECTED ||
          to == TYPX_STATE_RECEIVING || to == TYPX_STATE_ERROR;
    case TYPX_STATE_BLUETOOTH_CONNECTED:
      return to == TYPX_STATE_RECEIVING ||
          to == TYPX_STATE_BLUETOOTH_ADVERTISING ||
          to == TYPX_STATE_ERROR;
    case TYPX_STATE_RECEIVING:
      return to == TYPX_STATE_VERIFYING ||
          to == TYPX_STATE_WIFI_READY ||
          to == TYPX_STATE_BLUETOOTH_ADVERTISING ||
          to == TYPX_STATE_BLUETOOTH_CONNECTED ||
          to == TYPX_STATE_ABORTED || to == TYPX_STATE_ERROR;
    case TYPX_STATE_VERIFYING:
      return to == TYPX_STATE_READY || to == TYPX_STATE_ABORTED ||
          to == TYPX_STATE_ERROR;
    case TYPX_STATE_READY:
      return to == TYPX_STATE_COUNTDOWN || to == TYPX_STATE_RECEIVING ||
          to == TYPX_STATE_ABORTED || to == TYPX_STATE_ERROR;
    case TYPX_STATE_COUNTDOWN:
      return to == TYPX_STATE_EXECUTING || to == TYPX_STATE_ABORTED ||
          to == TYPX_STATE_ERROR;
    case TYPX_STATE_EXECUTING:
      return to == TYPX_STATE_COMPLETED || to == TYPX_STATE_ABORTED ||
          to == TYPX_STATE_ERROR;
    case TYPX_STATE_COMPLETED:
    case TYPX_STATE_ABORTED:
      return to == TYPX_STATE_WIFI_READY ||
          to == TYPX_STATE_BLUETOOTH_CONNECTED || to == TYPX_STATE_ERROR;
    case TYPX_STATE_ERROR:
      return to == TYPX_STATE_WIFI_READY ||
          to == TYPX_STATE_BLUETOOTH_ADVERTISING || to == TYPX_STATE_ERROR;
    default:
      return false;
  }
}

void typx_state_machine_boot(typx_state_machine_t *machine) {
  if (machine == NULL) {
    return;
  }
  machine->state = TYPX_STATE_BOOTING;
  machine->bluetooth_connected = false;
  machine->upload_complete = false;
  machine->job_verified = false;
}

void typx_state_machine_set_bluetooth_connected(
    typx_state_machine_t *machine, bool connected) {
  if (machine != NULL) {
    machine->bluetooth_connected = connected;
  }
}

void typx_state_machine_set_upload_complete(
    typx_state_machine_t *machine, bool complete) {
  if (machine != NULL) {
    machine->upload_complete = complete;
    if (!complete) {
      machine->job_verified = false;
    }
  }
}

typx_state_error_t typx_state_machine_transition(
    typx_state_machine_t *machine, typx_state_t next) {
  if (machine == NULL || !transition_allowed(machine->state, next)) {
    return TYPX_STATE_ILLEGAL_TRANSITION;
  }
  if (next == TYPX_STATE_READY) {
    return TYPX_STATE_JOB_NOT_VERIFIED;
  }
  if (next == TYPX_STATE_COUNTDOWN || next == TYPX_STATE_COMPLETED ||
      next == TYPX_STATE_ABORTED || next == TYPX_STATE_ERROR) {
    return TYPX_STATE_ILLEGAL_TRANSITION;
  }
  machine->state = next;
  return TYPX_STATE_OK;
}

typx_state_error_t typx_state_machine_mark_ready(
    typx_state_machine_t *machine, bool verification_succeeded) {
  if (machine == NULL || machine->state != TYPX_STATE_VERIFYING) {
    return TYPX_STATE_ILLEGAL_TRANSITION;
  }
  if (!machine->upload_complete) {
    return TYPX_STATE_JOB_INCOMPLETE;
  }
  if (!verification_succeeded) {
    machine->job_verified = false;
    return TYPX_STATE_JOB_NOT_VERIFIED;
  }
  machine->job_verified = true;
  machine->state = TYPX_STATE_READY;
  return TYPX_STATE_OK;
}

typx_state_error_t typx_state_machine_start(typx_state_machine_t *machine) {
  if (machine == NULL || machine->state != TYPX_STATE_READY) {
    return TYPX_STATE_ILLEGAL_TRANSITION;
  }
  if (!machine->job_verified) {
    return TYPX_STATE_JOB_NOT_VERIFIED;
  }
  if (!machine->bluetooth_connected) {
    return TYPX_STATE_HID_NOT_CONNECTED;
  }
  machine->state = TYPX_STATE_COUNTDOWN;
  return TYPX_STATE_OK;
}

typx_state_error_t typx_state_machine_finish(
    typx_state_machine_t *machine,
    typx_state_t terminal_state,
    typx_state_release_all_fn release_all,
    void *release_context) {
  bool released;
  if (machine == NULL || release_all == NULL ||
      (terminal_state != TYPX_STATE_COMPLETED &&
       terminal_state != TYPX_STATE_ABORTED &&
       terminal_state != TYPX_STATE_ERROR) ||
      !transition_allowed(machine->state, terminal_state)) {
    return TYPX_STATE_ILLEGAL_TRANSITION;
  }
  released = release_all(release_context);
  machine->state = released ? terminal_state : TYPX_STATE_ERROR;
  machine->job_verified = false;
  return released ? TYPX_STATE_OK : TYPX_STATE_RELEASE_FAILED;
}

const char *typx_state_error_code(typx_state_error_t error) {
  static const char *codes[] = {
      "OK",
      "ILLEGAL_TRANSITION",
      "JOB_INCOMPLETE",
      "JOB_NOT_VERIFIED",
      "HID_NOT_CONNECTED",
      "RELEASE_FAILED"};
  size_t count = sizeof(codes) / sizeof(codes[0]);
  return (unsigned)error < count ? codes[error] : "UNKNOWN_STATE_ERROR";
}
