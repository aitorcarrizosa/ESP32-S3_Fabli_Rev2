#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t power_ctrl_init(void);

esp_err_t power_ctrl_set_on(void);
esp_err_t power_ctrl_set_off(void);

float power_ctrl_get_vbat_voltage(void);
float power_ctrl_get_rev_voltage(void);

void power_ctrl_print_status(void);

#ifdef __cplusplus
}
#endif