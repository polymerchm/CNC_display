#ifndef COMPONENTS_INCLUDE__I2C
#define COMPONENTS_INCLUDE__I2C

#include "driver/i2c_master.h"


 typedef struct {
    i2c_master_dev_handle_t dac;
    i2c_master_dev_handle_t relays;
    i2c_master_dev_handle_t adc;
 } i2c_handles_t;
 
esp_err_t i2c_init(i2c_handles_t *h);

esp_err_t set_relay(i2c_master_dev_handle_t relays, int relay_num, bool state);
esp_err_t clear_relays(i2c_master_dev_handle_t relays);
esp_err_t set_relays(i2c_master_dev_handle_t re, uint8_t mask);

// /*********************
//  * 
//  * GP8403 0-10V DAC
//  * 
//  */

//  typedef struct {
//     uint8_t low;
//     uint8_t high;
//  } voltageData_t;


// /*
// * set output range to 0-10V
// */

// void setDACOutRange(i2c_master_dev_handle_t dac); 

// /* 
// * convert input decial voltate (0-4095) to proper two byte dac format
// */
// static void getVoltageBytes(voltageData_t *buffer, uint16_t voltage);
// /*
// * set the output voltage 
// * voltage is 0-4095
// * channel: 0 = channel 0, 1 - channel 2, > 1 = both
// */
// void setDACOutVoltage(i2c_master_dev_handle_t dac, uint16_t voltage, uint8_t channel);


#endif  /* COMPONENTS_INCLUDE__I2C */
