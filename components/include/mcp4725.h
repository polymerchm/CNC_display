#ifndef COMPONENTS_INCLUDE_MCP4725
#define COMPONENTS_INCLUDE_MCP4725

#ifdef __cplusplus
extern "C"
{
#endif

#include "driver/i2c_master.h"
#include <stdio.h>

#define MCP4725_WRITE_FAST 0x00
#define MCP4725_WRITE_DAC 0x40
#define MCP4725_WRITE_DAC_EEPROM 0x60
#define MCP4725_MASK 0xFF

    typedef enum
    {
        MCP4725_POWER_DOWN_0 = 0,
        MCP4725_POWER_DOWN_1,
        MCP4725_POWER_DOWN_100,
        MCP4725_POWER_DOWN_500
    } mcp4725_power_down_t;

    typedef struct
    {
        mcp4725_power_down_t power_down_mode;
        uint16_t input_data;
    } mcp4725_eeprom_t;

    esp_err_t mcp4725_set_voltage(i2c_master_dev_handle_t dac, uint16_t value);

    esp_err_t mcp4725_power_down(i2c_master_dev_handle_t dac, mcp4725_power_down_t mode);

    esp_err_t mcp4725_read_eeprom(i2c_master_dev_handle_t dac, mcp4725_eeprom_t *eeprom);

    esp_err_t mcp4725_write_eeprom(i2c_master_dev_handle_t dac, mcp4725_eeprom_t eeprom);

#ifdef __cplusplus
}
#endif

#endif /* COMPONENTS_INCLUDE_MCP4725 */
