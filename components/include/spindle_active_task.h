#ifndef MAIN_COMPONENTS_SPINDLE_ACTIVE_TASK
#define MAIN_COMPONENTS_SPINDLE_ACTIVE_TASK

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"


void spindle_active_task(void *pvParameters);
extern QueueHandle_t spindleActiveQueue;


#endif  /* MAIN_COMPONENTS_SPINDLE_ACTIVE_TASK */
