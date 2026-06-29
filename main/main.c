#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/i2c_master.h"
#include "encoder.h"

#include "esp_lcd_ili9341.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_dev.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "hal/gpio_types.h"
#include "hal/spi_types.h"
// #include "lvgl.h"
#include "esp_lvgl_port.h"
#include <sys/lock.h>
#include <sys/param.h>
#include <esp_timer.h>
#include "esp_task_wdt.h"
#include "driver/pulse_cnt.h"
#include "lvgl.h"

#include "main.h"
#include "cnc_i2c.h"
#include "spindle.h"
#include "cnc_encoder.h"
#include "display.h"

#include "../components/router.c"

static const char *TAG = "CNC";

/*

        Bodgery CNC-Router Heads-Up Display

*/

/* GPIO Inventory
General GPIO
    SPINDLE RELAY 32

I2C (DAC, ADC, Relays)
    SDA 16 (RX on DEVKIT V1)
    SCL 17 (TX on DEVKIT V1)

LCD (SPI)
    PBKL 4
    LCD_CS 5
    CLK  18
    MISO 19
    LCD_DC 21
    RST  22
    MOSI 23

ROTARY ENCODER
    CLK    25
    DT     26
    BTN    2
*/

#define SDA GPIO_NUM_16
#define SCL GPIO_NUM_17

/* IIC devices */
#define ADC_ADDR (0x48)
#define RELAY_ADDR (0x3F)
#define DAC_ADDR (0x5f)


/*==========================================================*/
/*====================== globals ===========================*/
/*==========================================================*/

// int spindle_sense_pin  = GPIO_NUM_32;
// #define DEBOUNCE_TIME_US  50000 // 50 milliseconds
double total_spindle_time; // total of all spindle on time since startup
double time_at_turn_on;    //   time for last segment

int speed = 0;
bool spindle_on = false;

/* encoder */

#define re_channel_a GPIO_NUM_25
#define re_channel_b GPIO_NUM_26

int pulse_count = 0;
int event_count = 0;

/* lcd * */

esp_lcd_panel_io_handle_t io_handle = NULL;

#define DISP_WIDTH 240
#define DISP_HEIGHT 320

/* lvgl */

// LVGL library is not thread-safe, this will call LVGL APIs from different tasks, so use a mutex to protect it
static _lock_t lvgl_api_lock;
lv_obj_t *text_label = NULL;

const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();

/* i2c */

_lock_t i2c_lock;
static i2c_master_bus_config_t i2c_bus_config = {
    .i2c_port = I2C_NUM_0,
    .sda_io_num = SDA,
    .scl_io_num = SCL,
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .glitch_ignore_cnt = 7,
    .flags.enable_internal_pullup = true,
};
i2c_master_bus_handle_t bus_handle;

/* relay globals */

static i2c_device_config_t relay_i2C_cfg = {
    // relay board
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = RELAY_ADDR,
    .scl_speed_hz = 100000, // 100kHz
};

uint8_t relay_buffer = 0xf0;

/* adc globals */
static i2c_device_config_t adc_i2C_cfg = {
    // adc board
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = ADC_ADDR,
    .scl_speed_hz = 100000, // 100kHz
};

/* dac globals */
static i2c_device_config_t dac_i2C_cfg = {
    // dav board
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = DAC_ADDR,
    .scl_speed_hz = 100000, // 400kHz
};

// static i2c_dev_t pcf8574;
static i2c_master_dev_handle_t dac;
static i2c_master_dev_handle_t relays;
static i2c_master_dev_handle_t adc;

/* pcnt */

/*
****************************************************
*
*      CHANNELS MUST BE REVERSED FOR QUADRATURE DETECTION
*
***************************************************
*/

static pcnt_unit_config_t unit_config = {
    .high_limit = 100,
    .low_limit = -100,
};
static pcnt_unit_handle_t pcnt_unit = NULL;

static pcnt_chan_config_t chan_a_config = {
    .edge_gpio_num = re_channel_a,
    .level_gpio_num = re_channel_b,
};
static pcnt_channel_handle_t pcnt_chan_a = NULL;

static pcnt_chan_config_t chan_b_config = {
    .edge_gpio_num = re_channel_b,
    .level_gpio_num = re_channel_a,
};
static pcnt_channel_handle_t pcnt_chan_b = NULL;

pcnt_glitch_filter_config_t filter_config = {
    .max_glitch_ns = 1000,
};

static int watch_points[] = {-10, 0, 10};
/*==========================================================*/
/*====================== functions  ========================*/
/*==========================================================*/

/********************* LVGL *************************/

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

    switch (rotation)
    {
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

static void lvgl_tick(void *arg) // used in lvgl create timer
{
    /* Tell LVGL how many milliseconds has elapsed */
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static void lvgl_port_task(void *arg)
{
    uint32_t time_till_next_ms = 0;
    uint32_t time_threshold_ms = 2000 / CONFIG_FREERTOS_HZ;
    while (1)
    {
        _lock_acquire(&lvgl_api_lock);
        time_till_next_ms = lv_timer_handler();
        _lock_release(&lvgl_api_lock);
        // in case of triggering a task watch dog time out
        time_till_next_ms = MAX(time_till_next_ms, time_threshold_ms);
        vTaskDelay(time_till_next_ms);
    }
}

/**************** encoder *******************/

/* callback on watch counts */
static bool pcnt_on_reach(pcnt_unit_handle_t unit, const pcnt_watch_event_data_t *edata, void *user_ctx)
{
    BaseType_t high_task_wakeup;
    QueueHandle_t queue = (QueueHandle_t)user_ctx;
    // send event data to queue, from this interrupt callback
    xQueueSendFromISR(queue, &(edata->watch_point_value), &high_task_wakeup);
    return (high_task_wakeup == pdTRUE);
}


/* UI */
static lv_obj_t *tick_label;

static void build_ui(lv_display_t *disp) {
    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x06102A), LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Hello ESP32-S31");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFC83D), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 60);

    lv_obj_t *sub = lv_label_create(scr);
    lv_label_set_text(sub, "Welcome to Wokwi!");
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(sub, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 102);

    tick_label = lv_label_create(scr);
    lv_label_set_text(tick_label, "Tick: 0");
    lv_obj_set_style_text_font(tick_label, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(tick_label, lv_color_hex(0x32E6C3), 0);
    lv_obj_align(tick_label, LV_ALIGN_CENTER, 0, 60);
}
/*
 *   ======================== APP_MAIN =========================
 */

void app_main(void)
{

    /* inintialize the pcnt for the encoder */
    ESP_LOGI(TAG, "install pcnt unit");
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, &pcnt_unit));
    ESP_LOGI(TAG, "set glitch filter");
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(pcnt_unit, &filter_config));
    ESP_LOGI(TAG, "install pcnt channels");
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_a_config, &pcnt_chan_a));
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_b_config, &pcnt_chan_b));
    ESP_LOGI(TAG, "set edge and level actions for pcnt channels");
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan_a, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan_b, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan_b, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

    ESP_LOGI(TAG, "add watch points and register callbacks");

    for (size_t i = 0; i < sizeof(watch_points) / sizeof(watch_points[0]); i++)
    {
        ESP_ERROR_CHECK(pcnt_unit_add_watch_point(pcnt_unit, watch_points[i]));
    }
    pcnt_event_callbacks_t cbs = {
        .on_reach = pcnt_on_reach,
    };
    QueueHandle_t queue = xQueueCreate(10, sizeof(int));
    ESP_ERROR_CHECK(pcnt_unit_register_event_callbacks(pcnt_unit, &cbs, queue));
    ESP_LOGI(TAG, "enable pcnt unit");
    ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_unit));
    ESP_LOGI(TAG, "clear pcnt unit");
    ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));
    ESP_LOGI(TAG, "start pcnt unit");
    ESP_ERROR_CHECK(pcnt_unit_start(pcnt_unit));

    // init_spindle_change();

    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &bus_handle));
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &relay_i2C_cfg, &relays));

    ESP_ERROR_CHECK(i2c_master_transmit(relays, &relay_buffer, sizeof(relay_buffer), -1));

    // /**************************** LCD ************************ */

    lcd_user_data_t user_data= init_lcd_display();
    esp_lcd_panel_io_handle_t io = user_data.io;
    esp_lcd_panel_handle_t panel = user_data.panel;
    esp_lcd_panel_draw_bitmap(panel, 0, 0, 240, 320, &router_map);
    vTaskDelay(5000 / portTICK_PERIOD_MS);

    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    esp_err_t err = lvgl_port_init(&lvgl_cfg);

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io,
        .panel_handle = panel,
        .buffer_size = DISP_WIDTH * 40,
        .double_buffer = true,
        .hres = DISP_WIDTH,
        .vres = DISP_HEIGHT,
        .monochrome = false,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_dma = true,
            .swap_bytes = true,
        },
    };
    lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);

    lvgl_port_lock(0);
    build_ui(disp);
    lvgl_port_unlock();

    int last_count = 0;
    for (;;)
    {
        if (xQueueReceive(queue, &event_count, pdMS_TO_TICKS(1000)))
        {
            ESP_LOGI(TAG, "Watch point event, count: %d", event_count);
        }
        else
        {
            ESP_ERROR_CHECK(pcnt_unit_get_count(pcnt_unit, &pulse_count));
            if (pulse_count != last_count){
            ESP_LOGI(TAG, "Pulse count: %d", pulse_count);
            last_count = pulse_count;
            }
        }
        // snprintf(buf, sizeof(buf), "Tick: %d", tick);
        // ESP_LOGI(TAG, "%s", buf);
        // if (lvgl_port_lock(100)) {
        //     lv_label_set_text(tick_label, buf);
        //     lvgl_port_unlock();
        // }
        // relay_buffer = 0x10;
        // relay_buffer = ~relay_buffer;
        // ESP_ERROR_CHECK(i2c_master_transmit(relays, &relay_buffer, 1, -1));
        // vTaskDelay(1000/portTICK_PERIOD_MS);
        // relay_buffer = 0x20;
        // relay_buffer = ~relay_buffer;
        // ESP_ERROR_CHECK(i2c_master_transmit(relays, &relay_buffer, 1, -1));
        // vTaskDelay(1000/portTICK_PERIOD_MS);
        // relay_buffer = 0x40;
        // relay_buffer = ~relay_buffer;
        // ESP_ERROR_CHECK(i2c_master_transmit(relays, &relay_buffer, 1, -1));
        // vTaskDelay(1000/portTICK_PERIOD_MS);
        // relay_buffer = 0x80;
        // relay_buffer = ~relay_buffer;
        // ESP_ERROR_CHECK(i2c_master_transmit(relays, &relay_buffer, 1, -1));
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
    // // // initialize the I2C bus
    // xTaskCreate(ADC_task, "ADC_task", 512, &ucParameterToPass, 3, &xHandle);
    // xTaskCreate(display_task, "display_task", 512, &ucParameterToPass, 3, &xHandle);
    // xTaskCreate(relay_task, "relay", 512, &ucParameterToPass, 3, &xHandle);
}
