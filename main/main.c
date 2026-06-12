#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/i2c_master.h"


#include "esp_lcd_ili9341.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_dev.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "hal/gpio_types.h"
#include "hal/spi_types.h"



#include "ADC_task.h"
#include "spindle_active_task.h"
#include "relay_task.h"

#include "cnc.c"
#include "logo.c"
static const char *TAG = "CNC";

/*

        Bodgery CNC-Router Heads-Up Display

*/
#define TRUE 1
#define FALSE 0

/* I2C */



// #define T1_PIN GPIO_NUM_5
// int speed = 0;
// bool spindle_on = FALSE;

// /* queues */

// #define ADC_QUEUE_LEN 10
// QueueHandle_t adcQueue;

// #define RELAY_QUEUE_LEN 10
// QueueHandle_t relayQueue;

// #define SPINDLE_ACTIVE_QUEUE_LEN 10
// QueueHandle_t spindleActiveQueue;

/* IIC data */
#define ADC_ADDR (0x48)
#define RELAY_ADDR (0x20)

#define SDA GPIO_NUM_21
#define SCL GPIO_NUM_22

i2c_master_bus_config_t i2c_bus_config = {
    .i2c_port = I2C_NUM_0,
    .sda_io_num = SDA,
    .scl_io_num = SCL,
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .glitch_ignore_cnt = 7,
};
i2c_master_bus_handle_t bus_handle;

i2c_device_config_t relay_i2C_cfg = {
    // relay board
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = ADC_ADDR,
    .scl_speed_hz = 400000, // 400kHz
};
i2c_master_dev_handle_t relay_i2C_handle;

i2c_device_config_t adc_i2C_cfg = {
    // relay board
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = RELAY_ADDR,
    .scl_speed_hz = 400000, // 400kHz
};
i2c_master_dev_handle_t adc_i2C_handle;

/*  display related constants */

#define LCD_HOST SPI3_HOST // VSPI
#define PIN_NUM_SCLK 18
#define PIN_NUM_MOSI 23
#define PIN_NUM_MISO 19
#define PIN_NUM_LCD_CS 5
#define PIN_NUM_BKL 4
#define PIN_NUM_RST 22
#define PIN_NUM_LCD_DC 21

#define LCD_H_RES 240
#define LCD_V_RES 320
#define LCD_PIXEL_CLOCK_HZ 20 * 1000 * 1000
#define LCD_CMD_BITS 8
#define LCD_PARAM_BITS 8

void display_task(void *pvParameters);

spi_bus_config_t buscfg = {
    .sclk_io_num = PIN_NUM_SCLK,
    .mosi_io_num = PIN_NUM_MOSI,
    .miso_io_num = PIN_NUM_MISO,
    .quadwp_io_num = -1,
    .quadhd_io_num = -1,
    .max_transfer_sz = LCD_H_RES * 80 * sizeof(uint16_t),
    // transfer 80 lines of pixels (assume pixel is RGB565) at most in one SPI transaction
};

esp_lcd_panel_io_spi_config_t lcd_io_config = {
    .dc_gpio_num = PIN_NUM_LCD_DC,
    .cs_gpio_num = PIN_NUM_LCD_CS,
    .pclk_hz = LCD_PIXEL_CLOCK_HZ,
    .lcd_cmd_bits = LCD_CMD_BITS,
    .lcd_param_bits = LCD_PARAM_BITS,
    .spi_mode = 0,
    .trans_queue_depth = 10,
};
// Attach the LCD to the SPI buses

esp_lcd_panel_io_handle_t lcd_io_handle = NULL;

esp_lcd_panel_dev_config_t panel_config = {
    .reset_gpio_num = PIN_NUM_RST,
    .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
    .bits_per_pixel = 16,
};
// Create LCD panel handle for ILI9341, with the SPI IO device handle
esp_lcd_panel_handle_t lcd_panel_handle = NULL;

static void display_init(void)
{
    // gpio_set_direction(PIN_NUM_BKL, GPIO_MODE_OUTPUT);
    // gpio_set_level(PIN_NUM_BKL, 1);
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO)); // Enable the DMA feature
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &lcd_io_config, &lcd_io_handle));
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(lcd_io_handle, &panel_config, &lcd_panel_handle));
    esp_lcd_panel_reset(lcd_panel_handle);
    esp_lcd_panel_init(lcd_panel_handle);
    esp_lcd_panel_disp_on_off(lcd_panel_handle, true);

    
}

void app_main(void)
{
    // static uint8_t ucParameterToPass;
    // TaskHandle_t xHandle = NULL;

    // ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &bus_handle));
    // ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &adc_i2C_cfg, &adc_i2C_handle));
    // ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &relay_i2C_cfg, &relay_i2C_handle));

    // initialize the display/spi interfaces
    display_init();
    esp_lcd_panel_swap_xy(lcd_panel_handle,true);
    esp_lcd_panel_draw_bitmap(lcd_panel_handle, 0, 0, 141, 156, &spmt_logo_map);
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    // // initialize the I2C bus
    // xTaskCreate(spindle_active_task, "spindle_active", 512, &ucParameterToPass, 3, &xHandle);
    // xTaskCreate(ADC_task, "ADC_task", 512, &ucParameterToPass, 3, &xHandle);
    // xTaskCreate(display_task, "display_task", 512, &ucParameterToPass, 3, &xHandle);
    // xTaskCreate(relay_task, "relay", 512, &ucParameterToPass, 3, &xHandle);
}

void display_task(void *pvParameters)
{
    for (;;)
    {
    }
}
