#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    I2C_BUS_0 = 0,
    I2C_BUS_1 = 1,
} i2c_bus_id_t;

esp_err_t i2c_bus_init(void);
bool i2c_bus_is_initialized(i2c_bus_id_t bus_id);

esp_err_t i2c_bus_scan(i2c_bus_id_t bus_id);
esp_err_t i2c_bus_probe(i2c_bus_id_t bus_id, uint8_t address);
esp_err_t i2c_bus_read_reg(i2c_bus_id_t bus, uint8_t dev_addr, uint8_t reg, uint8_t *value);
esp_err_t i2c_bus_write_reg(i2c_bus_id_t bus, uint8_t dev_addr, uint8_t reg, uint8_t value);

i2c_master_bus_handle_t i2c_bus_get_handle(i2c_bus_id_t bus_id);

#ifdef __cplusplus
}
#endif