#include <stdio.h>
#include <sys/lock.h>
#include <sys/param.h>
#include <sys/time.h>
#include <esp_timer.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "driver/gptimer.h"

#include "hal/gpio_types.h"
#include "hal/spi_types.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"

#include "esp_lvgl_port.h"
#include "lvgl.h"

#include "UI/ui.h"
#include "mcp4725.h"
#include "ads1115.h"
#include "encoder.h"
#include "iot_button.h"
#include "button_gpio.h"

/* local helpers*/

#include "colorShifters.h"
#include "formatWithCommas.h"

#include "main.h"

#ifndef ARRAY
#define ARRAY_LENGTH(x) (sizeof(x) / sizeof((x)[0]))
#endif
#ifndef MIN
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef MAX
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

#define RPM_MIN 4000
#define RPM_MAX 24000
#define FM1_MIN 0.0 // analog input from VFD
#define FM1_MAX 3.0
#define VF1_MIN 0 // analog output to VFD, frequencey control
#define VF1_MAX 3.0

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

// #define DEBOUNCE_TIME_US  50000 // 50 milliseconds
double total_spindle_time; // total of all spindle on time since startup
double time_at_turn_on;    //   time for last segment

int speed = 0;
bool spindle_on = false;
long elasped_time = 0;


/* encoder */

#define re_channel_a GPIO_NUM_25
#define re_channel_b GPIO_NUM_26
#define re_btn GPIO_NUM_2


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
uint16_t speed_increments[] = {1000, 500, 50, 25};
uint8_t speed_increment_pointer = 0;
uint16_t program_spindle_speed = 14000;
#define DEFAULT_SPEED 14000




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
    speed_increment_pointer = 0;


    ESP_LOGI(TAG, "Initial value: %" PRIi32, val);
    while (1)
    {
        xQueueReceive(re_event_queue, &e, portMAX_DELAY);

        switch (e.type)
        {
            case RE_ET_BTN_PRESSED:
                ESP_LOGI(TAG, "Button pressed");
                speed_increment_pointer++;
                if (speed_increment_pointer > ARRAY_LENGTH(speed_increments) - 1){
                    speed_increment_pointer = 0;
                }
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
                speed_increment_pointer = 0;
                program_spindle_speed = (program_spindle_speed/1000)*1000;
                ESP_LOGI(TAG, "Acceleration disabled");
                break;
            case RE_ET_CHANGED:
                ESP_LOGI(TAG, "Value = %" PRIi32, val);
                if (e.diff > 0) {
                    program_spindle_speed = MIN(
                        (program_spindle_speed + speed_increments[speed_increment_pointer]),
                        RPM_MAX); 
                    ESP_LOGI(TAG, "new speed = %" PRIi16, program_spindle_speed);
                } else if (e.diff < 0) {
                    program_spindle_speed = MAX((program_spindle_speed - speed_increments[speed_increment_pointer]),
                        RPM_MIN); 
                    ESP_LOGI(TAG, "new speed = %" PRIi16, program_spindle_speed);
                }
                break;
            default:
                break;
        }
    }
}

/************ external relay closures *****************/

#define BUTTON_IO_NUM GPIO_NUM_32
#define BUTTON_ACTIVE_LEVEL 0

/***** spindle on relay **********/

bool spindle_relay_state = false; // open
gptimer_handle_t gptimer = NULL;
gptimer_config_t timer_config = {
    .clk_src = GPTIMER_CLK_SRC_DEFAULT, // Select the default clock source
    .direction = GPTIMER_COUNT_UP,      // Counting direction is up
    .resolution_hz = 1 * 1000 * 1000,   // Resolution is 1 MHz, i.e., 1 tick equals 1 microsecond
};
// Create a timer instance

char elapsed_time_string[40] = "0:00:00";
uint64_t raw_count;
uint32_t timer_resolution;




void update_elapsed_time() {
    uint32_t resolution;
    gptimer_get_raw_count(gptimer, &raw_count); // in ticks
    long long total_seconds = raw_count/timer_resolution; // time in seconds
    long long hours = total_seconds / 3600;
    long long minutes = (total_seconds % 3600) / 60;
    long long seconds = (total_seconds % 60); 
    snprintf(elapsed_time_string, sizeof(elapsed_time_string), "%1lld:%02lld:%02lld" , hours, minutes,seconds); 
    

}




static void spindle_relay_close_event(void *arg, void*data) {
    ESP_LOGI(TAG,"relay closed");
    spindle_relay_state = true;
       // Start the timer
    ESP_ERROR_CHECK(gptimer_start(gptimer));

}

static void spindle_relay_open_event(void *arg, void*data) {
    ESP_LOGI(TAG,"relay opened");
    spindle_relay_state = false;
       // Start the timer
    ESP_ERROR_CHECK(gptimer_stop(gptimer));
}



/*
 *   ======================== APP_MAIN =========================
 */


int last_count = 0;
lv_display_t *disp = NULL;

/*
    signal from VFD proportional to the current speed
    input in range 0-3v
    voltage2Speed 0v = RPM_MIN rpm, VMAX = RPM_MAX rpm
*/
float input_FM1;
float voltage2speed;


/* signal to the VFD for programming */
float output_VF1;
int program_speed = 14000;
int run_speed = 0;
int frequency;





int blink_counter = 0;
/********* ui updating ****************/
/* N.B.  as a Lvgl timer callback, no lvgl_lock required */
void update_scr_cb(lv_timer_t *timer)
{
    char buff[10];
    if (lvgl_port_lock(100)) {
    /**** run status ***/
    lv_color_t red = lv_color_hex(rgb2rbg(0xff0000));
    lv_color_t green = lv_color_hex(rgb2rbg(0x00ff00));
    lv_color_t orange = lv_color_hex(rgb2rbg(0xff00d5));

    if (blink_counter == 5)
    {
        blink_counter = 0;
        if (lv_obj_has_flag(ui_Status, LV_OBJ_FLAG_HIDDEN))
        {
            lv_obj_clear_flag(ui_Status, LV_OBJ_FLAG_HIDDEN);
        }
        else if (spindle_relay_state)
        {
            lv_obj_add_flag(ui_Status, LV_OBJ_FLAG_HIDDEN);
        }
    }
    else
    {
        blink_counter++;
    }
    lv_color_t status_color = (spindle_relay_state ? red : green);

  /**speed updates */
    format_with_commas(run_speed, buff);
    lv_label_set_text(ui_CurrentSpeed, buff);
    format_with_commas(program_spindle_speed, buff);
    lv_label_set_text(ui_ProgramSpeed, buff);
    if (program_spindle_speed == RPM_MAX || program_spindle_speed == RPM_MIN) {
        lv_obj_set_style_text_color(ui_ProgramSpeed, orange,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    } else {
        lv_obj_set_style_text_color(ui_ProgramSpeed, lv_color_white(),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    frequency = program_spindle_speed/30;
    format_with_commas(frequency, buff);
    lv_label_set_text(ui_Frequency, buff);

    /* elapsed time */

    update_elapsed_time();
    lv_label_set_text(ui_Elapsed, elapsed_time_string);


    /** STATUS  UPDATE */
    lv_label_set_text(ui_Status, spindle_relay_state ? "ON" : "OFF");
    lv_obj_set_style_text_color(ui_Status, status_color,
                                LV_PART_MAIN | LV_STATE_DEFAULT);

    // /** speed increment update  */
    lv_label_set_text_fmt(ui_Delta, "%d", speed_increments[speed_increment_pointer]);
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


    /************** external relay******************* */

    const button_config_t spindle_relay_cfg = {
        .long_press_time = 500, // in ms
        .short_press_time = 200
    };
    
    const button_gpio_config_t spindle_relay_gpio_cfg = {
        .gpio_num = BUTTON_IO_NUM,
        .active_level = BUTTON_ACTIVE_LEVEL,
        .disable_pull = false,
    };

    button_handle_t spindle_relay;

    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));
    // Enable the timer
    ESP_ERROR_CHECK(gptimer_enable(gptimer));
    ESP_ERROR_CHECK(gptimer_get_resolution(gptimer, &timer_resolution));
 

    // Button handle

    esp_err_t ret = iot_button_new_gpio_device(&spindle_relay_cfg, &spindle_relay_gpio_cfg, &spindle_relay);

    ret = iot_button_register_cb(spindle_relay, BUTTON_PRESS_DOWN, NULL, spindle_relay_close_event, NULL);
    ESP_ERROR_CHECK(ret);
    ret = iot_button_register_cb(spindle_relay, BUTTON_PRESS_END, NULL, spindle_relay_open_event, NULL);
    ESP_ERROR_CHECK(ret);

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
