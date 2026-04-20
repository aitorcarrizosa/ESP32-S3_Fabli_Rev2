#include "power_ctrl.h"

#include <stdio.h>

#include "board.h"
#include "gpio_ctrl.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "power_ctrl";

#define POWER_ADC_ATTEN         ADC_ATTEN_DB_12
#define POWER_ADC_BITWIDTH      ADC_BITWIDTH_DEFAULT
#define POWER_ADC_MAX           4095.0f
#define POWER_ADC_VREF          3.3f
#define VBAT_DIVIDER_GAIN       2.0f

static adc_oneshot_unit_handle_t s_adc_handle;
static adc_channel_t s_vbat_channel;
static adc_channel_t s_rev_channel;

/* -------------------------------------------------------------------------- */
/* Local ADC helpers                                                          */
/* ------------------------------------------------------------------------- */
static float power_ctrl_read_voltage(adc_channel_t channel)
{
    int raw = 0;

    if (adc_oneshot_read(s_adc_handle, channel, &raw) != ESP_OK) {
        return -1.0f;
    }

    return ((float)raw / POWER_ADC_MAX) * POWER_ADC_VREF;
}

/* -------------------------------------------------------------------------- */
/* Initialization                                                             */
/* -------------------------------------------------------------------------- */
esp_err_t power_ctrl_init(void)
{
    adc_oneshot_unit_init_cfg_t adc_cfg = {
        .unit_id = ADC_UNIT_1,
    };

    ESP_ERROR_CHECK(adc_oneshot_new_unit(&adc_cfg, &s_adc_handle));

    adc_unit_t unit;

    ESP_ERROR_CHECK(adc_oneshot_io_to_channel(PIN_VBAT_ADC, &unit, &s_vbat_channel));
    ESP_ERROR_CHECK(adc_oneshot_io_to_channel(PIN_REV_ADC, &unit, &s_rev_channel));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = POWER_ADC_ATTEN,
        .bitwidth = POWER_ADC_BITWIDTH,
    };

    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc_handle, s_vbat_channel, &chan_cfg));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc_handle, s_rev_channel, &chan_cfg));

    ESP_LOGI(TAG, "Power control initialized");
    return ESP_OK;
}

/* -------------------------------------------------------------------------- */
/* Power control                                                              */
/* -------------------------------------------------------------------------- */
esp_err_t power_ctrl_set_on(void)
{
    return gpio_ctrl_set_pwr_on(true);
}

esp_err_t power_ctrl_set_off(void)
{
    return gpio_ctrl_set_pwr_on(false);
}

/* -------------------------------------------------------------------------- */
/* Voltage readings                                                           */
/* -------------------------------------------------------------------------- */
float power_ctrl_get_vbat_voltage(void)
{
    float pin_voltage = power_ctrl_read_voltage(s_vbat_channel);
    if (pin_voltage < 0.0f) {
        return -1.0f;
    }

    return pin_voltage * VBAT_DIVIDER_GAIN;
}

float power_ctrl_get_rev_voltage(void)
{
    return power_ctrl_read_voltage(s_rev_channel);
}

/* -------------------------------------------------------------------------- */
/* Status                                                                     */
/* -------------------------------------------------------------------------- */
void power_ctrl_print_status(void)
{
    float vbat = power_ctrl_get_vbat_voltage();
    float rev = power_ctrl_get_rev_voltage();

    printf("\r\n");
    printf("Power status:\r\n");
    printf("  PWR_ON  = %d\r\n", gpio_ctrl_get_pwr_on() ? 1 : 0);
    printf("  AC_nOK  = %d  (%s)\r\n",
           gpio_ctrl_get_ac_nok() ? 1 : 0,
           gpio_ctrl_get_ac_nok() ? "no valid input" : "valid input present");
    printf("  CHG_OK  = %d  (%s)\r\n",
           gpio_ctrl_get_chg_ok() ? 1 : 0,
           gpio_ctrl_get_chg_ok() ? "charge complete / charger idle" : "charging");

    if (vbat >= 0.0f) {
        printf("  VBAT    = %.2f V\r\n", vbat);
    } else {
        printf("  VBAT    = read error\r\n");
    }

    if (rev >= 0.0f) {
        printf("  REV     = %.2f V\r\n", rev);
    } else {
        printf("  REV     = read error\r\n");
    }

    printf("\r\n");
}