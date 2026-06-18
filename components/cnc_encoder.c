#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "encoder.h"

#include "cnc_encoder.h"

#include "esp_task_wdt.h"

/* Rotory Encoder Section */

#define re_channel_a GPIO_NUM_25
#define re_channel_b GPIO_NUM_26
#define re_button GPIO_NUM_2

#define RE_EVENT_QUEUE_LEN 5

QueueHandle_t re_event_queue;
rotary_encoder_handle_t re;

static const char *TAG = "encoder";

static void encoder_event_handler(const rotary_encoder_event_t *event, void *ctx)
{
    QueueHandle_t queue = (QueueHandle_t)ctx;
    xQueueSendToBack(queue, event, 0);
}

void re_task(void *arg)
{
    // Create queue for rotary encoder events
    re_event_queue = xQueueCreate(RE_EVENT_QUEUE_LEN, sizeof(rotary_encoder_event_t));
    rotary_encoder_config_t re_config = {
        .pin_a = re_channel_a,
        .pin_b = re_channel_b,
        .pin_btn = re_button,
        .btn_pressed_level = 0, // active low
        .enable_internal_pullup = true,
        // .btn_long_press_time_us = 500000,
        .callback = encoder_event_handler,
        .callback_ctx = re_event_queue};

    // Create an encoder

    ESP_ERROR_CHECK(rotary_encoder_create(&re_config, &re));

    rotary_encoder_event_t e;
    int32_t val = 0;

    ESP_LOGI(TAG, "Initial value: %" PRIi32, val);

    TickType_t xTicksToWait = pdMS_TO_TICKS(1000);
    esp_task_wdt_add(NULL);
    while (1)
    {
        if (xQueueReceive(re_event_queue, &e, xTicksToWait) == pdPASS)
        {

            switch (e.type)
            {
            case RE_ET_BTN_PRESSED:
                ESP_LOGI(TAG, "Button pressed");
                break;
            case RE_ET_BTN_RELEASED:
                ESP_LOGI(TAG, "Button released");
                break;
            case RE_ET_BTN_CLICKED:
                ESP_LOGI(TAG, "Button clicked");
                rotary_encoder_enable_acceleration(re, 100);
                ESP_LOGI(TAG, "Acceleration enabled");
                break;
            case RE_ET_BTN_LONG_PRESSED:
                ESP_LOGI(TAG, "Looooong pressed button");
                rotary_encoder_disable_acceleration(re);
                ESP_LOGI(TAG, "Acceleration disabled");
                break;
            case RE_ET_CHANGED:
                val += e.diff;
                ESP_LOGI(TAG, "Value = %" PRIi32, val);
                break;
            default:
                break;
            }
        }
        else
        {
            esp_task_wdt_reset();
        }
    }
}

void init_rotary_encoder(void)
{
    xTaskCreatePinnedToCore(re_task, TAG, configMINIMAL_STACK_SIZE * 8, NULL, 2, NULL, 1);
}