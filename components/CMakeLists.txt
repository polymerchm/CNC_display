idf_component_register(SRCS 
                            "spindle.c"
                            "i2c.c"
                            "display.c"
                            "cnc_encoder.c"
                            "router.c"
                            "logo.c"
                            "pcnt_encoder.c"
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
                            esp_driver_pcnt
                           
                  )