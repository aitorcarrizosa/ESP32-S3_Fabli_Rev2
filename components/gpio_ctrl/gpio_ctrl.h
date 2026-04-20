#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t gpio_ctrl_init(void);
void gpio_ctrl_print_status(void);

/* -------------------------------------------------------------------------- */
/* Low-level output control                                                    */
/* -------------------------------------------------------------------------- */
esp_err_t gpio_ctrl_set_pwr_on(bool on);
esp_err_t gpio_ctrl_set_led_enable(bool on);
esp_err_t gpio_ctrl_set_led_reset(bool reset_released);
esp_err_t gpio_ctrl_set_usb_src(bool on);
esp_err_t gpio_ctrl_set_audio_sel(bool audio_mode);

/* -------------------------------------------------------------------------- */
/* Low-level GPIO reads                                                        */
/* -------------------------------------------------------------------------- */
/* Outputs */
bool gpio_ctrl_get_pwr_on(void);
bool gpio_ctrl_get_led_enable(void);
bool gpio_ctrl_get_led_reset(void);
bool gpio_ctrl_get_usb_src(void);
bool gpio_ctrl_get_audio_sel(void);

/* Inputs */
bool gpio_ctrl_get_hp_det(void);
bool gpio_ctrl_get_ac_nok(void);
bool gpio_ctrl_get_chg_ok(void);
bool gpio_ctrl_get_led_fault1_n(void);
bool gpio_ctrl_get_led_fault2_n(void);
bool gpio_ctrl_get_fusb_int_n(void);
bool gpio_ctrl_get_fusb_id(void);
bool gpio_ctrl_get_key_int_n(void);
bool gpio_ctrl_get_mems_int1(void);
bool gpio_ctrl_get_encoder_a(void);
bool gpio_ctrl_get_encoder_b(void);
bool gpio_ctrl_get_encoder_sw_n(void);

#ifdef __cplusplus
}
#endif