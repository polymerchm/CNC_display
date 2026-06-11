#ifndef COMPONENTS_RELAY_TASK
#define COMPONENTS_RELAY_TASK

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

void relay_task( void * pvParameters);
extern QueueHandle_t relayQueue;



#endif  /* COMPONENTS_RELAY_TASK */
