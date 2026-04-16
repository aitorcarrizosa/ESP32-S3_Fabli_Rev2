#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t sdcard_ctrl_init(void);
esp_err_t sdcard_ctrl_deinit(void);

bool sdcard_ctrl_is_mounted(void);
esp_err_t sdcard_ctrl_print_info(void);

#ifdef __cplusplus
}
#endif