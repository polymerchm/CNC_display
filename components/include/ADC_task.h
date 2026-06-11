#ifndef COMPONENTS_INCLUDE_ADC_TASK
#define COMPONENTS_INCLUDE_ADC_TASK

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

void ADC_task( void *pvParameters);
extern QueueHandle_t adcQueue;

#endif  /* COMPONENTS_INCLUDE_ADC_TASK */
