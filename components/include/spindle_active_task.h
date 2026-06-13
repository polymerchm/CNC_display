#ifndef MAIN_COMPONENTS_SPINDLE_ACTIVE_TASK
#define MAIN_COMPONENTS_SPINDLE_ACTIVE_TASK

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"


void spindle_active_task(void *pvParameters);



#endif  /* MAIN_COMPONENTS_SPINDLE_ACTIVE_TASK */
