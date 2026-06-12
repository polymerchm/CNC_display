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
#include "lvgl.h"
#include <sys/lock.h>
#include <sys/param.h>
#include <esp_timer.h>



#include "ADC_task.h"
#include "spindle_active_task.h"
#include "relay_task.h"
#include "router.c"

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

/**** LVGL START *****/
#define LVGL_TICK_PERIOD_MS 2

// LVGL library is not thread-safe, this will call LVGL APIs from different tasks, so use a mutex to protect it
static _lock_t lvgl_api_lock;

void my_ui(void)
{
	    /*Change the active screen's background color*/
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x003a57), LV_PART_MAIN);

    /*Create a white label, set its text and align it to the center*/
    lv_obj_t * label = lv_label_create(lv_screen_active());
    lv_label_set_text(label, "Hello world");
    lv_obj_set_style_text_color(lv_screen_active(), lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
}

static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    lv_display_t *disp = (lv_display_t *)user_ctx;
    lv_display_flush_ready(disp);
    return false;
}

/* Rotate display and touch, when rotated screen in LVGL. Called when driver parameters are updated. */
static void lvgl_port_update_callback(lv_display_t *disp)
{
    esp_lcd_panel_handle_t panel_handle = lv_display_get_user_data(disp);
    lv_display_rotation_t rotation = lv_display_get_rotation(disp);

    switch (rotation) {
    case LV_DISPLAY_ROTATION_0:
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, false);
        esp_lcd_panel_mirror(panel_handle, true, false);
        break;
    case LV_DISPLAY_ROTATION_90:
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, true);
        esp_lcd_panel_mirror(panel_handle, true, true);
        break;
    case LV_DISPLAY_ROTATION_180:
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, false);
        esp_lcd_panel_mirror(panel_handle, false, true);
        break;
    case LV_DISPLAY_ROTATION_270:
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, true);
        esp_lcd_panel_mirror(panel_handle, false, false);
        break;
    }
}

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    lvgl_port_update_callback(disp);
    esp_lcd_panel_handle_t panel_handle = lv_display_get_user_data(disp);
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;
    // because SPI LCD is big-endian, we need to swap the RGB bytes order
    lv_draw_sw_rgb565_swap(px_map, (offsetx2 + 1 - offsetx1) * (offsety2 + 1 - offsety1));
    // copy a buffer's content to a specific area of the display
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, px_map);
}

static void lvgl_tick(void *arg)
{
    /* Tell LVGL how many milliseconds has elapsed */
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static void lvgl_port_task(void *arg)
{
    uint32_t time_till_next_ms = 0;
    uint32_t time_threshold_ms = 2000 / CONFIG_FREERTOS_HZ;
    while (1) {
        _lock_acquire(&lvgl_api_lock);
        time_till_next_ms = lv_timer_handler();
        _lock_release(&lvgl_api_lock);
        // in case of triggering a task watch dog time out
        time_till_next_ms = MAX(time_till_next_ms, time_threshold_ms);
        vTaskDelay(time_till_next_ms);
    }
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
    esp_lcd_panel_draw_bitmap(lcd_panel_handle, 0, 0, 240, 240, &router_map);
    vTaskDelay(2000/portTICK_PERIOD_MS);

    		  /*Initialize LVGL library*/
    lv_init();

    // create a lvgl display
    lv_display_t *display = lv_display_create(LCD_H_RES, LCD_V_RES);

    // alloc draw buffers used by LVGL
    // it's recommended to choose the size of the draw buffer(s) to be at least 1/10 screen sized
    size_t draw_buffer_sz = LCD_H_RES * 20 * sizeof(lv_color16_t);
    void *buf1 = spi_bus_dma_memory_alloc(LCD_HOST, draw_buffer_sz, 0);
    void *buf2 = spi_bus_dma_memory_alloc(LCD_HOST, draw_buffer_sz, 0);

    // initialize LVGL draw buffers
    lv_display_set_buffers(display, buf1, buf2, draw_buffer_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);
    
    // associate the mipi panel handle to the display
    lv_display_set_user_data(display, lcd_panel_handle);
    
    // set color depth
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    
    // set the callback which can copy the rendered image to an area of the display
    lv_display_set_flush_cb(display, lvgl_flush_cb);
    
    lv_display_set_rotation(display, LV_DISPLAY_ROTATION_180);

    /*nstall LVGL tick timer*/
    // Tick interface for LVGL (using esp_timer to generate 2ms periodic event)
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &lvgl_tick,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000));

    /*Register io panel event callback for LVGL flush ready notification*/
    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = notify_lvgl_flush_ready,
    };
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(lcd_io_handle, &cbs, display));
    

	/* Run the UI */
    // Lock the mutex due to the LVGL APIs are not thread-safe
    _lock_acquire(&lvgl_api_lock);
    my_ui();
    _lock_release(&lvgl_api_lock);
    
    /*Create LVGL task*/
    xTaskCreate(lvgl_port_task, "LVGL", 4*4096, NULL, 2, NULL);
	
	while (1)
	{
		vTaskDelay(1000/portTICK_PERIOD_MS);
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
