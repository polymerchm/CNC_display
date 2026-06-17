#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "../main/main.h"
#include "spindle.h"
#include "esp_timer.h"

const char * TAG = "spindle";

int spindle_sense_pin  = SPINDLE_PIN; 
QueueHandle_t spindle_event_queue = NULL;


// Debounced Interrupt Service Routine (ISR)
// The ISR Handler
static void IRAM_ATTR gpio_isr_handler(void* arg) {
    static int64_t last_interrupt_time = 0;
    int64_t interrupt_time = esp_timer_get_time();
    
    // If interrupts come faster than 50ms, assume it's a bounce and ignore
    if (interrupt_time - last_interrupt_time > DEBOUNCE_TIME_US) {
        uint32_t gpio_num = (uint32_t) arg;
        xQueueSendFromISR(spindle_event_queue, &gpio_num, NULL);
    }
    last_interrupt_time = interrupt_time;
}


static void gpio_spindle_task(void* arg)
{
    uint32_t io_num;
    for(;;) {
        if(xQueueReceive(spindle_event_queue, &io_num, portMAX_DELAY)) {
            // Read current pin state to verify the logic level
            int pin_level = gpio_get_level(io_num); 
            ESP_LOGI(TAG, "Transition detected on GPIO[%lu]! Current Level: %d", io_num, pin_level);
            // here is where you set the relays!!!
            if (pin_level == 0) {
                // turn off all relays
                // stop counting spindle time
                // add to the total spindle count
            } else {
                // turn on all relays
                // start counting the spindle time
            }
        }
    }
}

void init_spindle_change(void) {
       gpio_config_t gpio_io_conf = {
        .pin_bit_mask = (1ULL << spindle_sense_pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE, // Enable pull-up if using a floating button/sensor
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE // Trigger on both rising and falling edges
    };
    gpio_config(&gpio_io_conf);

    spindle_event_queue = xQueueCreate(10, sizeof(uint32_t));
    xTaskCreatePinnedToCore(gpio_spindle_task, "gpio_worker_task", 2048, NULL, 3, NULL,1);

    // 5. Initialize the per-pin ISR service and link the handler
    gpio_install_isr_service(0);
    gpio_isr_handler_add(spindle_sense_pin, gpio_isr_handler, (void*) spindle_sense_pin);

    ESP_LOGI(TAG, "Transition detection monitoring initialized.");
}