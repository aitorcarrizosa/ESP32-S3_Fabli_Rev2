#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t encoder_ctrl_init(void);

void encoder_ctrl_get_position(int32_t *position, int32_t *revolutions);
void encoder_ctrl_reset(void);

void encoder_ctrl_print_status(void);
void encoder_ctrl_run_test(void);

#ifdef __cplusplus
}
#endif