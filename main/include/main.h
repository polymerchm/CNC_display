#ifndef MAIN_MAIN
#define MAIN_MAIN




/* GPIO */

#define SPINDLE_PIN GPIO_NUM_32
#define PIN_NUM_SCLK 18
#define PIN_NUM_MOSI 23
#define PIN_NUM_LCD_CS 5
#define PIN_NUM_BKL 4
#define PIN_NUM_RST 22
#define PIN_NUM_LCD_DC 21

#define LCD_HOST SPI2_HOST
#define LCD_PIXEL_CLOCK_HZ 20 * 1000 * 1000
#define LCD_CMD_BITS 8
#define LCD_PARAM_BITS 8

#define LVGL_TICK_PERIOD_MS 5

/* Rotory Encoder Section */

#define re_channel_a GPIO_NUM_25
#define re_channel_b GPIO_NUM_26
#define re_button    GPIO_NUM_2
#define RE_EVENT_QUEUE_LEN 5





#endif  /* MAIN_MAIN */
