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
#include "spindle.h"
#include "UI/ui.h"
#include "formatWithCommas.h"
#include "mcp4725.h"
#include "ads1115.h"


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
#define ADC_ADDR    (0x48)
#define RELAY_ADDR  (0x3F)
#define DAC_ADDR    (0x60)

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
#define re_btn GPIO_NUM_2

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

/* ADC globals handled by ads1115_t structure **/
static ads1115_t ads1115_handle; 

/* dac globals */
static i2c_device_config_t dac_i2C_cfg = {
    // dav board
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = DAC_ADDR,
    .scl_speed_hz = 100000, // 400kHz
};

static i2c_master_dev_handle_t dac;
static i2c_master_dev_handle_t relays;

// esp-idf-encoder

static QueueHandle_t re_event_queue;
static rotary_encoder_handle_t re;

static void encoder_event_handler(const rotary_encoder_event_t *event, void *ctx)
{
    QueueHandle_t queue = (QueueHandle_t)ctx;
    xQueueSendToBack(queue, event, 0);
}

#define RE_EV_QUEUE 5

void re_task(void *arg)
{
    // Create queue for rotary encoder events
    re_event_queue = xQueueCreate(RE_EV_QUEUE, sizeof(rotary_encoder_event_t));

    // Create an encoder
    rotary_encoder_config_t config = ROTARY_ENCODER_DEFAULT_CONFIG();
    config.pin_a = re_channel_a;
    config.pin_b = re_channel_b;
    config.pin_btn = re_btn;
    config.callback = encoder_event_handler;
    config.callback_ctx = re_event_queue;
    ESP_ERROR_CHECK(rotary_encoder_create(&config, &re));

    rotary_encoder_event_t e;
    int32_t val = 0;

    ESP_LOGI(TAG, "Initial value: %" PRIi32, val);
    while (1)
    {
        xQueueReceive(re_event_queue, &e, portMAX_DELAY);

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
}





/*
 *   ======================== APP_MAIN =========================
 */


int last_count = 0;
lv_display_t *disp = NULL;


void update_scr_cb(lv_timer_t *timer)
{
    int temp;
    char buff[10];
    if (lvgl_port_lock(100))
    {
        temp = (rand() % (24000 - 23500 + 1)) + 23500;
        format_with_commas(temp, buff);
        lv_label_set_text(ui_CurrentSpeed,  buff);
    } else {
        ESP_LOGI(TAG, "Cound not get the lock");
    }
}

void app_main(void)
{

    

    /********************************* i2c master *****************************/

    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &bus_handle));
    // initalize relay board
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &relay_i2C_cfg, &relays));
    // ininitailize ADC - calls i2c_master_bus_add_device internally
    ESP_ERROR_CHECK(ads1115_init(&ads1115_handle, &bus_handle, ADC_ADDR, 100000));
    // initialize DAC board 
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dac_i2C_cfg, &dac));

    // pull all relays to open (active low)
    uint8_t relay_buffer = 0xf0;
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
        .rgb_ele_order =    LCD_RGB_ELEMENT_ORDER_RGB,
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

    ui_init();

    lv_scr_load_anim(ui_Primary, LV_SCR_LOAD_ANIM_OVER_BOTTOM, 2000, 0, true);

    char buf[32];
    int tick = 0;
    int last_pulse = 0;




    lv_timer_t * refresh_timer = lv_timer_create(update_scr_cb, 100, NULL);

    ads1115_set_gain(&ads1115_handle, ADS_FSR_4_096V); // +/- 4.096 FS
    ads1115_set_sps(&ads1115_handle, ADS_SPS_128);
    
    xTaskCreate(re_task, TAG, configMINIMAL_STACK_SIZE * 8, NULL, 5, NULL);

    while (1)
    {
        lv_task_handler();
        
        uint16_t raw;
        float voltage;
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    // // // initialize the I2C bus
    // xTaskCreate(ADC_task, "ADC_task", 512, &ucParameterToPass, 3, &xHandle);
    // xTaskCreate(display_task, "display_task", 512, &ucParameterToPass, 3, &xHandle);
    // xTaskCreate(relay_task, "relay", 512, &ucParameterToPass, 3, &xHandle);
}
