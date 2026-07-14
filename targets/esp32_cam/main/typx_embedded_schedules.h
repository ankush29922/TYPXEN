#ifndef TYPX_EMBEDDED_SCHEDULES_H
#define TYPX_EMBEDDED_SCHEDULES_H

#include "typx_local_schedule.h"

typedef enum {
  TYPX_LOCAL_STANDARD = 0,
  TYPX_LOCAL_PERSONAL
} typx_local_schedule_id_t;

typx_local_schedule_result_t typx_embedded_schedule_run(
    typx_local_schedule_id_t schedule_id);

const char *typx_embedded_schedule_name(
    typx_local_schedule_id_t schedule_id);

#endif
