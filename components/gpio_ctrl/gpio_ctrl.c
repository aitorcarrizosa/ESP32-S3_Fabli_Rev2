#include "gpio_ctrl.h"

#include "board.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "gpio_ctrl";

static bool s_test_toggle_state = false;

void gpio_ctrl_init(void)
{
    esp_err_t ret;

    // -------------------------------------------------------------------------
    // Configure outputs
    // -------------------------------------------------------------------------
    const gpio_config_t out_conf = {
        .pin_bit_mask = (1ULL << PIN_PWR_ON) |
                        (1ULL << PIN_LED_EN) |
                        (1ULL << PIN_LED_RESET_N),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ret = gpio_config(&out_conf);
    ESP_ERROR_CHECK(ret);

    // -------------------------------------------------------------------------
    // Configure inputs
    // -------------------------------------------------------------------------
    const gpio_config_t in_conf = {
        .pin_bit_mask = (1ULL << PIN_HP_DET) |
                        (1ULL << PIN_AC_NOK) |
                        (1ULL << PIN_CHG_OK) |
                        (1ULL << PIN_LED_FAULT1_N) |
                        (1ULL << PIN_LED_FAULT2_N),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ret = gpio_config(&in_conf);
    ESP_ERROR_CHECK(ret);

    // -------------------------------------------------------------------------
    // Safe initial output states
    // -------------------------------------------------------------------------
    gpio_ctrl_set_pwr_on(true);
    gpio_ctrl_set_led_enable(false);
    gpio_ctrl_set_led_reset(true);

    ESP_LOGI(TAG, "GPIO control initialized");
    gpio_ctrl_print_status();
}

void gpio_ctrl_set_pwr_on(bool on)
{
    ESP_ERROR_CHECK(gpio_set_level(PIN_PWR_ON, on ? 1 : 0));
}

void gpio_ctrl_set_led_enable(bool on)
{
    ESP_ERROR_CHECK(gpio_set_level(PIN_LED_EN, on ? 1 : 0));
}

void gpio_ctrl_set_led_reset(bool reset_released)
{
    // Signal name is LED_nRESET:
    // 1 = reset released
    // 0 = reset asserted
    ESP_ERROR_CHECK(gpio_set_level(PIN_LED_RESET_N, reset_released ? 1 : 0));
}

bool gpio_ctrl_get_hp_det(void)
{
    return gpio_get_level(PIN_HP_DET) ? true : false;
}

bool gpio_ctrl_get_ac_nok(void)
{
    return gpio_get_level(PIN_AC_NOK) ? true : false;
}

bool gpio_ctrl_get_chg_ok(void)
{
    return gpio_get_level(PIN_CHG_OK) ? true : false;
}

bool gpio_ctrl_get_led_fault1_n(void)
{
    return gpio_get_level(PIN_LED_FAULT1_N) ? true : false;
}

bool gpio_ctrl_get_led_fault2_n(void)
{
    return gpio_get_level(PIN_LED_FAULT2_N) ? true : false;
}

void gpio_ctrl_print_status(void)
{
    ESP_LOGI(TAG,
             "Inputs: HP_DET=%d AC_nOK=%d CHG_OK=%d LED_nFAULT_1=%d LED_nFAULT_2=%d",
             gpio_ctrl_get_hp_det(),
             gpio_ctrl_get_ac_nok(),
             gpio_ctrl_get_chg_ok(),
             gpio_ctrl_get_led_fault1_n(),
             gpio_ctrl_get_led_fault2_n());
}

void gpio_ctrl_test_toggle(void)
{
    s_test_toggle_state = !s_test_toggle_state;

    gpio_ctrl_set_led_enable(s_test_toggle_state);
    gpio_ctrl_set_led_reset(!s_test_toggle_state);

    ESP_LOGI(TAG,
             "Test toggle: LED_EN=%d LED_nRESET=%d",
             s_test_toggle_state,
             !s_test_toggle_state);

    gpio_ctrl_print_status();
}