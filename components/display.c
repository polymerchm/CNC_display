#include "inttypes.h"
#include  "../main/main.h"
#include "display.h"


static spi_bus_config_t buscfg = {
    .sclk_io_num = PIN_NUM_SCLK,
    .mosi_io_num = PIN_NUM_MOSI,
    .miso_io_num = PIN_NUM_MISO,
    .quadwp_io_num = -1,
    .quadhd_io_num = -1,
    .max_transfer_sz = LCD_H_RES * 80 * sizeof(uint16_t),
    // transfer 80 lines of pixels (assume pixel is RGB565) at most in one SPI transaction
};

static esp_lcd_panel_io_spi_config_t lcd_io_config = {
    .dc_gpio_num = PIN_NUM_LCD_DC,
    .cs_gpio_num = PIN_NUM_LCD_CS,
    .pclk_hz = LCD_PIXEL_CLOCK_HZ,
    .lcd_cmd_bits = LCD_CMD_BITS,
    .lcd_param_bits = LCD_PARAM_BITS,
    .spi_mode = 0,
    .trans_queue_depth = 10,
};
// Attach the LCD to the SPI buses

static esp_lcd_panel_io_handle_t lcd_io_handle = NULL;

static esp_lcd_panel_dev_config_t panel_config = {
    .reset_gpio_num = PIN_NUM_RST,
    .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
    .bits_per_pixel = 16,
};
// Create LCD panel handle for ILI9341, with the SPI IO device handle
static esp_lcd_panel_handle_t lcd_panel_handle = NULL;

esp_lcd_panel_handle_t lcd_display_init(void)
{
    // gpio_set_direction(PIN_NUM_BKL, GPIO_MODE_OUTPUT);
    // gpio_set_level(PIN_NUM_BKL, 1);
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO)); // Enable the DMA feature
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &lcd_io_config, &lcd_io_handle));
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(lcd_io_handle, &panel_config, &lcd_panel_handle));
    esp_lcd_panel_reset(lcd_panel_handle);
    esp_lcd_panel_init(lcd_panel_handle);
    esp_lcd_panel_disp_on_off(lcd_panel_handle, true);
    return lcd_panel_handle;
}