idf_component_register(SRCS 
                        "relay_task.c"
                        "spindle_active_task.c"
                        "ADC_task.c"
                       INCLUDE_DIRS 
                       "."
                       "include"
                       REQUIRES esp_timer  esp_driver_gpio)