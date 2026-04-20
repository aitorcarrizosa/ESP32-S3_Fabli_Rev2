#include "gpio_ctrl.h"

#include "board.h"

#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "gpio_ctrl";

/* -------------------------------------------------------------------------- */
/* Local helpers                                                              */
/* -------------------------------------------------------------------------- */

static bool gpio_ctrl_read_level(gpio_num_t pin)
{
    return gpio_get_level(pin) ? true : false;
}

static esp_err_t gpio_ctrl_write_level(gpio_num_t pin, bool high)
{
    return gpio_set_level(pin, high ? 1 : 0);
}

/* -------------------------------------------------------------------------- */
/* Init                                                                       */
/* -------------------------------------------------------------------------- */

esp_err_t gpio_ctrl_init(void)
{
    esp_err_t ret;

    const gpio_config_t out_conf = {
        .pin_bit_mask = (1ULL << PIN_PWR_ON) |
                        (1ULL << PIN_LED_EN) |
                        (1ULL << PIN_LED_RESET_N) |
                        (1ULL << PIN_USB_SRC) |
                        (1ULL << PIN_AUDIO_SEL),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ret = gpio_config(&out_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure output GPIOs: %s", esp_err_to_name(ret));
        return ret;
    }

    const gpio_config_t in_conf = {
        .pin_bit_mask = (1ULL << PIN_HP_DET) |
                        (1ULL << PIN_AC_NOK) |
                        (1ULL << PIN_CHG_OK) |
                        (1ULL << PIN_LED_FAULT1_N) |
                        (1ULL << PIN_LED_FAULT2_N) |
                        (1ULL << PIN_FUSB_INT_N) |
                        (1ULL << PIN_FUSB_ID) |
                        (1ULL << PIN_KEY_INT_N) |
                        (1ULL << PIN_MEMS_INT1) |
                        (1ULL << PIN_ENCODER_A) |
                        (1ULL << PIN_ENCODER_B) |
                        (1ULL << PIN_ENCODER_SW_N),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ret = gpio_config(&in_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure input GPIOs: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Safe default states only. Functional policy belongs to upper modules. */
    ret = gpio_ctrl_set_pwr_on(true);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = gpio_ctrl_set_led_enable(false);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = gpio_ctrl_set_led_reset(true);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = gpio_ctrl_set_usb_src(false);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = gpio_ctrl_set_audio_sel(false);
    if (ret != ESP_OK) {
        return ret;
    }

    ESP_LOGI(TAG, "GPIO initialized");
    return ESP_OK;
}

/* -------------------------------------------------------------------------- */
/* Low-level output control                                                   */
/* -------------------------------------------------------------------------- */

esp_err_t gpio_ctrl_set_pwr_on(bool on)
{
    return gpio_ctrl_write_level(PIN_PWR_ON, on);
}

esp_err_t gpio_ctrl_set_led_enable(bool on)
{
    return gpio_ctrl_write_level(PIN_LED_EN, on);
}

esp_err_t gpio_ctrl_set_led_reset(bool reset_released)
{
    return gpio_ctrl_write_level(PIN_LED_RESET_N, reset_released);
}

esp_err_t gpio_ctrl_set_usb_src(bool on)
{
    return gpio_ctrl_write_level(PIN_USB_SRC, on);
}

esp_err_t gpio_ctrl_set_audio_sel(bool audio_mode)
{
    return gpio_ctrl_write_level(PIN_AUDIO_SEL, audio_mode);
}

/* -------------------------------------------------------------------------- */
/* Output reads                                                               */
/* -------------------------------------------------------------------------- */

bool gpio_ctrl_get_pwr_on(void)
{
    return gpio_ctrl_read_level(PIN_PWR_ON);
}

bool gpio_ctrl_get_led_enable(void)
{
    return gpio_ctrl_read_level(PIN_LED_EN);
}

bool gpio_ctrl_get_led_reset(void)
{
    return gpio_ctrl_read_level(PIN_LED_RESET_N);
}

bool gpio_ctrl_get_usb_src(void)
{
    return gpio_ctrl_read_level(PIN_USB_SRC);
}

bool gpio_ctrl_get_audio_sel(void)
{
    return gpio_ctrl_read_level(PIN_AUDIO_SEL);
}

/* -------------------------------------------------------------------------- */
/* Input reads                                                                */
/* -------------------------------------------------------------------------- */

bool gpio_ctrl_get_hp_det(void)
{
    return gpio_ctrl_read_level(PIN_HP_DET);
}

bool gpio_ctrl_get_ac_nok(void)
{
    return gpio_ctrl_read_level(PIN_AC_NOK);
}

bool gpio_ctrl_get_chg_ok(void)
{
    return gpio_ctrl_read_level(PIN_CHG_OK);
}

bool gpio_ctrl_get_led_fault1_n(void)
{
    return gpio_ctrl_read_level(PIN_LED_FAULT1_N);
}

bool gpio_ctrl_get_led_fault2_n(void)
{
    return gpio_ctrl_read_level(PIN_LED_FAULT2_N);
}

bool gpio_ctrl_get_fusb_int_n(void)
{
    return gpio_ctrl_read_level(PIN_FUSB_INT_N);
}

bool gpio_ctrl_get_fusb_id(void)
{
    return gpio_ctrl_read_level(PIN_FUSB_ID);
}

bool gpio_ctrl_get_key_int_n(void)
{
    return gpio_ctrl_read_level(PIN_KEY_INT_N);
}

bool gpio_ctrl_get_mems_int1(void)
{
    return gpio_ctrl_read_level(PIN_MEMS_INT1);
}

bool gpio_ctrl_get_encoder_a(void)
{
    return gpio_ctrl_read_level(PIN_ENCODER_A);
}

bool gpio_ctrl_get_encoder_b(void)
{
    return gpio_ctrl_read_level(PIN_ENCODER_B);
}

bool gpio_ctrl_get_encoder_sw_n(void)
{
    return gpio_ctrl_read_level(PIN_ENCODER_SW_N);
}

/* -------------------------------------------------------------------------- */
/* Status                                                                     */
/* -------------------------------------------------------------------------- */

void gpio_ctrl_print_status(void)
{
    printf("\r\n");
    printf("GPIO status:\r\n");

    printf("  Inputs:\r\n");
    printf("    HP_DET        = %d\r\n", gpio_ctrl_get_hp_det() ? 1 : 0);
    printf("    AC_nOK        = %d\r\n", gpio_ctrl_get_ac_nok() ? 1 : 0);
    printf("    CHG_OK        = %d\r\n", gpio_ctrl_get_chg_ok() ? 1 : 0);
    printf("    LED_nFAULT_1  = %d\r\n", gpio_ctrl_get_led_fault1_n() ? 1 : 0);
    printf("    LED_nFAULT_2  = %d\r\n", gpio_ctrl_get_led_fault2_n() ? 1 : 0);
    printf("    FUSB_nINT     = %d\r\n", gpio_ctrl_get_fusb_int_n() ? 1 : 0);
    printf("    FUSB_ID       = %d\r\n", gpio_ctrl_get_fusb_id() ? 1 : 0);
    printf("    KEY_nINT      = %d\r\n", gpio_ctrl_get_key_int_n() ? 1 : 0);
    printf("    MEMS_INT1     = %d\r\n", gpio_ctrl_get_mems_int1() ? 1 : 0);
    printf("    ENCODER_A     = %d\r\n", gpio_ctrl_get_encoder_a() ? 1 : 0);
    printf("    ENCODER_B     = %d\r\n", gpio_ctrl_get_encoder_b() ? 1 : 0);
    printf("    ENCODER_nSW   = %d\r\n", gpio_ctrl_get_encoder_sw_n() ? 1 : 0);

    printf("  Outputs:\r\n");
    printf("    PWR_ON        = %d\r\n", gpio_ctrl_get_pwr_on() ? 1 : 0);
    printf("    LED_EN        = %d\r\n", gpio_ctrl_get_led_enable() ? 1 : 0);
    printf("    LED_nRESET    = %d\r\n", gpio_ctrl_get_led_reset() ? 1 : 0);
    printf("    USB_SRC       = %d\r\n", gpio_ctrl_get_usb_src() ? 1 : 0);
    printf("    AUDIO_SEL     = %d\r\n", gpio_ctrl_get_audio_sel() ? 1 : 0);

    printf("\r\n");
}