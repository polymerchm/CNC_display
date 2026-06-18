#include "inttypes.h"
#include  "../main/include/main.h"
#include "display.h"
#include "esp_log.h"



static const char * TAG = "DISPLAY";
spi_bus_config_t buscfg = ILI9341_PANEL_BUS_SPI_CONFIG(PIN_NUM_SCLK, PIN_NUM_MOSI,
                                                          LCD_H_RES * 40 * sizeof(uint16_t));

static esp_lcd_panel_io_handle_t lcd_io_handle = NULL;
esp_lcd_panel_io_handle_t io = NULL;
esp_lcd_panel_io_spi_config_t io_config = ILI9341_PANEL_IO_SPI_CONFIG(PIN_NUM_LCD_CS, 
    PIN_NUM_LCD_DC, NULL, NULL);



// Create LCD panel handle for ILI9341, with the SPI IO device handle
static esp_lcd_panel_handle_t lcd_panel_handle = NULL;
static esp_lcd_panel_dev_config_t panel_config = {
    .reset_gpio_num = PIN_NUM_RST,
    .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
    .bits_per_pixel = 16,
};

esp_lcd_panel_handle_t init_lcd_display(void)
{
    // gpio_set_direction(PIN_NUM_BKL, GPIO_MODE_OUTPUT);
    // gpio_set_level(PIN_NUM_BKL, 1);
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO)); // Enable the DMA feature
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io));
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(io, &panel_config, &lcd_panel_handle));
    esp_lcd_panel_reset(lcd_panel_handle);
    esp_lcd_panel_init(lcd_panel_handle);
    esp_lcd_panel_disp_on_off(lcd_panel_handle, true);
    ESP_LOGI(TAG,"finished display");
    return lcd_panel_handle;
}