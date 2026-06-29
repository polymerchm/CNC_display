#ifndef COMPONENTS_INCLUDE_DISPLAY
#define COMPONENTS_INCLUDE_DISPLAY

#include "esp_lcd_ili9341.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_dev.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "hal/gpio_types.h"
#include "hal/spi_types.h"

typedef struct {
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_handle_t panel;
} lcd_user_data_t;

lcd_user_data_t init_lcd_display(void);



#endif  /* COMPONENTS_INCLUDE_DISPLAY */
