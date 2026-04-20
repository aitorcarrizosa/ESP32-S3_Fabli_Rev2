#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t usb_ctrl_init(void);

esp_err_t usb_ctrl_set_mode_usb(void);
esp_err_t usb_ctrl_set_mode_audio(void);

void usb_ctrl_print_status(void);

#ifdef __cplusplus
}
#endif