#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    int i2s_port;
    uint32_t sample_rate;
    uint8_t bit_depth;
} audio_ctrl_config_t;

typedef struct {
    audio_ctrl_config_t config;
    uint8_t volume;
    bool is_initialized;
} audio_ctrl_handle_t;

esp_err_t audio_ctrl_init(void);

esp_err_t audio_ctrl_start(void);
esp_err_t audio_ctrl_stop(void);
esp_err_t audio_ctrl_set_volume(uint8_t volume);

void audio_ctrl_print_status(void);
void audio_ctrl_run_test(void);