// #include <stdio.h>
// #include <inttypes.h>
// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"
// #include "freertos/queue.h"
// #include "esp_log.h"
// #include "esp_timer.h"

// #include "driver/gpio.h"
// #include "esp_adc/adc_oneshot.h"
// #include "encoder.h" 
// #include "driver/i2c_master.h"
// #include "cnc_i2c.h"


// /* IIC data */
// #define ADC_ADDR (0x48)
// #define RELAY_ADDR (0x3F)
// #define DAC_ADDR (0x5f)

// #define SDA GPIO_NUM_16
// #define SCL GPIO_NUM_17

// static const char * TAG = "i2c";

// static i2c_master_bus_config_t i2c_bus_config = {
//     .i2c_port = I2C_NUM_0,
//     .sda_io_num = SDA,
//     .scl_io_num = SCL,
//     .clk_source = I2C_CLK_SRC_DEFAULT,
//     .glitch_ignore_cnt = 7,
//     .flags.enable_internal_pullup = true,
// };
// i2c_master_bus_handle_t bus_handle;

// static i2c_device_config_t relay_i2C_cfg = {
//     // relay board
//     .dev_addr_length = I2C_ADDR_BIT_LEN_7,
//     .device_address = ADC_ADDR,
//     .scl_speed_hz = 100000, // 100kHz
// };


// static i2c_device_config_t adc_i2C_cfg = {
//     // relay board
//     .dev_addr_length = I2C_ADDR_BIT_LEN_7,
//     .device_address = RELAY_ADDR,
//     .scl_speed_hz = 100000, // 100kHz
// };


// static i2c_device_config_t dac_i2C_cfg = {
//     // relay board
//     .dev_addr_length = I2C_ADDR_BIT_LEN_7,
//     .device_address = DAC_ADDR,
//     .scl_speed_hz = 100000, // 400kHz
// };



// esp_err_t i2c_init(i2c_handles_t *h) {

//     esp_err_t ret = ESP_OK;
//     i2c_master_dev_handle_t dev;

//     ret = i2c_new_master_bus(&i2c_bus_config, &bus_handle);
//     if (ret != ESP_OK) {
//         return ret;
//     }  else {
//         ESP_LOGI(TAG, "assigned the bus");
//     }
//     ret = i2c_master_bus_add_device(bus_handle, &adc_i2C_cfg, &dev);
//         if (ret != ESP_OK) {
//         return ret;
//     } else {
//         h->adc = dev;
//         ESP_LOGI(TAG,"ADC Added %x", dev);
//     }
//     ret = i2c_master_bus_add_device(bus_handle, &relay_i2C_cfg, &dev);
//         if (ret != ESP_OK) {
//         return ret;
//     }else {
//         h->relays = dev;
//         ESP_LOGI(TAG,"Relays Added %x", dev);
//     }
//     ret = i2c_master_bus_add_device(bus_handle, &dac_i2C_cfg, &dev);
//         if (ret != ESP_OK) {
//         return ret;
//     }else {
//         h->dac = dev;
//         ESP_LOGI(TAG,"DAC Added %x", dev);
//     }
//     return ret;

// }




// /* 
//         the relay board uses a uint8 parameter.

//         Cx where x is ignored and C if the active low control mask.

//         01111111 ( 0x7x) enables relay #1
//         10111111 ( 0xBx) enables relay #2, 
//                 etc.


// */

// esp_err_t set_relay(i2c_master_dev_handle_t relays, int relay_num, bool state) {
//     /* sets state of the designated relay */

//     ESP_LOGI(TAG, "in the set_relay, hand is %x",relays);
//     uint8_t data;
//     size_t data_size = sizeof(data);
//     esp_err_t ret;

//     // get current state
//     ret = i2c_master_receive(relays, &data, data_size, -1);

//     if (ret == ESP_OK) {
//         ESP_LOGI(TAG, "current relay state is %02x", data);
//     } else {
//         ESP_LOGI(TAG, "Read Failed: %s", esp_err_to_name(ret));
//     }

//     uint8_t mask =  1 << (8 - relay_num);
//     if (state) { // turn on relay (set low)
//         data &= ~mask;
//     } else { // set high
//         data |= mask;
//     }
//     ret = i2c_master_transmit(relays, &data, data_size, -1);
//     if (ret == ESP_OK) {
//         ESP_LOGI(TAG, "Successfully changed the relays");
//     } else {
//         ESP_LOGI(TAG, "Error on write %s", esp_err_to_name(ret));
//     }
//     return ret;
// }

// esp_err_t clear_relays(i2c_master_dev_handle_t relays) {
//     /* opens all relays */
//     uint8_t all_open = 0xff;
//     size_t data_size = sizeof(all_open);
//     esp_err_t ret;



//     ret = i2c_master_transmit(relays, &all_open , data_size, -1);
//     if (ret == ESP_OK) {
//         ESP_LOGI(TAG, "Successfully changed the relays");
//     } else {
//         ESP_LOGI(TAG, "Error on write %s", esp_err_to_name(ret));
//     }
//     return ret;

// }

// esp_err_t set_relays(i2c_master_dev_handle_t relays, uint8_t mask) {
//         /* set all relays to mask (active low)*/
//     uint8_t data;
//     size_t data_size = sizeof(data);
//     esp_err_t ret;

//     data =  ((mask << 4) | 0x0f);

//     ret = i2c_master_transmit(relays, &data, data_size, -1);
//     if (ret == ESP_OK) {
//         ESP_LOGI(TAG, "Successfully changed the relays");
//     } else {
//         ESP_LOGI(TAG, "Error on write %s", esp_err_to_name(ret));
//     }
//     return ret;
// }

//     /******************************************
//      * 
//      * DF4803 DAC
//      * 
//      */
    
// void setDACOutRange(i2c_master_dev_handle_t dac)
// {
//     uint8_t buffer[] = {0x01, 0x11}; // set to 0-10V
//     ESP_ERROR_CHECK(i2c_master_transmit(dac, buffer, sizeof(buffer), -1));
// }

// static void getVoltageBytes(voltageData_t *buffer, uint16_t voltage) {
//     buffer->low = (uint8_t) (voltage & 0xff) << 4;
//     buffer->high = (uint8_t) voltage >> 8;
// }

// void setDACOutVoltage(i2c_master_dev_handle_t dac, uint16_t voltage, uint8_t channel) // voltage is a 12, integer
// {
//     uint8_t channel_register;
//     voltageData_t voltage_buffer;
    
//     size_t bytes_to_send;

    
//     if (channel == 0) {
//         channel_register = 0x02;
//         bytes_to_send = 3;
//     } else if ( channel == 1) {
//         channel_register = 0x04;
//         bytes_to_send = 3;
//     } else { // both channels}
//         channel_register = 0x02;
//         bytes_to_send = 5;
//     }

//     if(voltage > 0x0fff)
//         voltage = 0X0fff; // truncate to 12 bit max.
        
//     getVoltageBytes(&voltage_buffer, voltage);
//     ESP_LOGI(TAG, "Ouoput is %d converted to %x %x", voltage, voltage_buffer.low, voltage_buffer.high);
//     const uint8_t outBuffer[] = {
//         0x04, 0xf0, 0xff
//         // voltage_buffer.low, voltage_buffer.high, 
//         // voltage_buffer.low, voltage_buffer.high
//     }; // load up to set both channels in case

    
//     ESP_ERROR_CHECK(i2c_master_transmit(dac, &outBuffer[0], bytes_to_send, -1));
// }







