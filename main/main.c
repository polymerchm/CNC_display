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
#include "lvgl.h"
#include <sys/lock.h>
#include <sys/param.h>
#include <esp_timer.h>


#include "main.h"
#include "i2c.h"
#include "spindle.h"
#include "cnc_encoder.h"
#include "router.c"

static const char *TAG = "CNC";

/*

        Bodgery CNC-Router Heads-Up Display

*/


/* GPIO Inventory 
General GPIO
    SPINDLE RELAY 32
    
I2C (DAC, ADC, Relays)
    SDA 16
    SCL 17

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

// /*================ GPIO ==============*/ 

// int spindle_sense_pin  = GPIO_NUM_32; 
// #define DEBOUNCE_TIME_US  50000 // 50 milliseconds
double total_spindle_time; // total of all spindle on time since startup
double time_at_turn_on; //   time for last segment

int speed = 0;
bool spindle_on = false;

_lock_t i2c_lock;



// LVGL library is not thread-safe, this will call LVGL APIs from different tasks, so use a mutex to protect it
static _lock_t lvgl_api_lock;
lv_obj_t * text_label = NULL;

// void my_ui(void)
// {
//     lv_obj_t * my_base_screen = lv_obj_create(NULL);
//     lv_scr_load(my_base_screen);


// 	    /*Change the active screen's background color*/
//     lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x003a57), LV_PART_MAIN);
//     /*Create a white label, set its text and align it to the center*/
//     text_label = lv_label_create(lv_screen_active());
//     lv_label_set_text(text_label, "Hello world");
//     lv_obj_set_style_text_color(lv_screen_active(), lv_color_hex(0xffffff), LV_PART_MAIN);
//     lv_obj_align(text_label, LV_ALIGN_CENTER, 0, 0);
// }

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

static void lvgl_tick(void *arg) // used in lvgl create timer
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


// static void init_spindle_change(void) {
//        gpio_config_t gpio_io_conf = {
//         .pin_bit_mask = (1ULL << spindle_sense_pin),
//         .mode = GPIO_MODE_INPUT,
//         .pull_up_en = GPIO_PULLUP_ENABLE, // Enable pull-up if using a floating button/sensor
//         .pull_down_en = GPIO_PULLDOWN_DISABLE,
//         .intr_type = GPIO_INTR_ANYEDGE // Trigger on both rising and falling edges
//     };
//     gpio_config(&gpio_io_conf);

//     spindle_event_queue = xQueueCreate(10, sizeof(uint32_t));
//     xTaskCreatePinnedToCore(gpio_spindle_task, "gpio_worker_task", 2048, NULL, 3, NULL,1);

//     // 5. Initialize the per-pin ISR service and link the handler
//     gpio_install_isr_service(0);
//     gpio_isr_handler_add(spindle_sense_pin, gpio_isr_handler, (void*) spindle_sense_pin);

//     ESP_LOGI(TAG, "Transition detection monitoring initialized.");
// }


// /* Rotory Encoder Section */

// #define re_channel_a GPIO_NUM_25
// #define re_channel_b GPIO_NUM_26
// #define re_button    GPIO_NUM_2

// #define RE_EVENT_QUEUE_LEN 5

// QueueHandle_t re_event_queue;
// rotary_encoder_handle_t re;


// static void encoder_event_handler(const rotary_encoder_event_t *event, void *ctx)
// {
//     QueueHandle_t queue = (QueueHandle_t)ctx;
//     xQueueSendToBack(queue, event, 0);
// }

// rotary_encoder_config_t re_config = {
//     .pin_a = re_channel_a,
//     .pin_b = re_channel_b,
//     .pin_btn = re_button,
//     .btn_pressed_level = 0, // active low
//     .enable_internal_pullup = true, 
//     .callback = encoder_event_handler,
// };




// void re_task(void *arg)
// {
//     // Create queue for rotary encoder events
//     re_event_queue = xQueueCreate(RE_EVENT_QUEUE_LEN, sizeof(rotary_encoder_event_t));
//     rotary_encoder_config_t re_config = {
//     .pin_a = re_channel_a,
//     .pin_b = re_channel_b,
//     .pin_btn = re_button,
//     .btn_pressed_level = 0, // active low
//     .enable_internal_pullup = true, 
//     // .btn_long_press_time_us = 500000,
//     .callback = encoder_event_handler,
//     .callback_ctx = re_event_queue  
// };

//     // Create an encoder
  
//     ESP_ERROR_CHECK(rotary_encoder_create(&re_config, &re));

//     rotary_encoder_event_t e;
//     int32_t val = 0;

//     ESP_LOGI(TAG, "Initial value: %" PRIi32, val);
//     while (1)
//     {
//         xQueueReceive(re_event_queue, &e, portMAX_DELAY);

//         switch (e.type)
//         {
//             case RE_ET_BTN_PRESSED:
//                 ESP_LOGI(TAG, "Button pressed");
//                 break;
//             case RE_ET_BTN_RELEASED:
//                 ESP_LOGI(TAG, "Button released");
//                 break;
//             case RE_ET_BTN_CLICKED:
//                 ESP_LOGI(TAG, "Button clicked");
//                 rotary_encoder_enable_acceleration(re, 100);
//                 ESP_LOGI(TAG, "Acceleration enabled");
//                 break;
//             case RE_ET_BTN_LONG_PRESSED:
//                 ESP_LOGI(TAG, "Looooong pressed button");
//                 rotary_encoder_disable_acceleration(re);
//                 ESP_LOGI(TAG, "Acceleration disabled");
//                 break;
//             case RE_ET_CHANGED:
//                 val += e.diff;
//                 ESP_LOGI(TAG, "Value = %" PRIi32, val);
//                 break;
//             default:
//                 break;
//         }
//     }
// }



void app_main(void)
{

    i2c_handles_t i2c_handles = i2c_init();
    init_spindle_change();
    init_rotary_encoder();

    // initialize the display/spi interfaces
    // lcd_display_init();
    // esp_lcd_panel_swap_xy(lcd_panel_handle,true);
    // esp_lcd_panel_draw_bitmap(lcd_panel_handle, 0, 0, 240, 240, &router_map);
    // vTaskDelay(2000/portTICK_PERIOD_MS);
  

    /*Initialize LVGL library*/
    // lv_init();
    // lv_obj_t * my_base_screen = lv_obj_create(NULL);
    // lv_screen_load(my_base_screen);

    // create a lvgl display
    // lv_display_t *display = lv_display_create(LCD_H_RES, LCD_V_RES);

    // alloc draw buffers used by LVGL
    // it's recommended to choose the size of the draw buffer(s) to be at least 1/10 screen sized
    // size_t draw_buffer_sz = LCD_H_RES * 20 * sizeof(lv_color16_t);
    // void *buf1 = spi_bus_dma_memory_alloc(LCD_HOST, draw_buffer_sz, 0);
    // void *buf2 = spi_bus_dma_memory_alloc(LCD_HOST, draw_buffer_sz, 0);

    // initialize LVGL draw buffers
    // lv_display_set_buffers(display, buf1, buf2, draw_buffer_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);
    
    // // associate the mipi panel handle to the display
    // lv_display_set_user_data(display, lcd_panel_handle);
    
    // // set color depth
    // lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    
    // // set the callback which can copy the rendered image to an area of the display
    // lv_display_set_flush_cb(display, lvgl_flush_cb);
    
    // lv_display_set_rotation(display, LV_DISPLAY_ROTATION_180);

    /* Install LVGL tick timer*/
    // Tick interface for LVGL (using esp_timer to generate 2ms periodic event)
    // const esp_timer_create_args_t lvgl_tick_timer_args = {
    //     .callback = &lvgl_tick,
    //     .name = "lvgl_tick"
    // };
    // esp_timer_handle_t lvgl_tick_timer = NULL;
    // ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    // ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000));

    /*Register io panel event callback for LVGL flush ready notification*/
    // const esp_lcd_panel_io_callbacks_t cbs = {
    //     .on_color_trans_done = notify_lvgl_flush_ready,
    // };
    // ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(lcd_io_handle, &cbs, display));
    
    /* gpio init */
    //init_spindle_change();

    /* init the Re task */
    //xTaskCreatePinnedToCore(re_task, TAG, configMINIMAL_STACK_SIZE * 8, NULL, 2, NULL, 1);




	
    time_at_turn_on = 0;
    total_spindle_time = 0;

	for(;;)
	{
        vTaskDelay(500/portTICK_PERIOD_MS);
	}
    // // // initialize the I2C bus
    // xTaskCreate(ADC_task, "ADC_task", 512, &ucParameterToPass, 3, &xHandle);
    // xTaskCreate(display_task, "display_task", 512, &ucParameterToPass, 3, &xHandle);
    // xTaskCreate(relay_task, "relay", 512, &ucParameterToPass, 3, &xHandle);
}
