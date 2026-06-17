#ifndef COMPONENTS_INCLUDE__I2C
#define COMPONENTS_INCLUDE__I2C

#include "driver/i2c_master.h"

typedef struct {
    i2c_master_dev_handle_t adc;
    i2c_master_dev_handle_t dac;
    i2c_master_dev_handle_t relays;
} i2c_handles_t;

i2c_handles_t i2c_init(void);

#endif  /* COMPONENTS_INCLUDE__I2C */
