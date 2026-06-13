/* 
    whatch state of the spindle and tell the rest of the system
*/

#include <stdio.h>
#include "spindle_active_task.h"
#include <esp_timer.h>
#include "driver/gpio.h"



extern QueueHandle_t spindle_event_queue;
extern double total_spindle_time;
static double time_at_turn_on;

static double time_since_boot(void) {
    int64_t time_us = esp_timer_get_time();
    return (double)time_us / 1000000.0;
}


void spindle_active_task(void *arg) {
    int gpio_num;
    printf("spindle test starting");
    for (;;) {
        // Block until an event is sent from the ISR
        if (xQueueReceive(spindle_event_queue, (void *)&gpio_num, portMAX_DELAY)) {
            // Read the current logic level of the pin to determine state change
            printf("got here");
            int level = gpio_get_level(gpio_num); 
            
            if (level == 1) {
                printf("GPIO %d went HIGH\n", gpio_num);
                // accumulate time
            } else {
                printf("GPIO %d went LOW\n", gpio_num);
                // add this delta to the overall time
            }
        }
    }
}