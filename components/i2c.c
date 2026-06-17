#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "encoder.h" 
#include "i2c.h"


/* IIC data */
#define ADC_ADDR (0x48)
#define RELAY_ADDR (0x20)

#define SDA GPIO_NUM_16
#define SCL GPIO_NUM_17



static i2c_master_bus_config_t i2c_bus_config = {
    .i2c_port = I2C_NUM_0,
    .sda_io_num = SDA,
    .scl_io_num = SCL,
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .glitch_ignore_cnt = 7,
};
static i2c_master_bus_handle_t bus_handle;

static i2c_device_config_t relay_i2C_cfg = {
    // relay board
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = ADC_ADDR,
    .scl_speed_hz = 400000, // 400kHz
};
i2c_master_dev_handle_t relay;

static i2c_device_config_t adc_i2C_cfg = {
    // relay board
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = RELAY_ADDR,
    .scl_speed_hz = 400000, // 400kHz
};
static i2c_master_dev_handle_t adc;

static i2c_device_config_t dac_i2C_cfg = {
    // relay board
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = RELAY_ADDR,
    .scl_speed_hz = 400000, // 400kHz
};
static i2c_master_dev_handle_t dac;

i2c_handles_t i2c_init(void) {

    i2c_handles_t handles ={0};

    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &bus_handle));
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &adc_i2C_cfg, &handles.adc));
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &relay_i2C_cfg, &handles.relays));
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dac_i2C_cfg, &handles.dac));
    return handles;

    
}


