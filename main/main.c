#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/i2c_master.h"
#include "esp_lcd_io_spi.h"

#include "ADC_task.h"
#include "spindle_active_task.h"
#include "relay_task.h"

/*

        Bodgery CNC-Router Heads-Up Display

*/
#define TRUE 1
#define FALSE 0

#define SDA GPIO_NUM_21
#define SCL GPIO_NUM_22

#define T1_PIN GPIO_NUM_5
#define ADC_ADDR  (0x48)
#define RELAY_ADDR  (0x20)


bool spindle_on = FALSE;
int speed = 0;

/* queues */ 

#define ADC_QUEUE_LEN 10
QueueHandle_t adcQueue;

#define RELAY_QUEUE_LEN 10
QueueHandle_t relayQueue;

#define SPINDLE_ACTIVE_QUEUE_LEN 10
QueueHandle_t spindleActiveQueue;

/* IIC data */

i2c_master_bus_config_t i2c_bus_config = {
    .i2c_port = I2C_NUM_0,
    .sda_io_num =SDA,
    .scl_io_num = SCL,
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .glitch_ignore_cnt = 7,
};

i2c_master_bus_handle_t bus_handle;

i2c_device_config_t relay_i2C_cfg = { // relay board
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = ADC_ADDR, 
    .scl_speed_hz = 400000, // 400kHz
};
i2c_master_dev_handle_t relay_i2C_handle;

i2c_device_config_t adc_i2C_cfg = { // relay board
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = RELAY_ADDR, 
    .scl_speed_hz = 400000, // 400kHz
};
i2c_master_dev_handle_t adc_i2C_handle;


/* display related constants

ILI9341V Driver
240x320, SPI Serial interface

ILI9341 pin --> ESP32 GPIO pin of choice
MOSI        --> GPIO13
MISO        --> n/c
CLK         --> GPIO14
CS          --> GPIO15
DC          --> GPIO2
Reset       --> pull up to 3.3V
GND         --> ground



*/

void display_task(void *pvParameters);




void app_main(void)
{
    static uint8_t ucParameterToPass;
    TaskHandle_t xHandle = NULL;

    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &bus_handle));
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &adc_i2C_cfg, &adc_i2C_handle));
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &relay_i2C_cfg, &relay_i2C_handle));


    // initialize the display/spi interfaces
    // initialize the I2C bus
    xTaskCreate(spindle_active_task, "spindle_active", 512, &ucParameterToPass, 3, &xHandle);
    xTaskCreate(ADC_task, "ADC_task", 512, &ucParameterToPass, 3, &xHandle);
    xTaskCreate(display_task, "display_task", 512, &ucParameterToPass, 3, &xHandle);
    xTaskCreate(relay_task, "relay", 512, &ucParameterToPass, 3, &xHandle);
}



void display_task(void *pvParameters)
{
    for (;;)
    {
    }
}


