#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Device selection enumeration */
typedef enum {
    LED_CTRL_DEVICE_1 = 0,
    LED_CTRL_DEVICE_2,
    LED_CTRL_DEVICE_MAX
} led_ctrl_device_t;

esp_err_t led_ctrl_init(void);

esp_err_t led_ctrl_set_enable(bool on);
esp_err_t led_ctrl_set_reset(bool released);

esp_err_t led_ctrl_set_color(uint8_t led_index, uint8_t red, uint8_t green, uint8_t blue);
esp_err_t led_ctrl_set_brightness(led_ctrl_device_t device, uint8_t brightness);

void led_ctrl_run_test(void);

void led_ctrl_print_status(void);

#ifdef __cplusplus
}
#endif