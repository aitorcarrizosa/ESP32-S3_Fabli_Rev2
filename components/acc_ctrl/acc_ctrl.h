#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    int16_t x;
    int16_t y;
    int16_t z;
} acc_ctrl_data_t;

esp_err_t acc_ctrl_init(void);
esp_err_t acc_ctrl_read(acc_ctrl_data_t *data);
esp_err_t acc_ctrl_set_interrupt(uint8_t int_config, uint8_t threshold, uint8_t duration);
bool acc_ctrl_interrupt_triggered(void);
esp_err_t acc_ctrl_clear_interrupt(void);
bool acc_ctrl_is_initialized(void);
void acc_ctrl_run_test(void);