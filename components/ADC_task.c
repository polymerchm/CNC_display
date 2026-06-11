#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "ADC_task.h"

/*
This routine will put the latest ADC reading in a queue and wait for room

*/

    void ADC_task( void * pvParameters)
    {
        for (;;)
        {
        }
    }