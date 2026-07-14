#ifndef TYPX_STATE_MACHINE_H
#define TYPX_STATE_MACHINE_H

#include <stdbool.h>

typedef enum {
  TYPX_STATE_BOOTING = 0,
  TYPX_STATE_WIFI_READY,
  TYPX_STATE_BLUETOOTH_ADVERTISING,
  TYPX_STATE_BLUETOOTH_CONNECTED,
  TYPX_STATE_RECEIVING,
  TYPX_STATE_VERIFYING,
  TYPX_STATE_READY,
  TYPX_STATE_COUNTDOWN,
  TYPX_STATE_EXECUTING,
  TYPX_STATE_COMPLETED,
  TYPX_STATE_ABORTED,
  TYPX_STATE_ERROR
} typx_state_t;

typedef enum {
  TYPX_STATE_OK = 0,
  TYPX_STATE_ILLEGAL_TRANSITION,
  TYPX_STATE_JOB_INCOMPLETE,
  TYPX_STATE_JOB_NOT_VERIFIED,
  TYPX_STATE_HID_NOT_CONNECTED,
  TYPX_STATE_RELEASE_FAILED
} typx_state_error_t;

typedef bool (*typx_state_release_all_fn)(void *context);

typedef struct {
  typx_state_t state;
  bool bluetooth_connected;
  bool upload_complete;
  bool job_verified;
} typx_state_machine_t;

void typx_state_machine_boot(typx_state_machine_t *machine);
void typx_state_machine_set_bluetooth_connected(
    typx_state_machine_t *machine, bool connected);
void typx_state_machine_set_upload_complete(
    typx_state_machine_t *machine, bool complete);
typx_state_error_t typx_state_machine_transition(
    typx_state_machine_t *machine, typx_state_t next);
typx_state_error_t typx_state_machine_mark_ready(
    typx_state_machine_t *machine, bool verification_succeeded);
typx_state_error_t typx_state_machine_start(typx_state_machine_t *machine);
typx_state_error_t typx_state_machine_finish(
    typx_state_machine_t *machine,
    typx_state_t terminal_state,
    typx_state_release_all_fn release_all,
    void *release_context);

const char *typx_state_error_code(typx_state_error_t error);

#endif
