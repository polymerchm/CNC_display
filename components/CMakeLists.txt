idf_component_register(SRCS 
                            "spindle.c"
                            "cnc_i2c.c"
                            "cnc_encoder.c"
                            "logo.c"
                            "formatWithCommas.c"
                            "ads1115.c"
                       INCLUDE_DIRS 
                            "."
                            "include"
                       REQUIRES 
                            freertos
                            esp_timer  
                            esp_driver_gpio 
                            esp_driver_i2c 
                            esp_adc encoder
                            esp_lcd 
                            esp_lcd_ili9341
                            encoder
                            lvgl
    
                           
                  )