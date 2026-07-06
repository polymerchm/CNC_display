#include "mcp4725.h"

esp_err_t mcp4725_set_voltage(i2c_master_dev_handle_t dac, uint16_t value)
{
  uint8_t buffer[] = {((value >> 8) | MCP4725_WRITE_FAST),
                      (value & MCP4725_MASK)};

  esp_err_t ret = i2c_master_transmit(dac, buffer, sizeof(buffer), -1);
  return ret;
}

esp_err_t mcp4725_power_down(i2c_master_dev_handle_t dac, mcp4725_power_down_t mode)
{
  uint8_t buffer[] = {((mode << 4) | MCP4725_WRITE_FAST), 0};
  esp_err_t ret = i2c_master_transmit(dac, buffer, sizeof(buffer), -1);
  return ret;
};

esp_err_t mcp4725_read_eeprom(i2c_master_dev_handle_t dac, mcp4725_eeprom_t *eeprom)
{
  uint8_t data_rd[5];

  esp_err_t ret = i2c_master_receive(dac, data_rd, 5, -1);

  eeprom->power_down_mode = (uint16_t)data_rd[3] >> 5;
  eeprom->input_data = ((uint16_t)data_rd[3] & 0b1111) << 8;
  eeprom->input_data |= (uint16_t)data_rd[4];
  return ret;
}

esp_err_t mcp4725_write_eeprom(i2c_master_dev_handle_t dac, mcp4725_eeprom_t eeprom)
{
  uint8_t buffer[] = {
      ((eeprom.power_down_mode << 1) | MCP4725_WRITE_DAC_EEPROM),
      (eeprom.input_data >> 4),
      ((eeprom.input_data << 4) & MCP4725_MASK)};

  esp_err_t ret = i2c_master_transmit(dac, buffer, sizeof(buffer), -1);
  return ret;
}