#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "driver/gpio.h"
#include "hal/gpio_types.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/i2c_master.h"
#include "encoder.h"

#include "driver/spi_master.h"
#include "hal/spi_types.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
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

// Pin map matches diagram.json
#define PIN_MOSI 23
#define PIN_SCK 18
#define PIN_CS 5
#define PIN_DC 21
#define PIN_RST 22

#define LCD_HOST SPI2_HOST
#define LCD_PCLK_HZ (40 * 1000 * 1000)
// Native portrait orientation of the ILI9341 panel.
#define LCD_WIDTH 240
#define LCD_HEIGHT 320

/* lvgl */

// LVGL library is not thread-safe, this will call LVGL APIs from different tasks, so use a mutex to protect it
// static _lock_t lvgl_api_lock;
lv_obj_t *tstatus = NULL;

const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();

char buffer_spindle[40] = "";
char buffer_program[40] = "";
char buffer_actual[40] = "";

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
static lv_obj_t *status = NULL;
static lv_obj_t *elapsed_time = NULL;
static lv_obj_t *primary_screen = NULL;
static lv_obj_t *splash = NULL;

static void splash_timer_cb(lv_timer_t *timer)
{
    lv_timer_del(timer);
    vTaskDelay(2000 / portTICK_PERIOD_MS);

    // load_new_screen_with_fade(scr);
    lv_scr_load(primary_screen);

    lv_obj_del(splash);
}

static void build_splash()
{
    splash = lv_obj_create(NULL);
    // 2. Create the image widget, setting the active screen as the parent
    lv_obj_set_style_bg_color(splash, lv_color_hex(0xFF0000), LV_PART_MAIN);

    lv_timer_create(splash_timer_cb, 3000, NULL);
}

static void build_ui()
{
    primary_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(primary_screen, lv_color_hex(0x06102A), LV_PART_MAIN);

    int offset = 20;
    int delta = 40;

    lv_obj_t *title = lv_label_create(primary_screen);
    lv_label_set_text(title, "TechnoCNC Speed Control");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFC83D), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, offset);
    offset += delta;

    lv_obj_t *actual_speed = lv_label_create(primary_screen);
    lv_label_set_text(actual_speed, "Current Speed= ******");
    lv_obj_set_style_text_font(actual_speed, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(actual_speed, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(actual_speed, LV_ALIGN_TOP_MID, 0, offset);
    offset += delta;

    lv_obj_t *program_speed = lv_label_create(primary_screen);
    lv_label_set_text(program_speed, "Program Speed= ******");
    lv_obj_set_style_text_font(program_speed, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(program_speed, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(program_speed, LV_ALIGN_TOP_MID, 0, offset);
    offset += delta;

    elapsed_time = lv_label_create(primary_screen);
    lv_label_set_text(elapsed_time, "Elapsed Time= ******");
    lv_obj_set_style_text_font(elapsed_time, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(elapsed_time, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(elapsed_time, LV_ALIGN_TOP_MID, 0, offset);
    offset += delta;

    status = lv_label_create(primary_screen);
    lv_label_set_text(status, "RUNNING");
    lv_obj_set_style_text_font(status, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(status, lv_color_hex(0xFF0000), 0);
    lv_obj_align(status, LV_ALIGN_BOTTOM_MID, 0, -10);
}
/*
 *   ======================== APP_MAIN =========================
 */

int monitor = 0;
int last_count = 0;
lv_display_t *disp = NULL;

void update_scr_cb(lv_timer_t *timer)
{
    char *buffer = ((monitor % 2)) == 0 ? "ON" : "OFF";
    if (lvgl_port_lock(100))
    {
        lv_obj_set_style_text_color(status,
                (monitor % 2 == 0 ? lv_color_hex(0xFF0000) : lv_color_hex(0x0000FF)), 0);
        lv_label_set_text_fmt(elapsed_time, "Pulse Count %d", pulse_count);
        lv_label_set_text(status, buffer);
    } else {
        ESP_LOGI(TAG, "Cound not get the lock");
    }
}

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

    // // init_spindle_change();

    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &bus_handle));
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &relay_i2C_cfg, &relays));

    ESP_ERROR_CHECK(i2c_master_transmit(relays, &relay_buffer, sizeof(relay_buffer), -1));

    /**************************** LCD ************************ */

    ESP_LOGI(TAG, "Hello ESP32-S31");
    ESP_LOGI(TAG, "Initialising SPI bus on host %d (MOSI=%d, SCK=%d)", LCD_HOST, PIN_MOSI, PIN_SCK);

    spi_bus_config_t buscfg = ILI9341_PANEL_BUS_SPI_CONFIG(PIN_SCK, PIN_MOSI,
                                                           LCD_HEIGHT * 40 * sizeof(uint16_t));
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "Installing panel IO (CS=%d, DC=%d, pclk=%d Hz)", PIN_CS, PIN_DC, LCD_PCLK_HZ);
    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t io_config = ILI9341_PANEL_IO_SPI_CONFIG(PIN_CS, PIN_DC, NULL, NULL);
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io));

    ESP_LOGI(TAG, "Installing ILI9341 panel (RST=%d)", PIN_RST);
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(io, &panel_config, &panel));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

    ESP_LOGI(TAG, "Starting LVGL port...");
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io,
        .panel_handle = panel,
        .buffer_size = LCD_WIDTH * 40,
        .double_buffer = true,
        .hres = LCD_HEIGHT,
        .vres = LCD_WIDTH,
        .monochrome = false,
        .rotation = {
            .swap_xy = true,
            .mirror_x = true,
            .mirror_y = true,
        },
        .flags = {
            .buff_dma = true,
        },
    };
    disp = lvgl_port_add_disp(&disp_cfg);

    char buf[32];
    int tick = 0;

    // esp_lcd_panel_draw_bitmap(panel, 0, 0, (int)router.header.w, (int)router.header.h, &router_map);
    lvgl_port_lock(0);
    build_ui();
    build_splash();
    lv_scr_load(splash);
    lvgl_port_unlock();

    // // Create an LVGL timer to wait 3 seconds (3000 ms) before loading the main screen
    lv_timer_t * refresh_timer = lv_timer_create(update_scr_cb, 30, NULL);

    while (1)
    {
        lv_task_handler();
        if (xQueueReceive(queue, &event_count, pdMS_TO_TICKS(1000)))
        {
            ESP_LOGI(TAG, "Watch point event, count: %d", event_count);
        }
        else
        {
            ESP_ERROR_CHECK(pcnt_unit_get_count(pcnt_unit, &pulse_count));
            if (pulse_count != last_count)
            {
                ESP_LOGI(TAG, "Pulse count: %d", pulse_count);
                last_count = pulse_count;
            }
        }
        monitor++;
        

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
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    // // // initialize the I2C bus
    // xTaskCreate(ADC_task, "ADC_task", 512, &ucParameterToPass, 3, &xHandle);
    // xTaskCreate(display_task, "display_task", 512, &ucParameterToPass, 3, &xHandle);
    // xTaskCreate(relay_task, "relay", 512, &ucParameterToPass, 3, &xHandle);
}
