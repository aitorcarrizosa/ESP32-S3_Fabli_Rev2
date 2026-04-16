#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void gpio_ctrl_init(void);

// Outputs
void gpio_ctrl_set_pwr_on(bool on);
void gpio_ctrl_set_led_enable(bool on);
void gpio_ctrl_set_led_reset(bool reset_released);

// Inputs
bool gpio_ctrl_get_hp_det(void);
bool gpio_ctrl_get_ac_nok(void);
bool gpio_ctrl_get_chg_ok(void);
bool gpio_ctrl_get_led_fault1_n(void);
bool gpio_ctrl_get_led_fault2_n(void);

// Debug / bring-up
void gpio_ctrl_print_status(void);
void gpio_ctrl_test_toggle(void);

#ifdef __cplusplus
}
#endif