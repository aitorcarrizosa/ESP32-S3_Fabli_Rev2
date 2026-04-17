#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool fusb_present;
    bool int_n;
    bool id;
    bool usb_src;
    bool audio_sel;
    uint8_t reg_device_id;
    uint8_t reg_interrupt;
    uint8_t reg_status;
    uint8_t reg_type;
} usb_ctrl_status_t;

esp_err_t usb_ctrl_init(void);

esp_err_t usb_ctrl_read_reg(uint8_t reg, uint8_t *value);
esp_err_t usb_ctrl_write_reg(uint8_t reg, uint8_t value);

bool usb_ctrl_get_int_n(void);
bool usb_ctrl_get_id(void);

esp_err_t usb_ctrl_set_power_path(bool enable);
bool usb_ctrl_get_power_path(void);

esp_err_t usb_ctrl_set_mode_usb(void);
esp_err_t usb_ctrl_set_mode_audio(void);
bool usb_ctrl_get_audio_sel(void);

esp_err_t usb_ctrl_get_status(usb_ctrl_status_t *status);
esp_err_t usb_ctrl_print_status(void);

#ifdef __cplusplus
}
#endif