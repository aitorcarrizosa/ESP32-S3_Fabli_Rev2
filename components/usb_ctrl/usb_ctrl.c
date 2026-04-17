#include "usb_ctrl.h"

#include <stdio.h>
#include <string.h>

#include "board.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "gpio_ctrl.h"
#include "i2c_bus.h"

static const char *TAG = "usb_ctrl";

/* -------------------------------------------------------------------------- */
/* FUSB303 configuration                                                      */
/* -------------------------------------------------------------------------- */

#define USB_CTRL_I2C_BUS   I2C_BUS_0
#define USB_CTRL_I2C_ADDR  0x42

/* Minimal register subset for bring-up / product integration */
#define FUSB303_REG_DEVICE_ID   0x01
#define FUSB303_REG_STATUS      0x40
#define FUSB303_REG_TYPE        0x41
#define FUSB303_REG_INTERRUPT   0x42

static bool s_initialized = false;
static bool s_audio_mode = false; /* false = USB mode, true = audio mode */

/* -------------------------------------------------------------------------- */
/* Local helpers                                                              */
/* -------------------------------------------------------------------------- */

static esp_err_t usb_ctrl_configure_gpio_outputs(void)
{
    const gpio_config_t out_conf = {
        .pin_bit_mask = (1ULL << PIN_AUDIO_SEL),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&out_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure AUDIO_SEL GPIO: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

static esp_err_t usb_ctrl_update_power_path_from_input_status(void)
{
    bool usb_valid = !gpio_ctrl_get_ac_nok();

    gpio_ctrl_set_usb_src(usb_valid);

    ESP_LOGI(TAG, "USB_SRC set to %d (AC_nOK=%d)",
             usb_valid ? 1 : 0,
             gpio_ctrl_get_ac_nok() ? 1 : 0);

    return ESP_OK;
}

/* -------------------------------------------------------------------------- */
/* Raw register access                                                        */
/* -------------------------------------------------------------------------- */

esp_err_t usb_ctrl_read_reg(uint8_t reg, uint8_t *value)
{
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return i2c_bus_read_reg(USB_CTRL_I2C_BUS, USB_CTRL_I2C_ADDR, reg, value);
}

esp_err_t usb_ctrl_write_reg(uint8_t reg, uint8_t value)
{
    return i2c_bus_write_reg(USB_CTRL_I2C_BUS, USB_CTRL_I2C_ADDR, reg, value);
}

/* -------------------------------------------------------------------------- */
/* GPIO-associated USB signals                                                */
/* -------------------------------------------------------------------------- */

bool usb_ctrl_get_int_n(void)
{
    return gpio_get_level(PIN_FUSB_INT_N) ? true : false;
}

bool usb_ctrl_get_id(void)
{
    return gpio_get_level(PIN_FUSB_ID) ? true : false;
}

esp_err_t usb_ctrl_set_power_path(bool enable)
{
    gpio_ctrl_set_usb_src(enable);
    return ESP_OK;
}

bool usb_ctrl_get_power_path(void)
{
    return gpio_ctrl_get_usb_src();
}

esp_err_t usb_ctrl_set_mode_usb(void)
{
    /* AUDIO_SEL = 0 -> connector routed for USB mode */
    esp_err_t ret = gpio_set_level(PIN_AUDIO_SEL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set USB mode: %s", esp_err_to_name(ret));
        return ret;
    }

    s_audio_mode = false;
    ESP_LOGI(TAG, "Connector set to USB mode");
    return ESP_OK;
}

esp_err_t usb_ctrl_set_mode_audio(void)
{
    /* AUDIO_SEL = 1 -> connector routed for audio mode */
    esp_err_t ret = gpio_set_level(PIN_AUDIO_SEL, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set audio mode: %s", esp_err_to_name(ret));
        return ret;
    }

    s_audio_mode = true;
    ESP_LOGI(TAG, "Connector set to audio mode");
    return ESP_OK;
}

bool usb_ctrl_get_audio_sel(void)
{
    return s_audio_mode;
}

/* -------------------------------------------------------------------------- */
/* Init / status                                                              */
/* -------------------------------------------------------------------------- */

esp_err_t usb_ctrl_init(void)
{
    esp_err_t ret;
    uint8_t dev_id = 0;

    ret = usb_ctrl_configure_gpio_outputs();
    if (ret != ESP_OK) {
        return ret;
    }

    /* Default product mode at boot */
    ret = usb_ctrl_set_mode_usb();
    if (ret != ESP_OK) {
        return ret;
    }

    /* Update USB power path from charger input status */
    ret = usb_ctrl_update_power_path_from_input_status();
    if (ret != ESP_OK) {
        return ret;
    }

    /* Check if FUSB303 is present */
    ret = usb_ctrl_read_reg(FUSB303_REG_DEVICE_ID, &dev_id);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "FUSB303 not responding...");
        s_initialized = false;
        return ESP_OK;
    }

    ESP_LOGI(TAG, "FUSB303 detected, DEVICE_ID=0x%02X", dev_id);

    s_initialized = true;
    return ESP_OK;
}

esp_err_t usb_ctrl_get_status(usb_ctrl_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(status, 0, sizeof(*status));

    status->int_n = usb_ctrl_get_int_n();
    status->id = usb_ctrl_get_id();
    status->usb_src = usb_ctrl_get_power_path();
    status->audio_sel = usb_ctrl_get_audio_sel();

    esp_err_t ret = usb_ctrl_read_reg(FUSB303_REG_DEVICE_ID, &status->reg_device_id);
    if (ret != ESP_OK) {
        status->fusb_present = false;
        return ret;
    }

    status->fusb_present = true;

    ret = usb_ctrl_read_reg(FUSB303_REG_INTERRUPT, &status->reg_interrupt);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = usb_ctrl_read_reg(FUSB303_REG_STATUS, &status->reg_status);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = usb_ctrl_read_reg(FUSB303_REG_TYPE, &status->reg_type);
    if (ret != ESP_OK) {
        return ret;
    }

    return ESP_OK;
}

esp_err_t usb_ctrl_print_status(void)
{
    usb_ctrl_status_t status;
    esp_err_t ret = usb_ctrl_get_status(&status);
    if (ret != ESP_OK) {
        printf("USB status failed: %s\r\n", esp_err_to_name(ret));
        return ret;
    }

    printf("\r\n");
    printf("USB controller status:\r\n");
    printf("  initialized : %d\r\n", s_initialized ? 1 : 0);
    printf("  FUSB present : %d\r\n", status.fusb_present ? 1 : 0);
    printf("  FUSB_nINT    : %d\r\n", status.int_n ? 1 : 0);
    printf("  FUSB_ID      : %d\r\n", status.id ? 1 : 0);
    printf("  USB_SRC      : %d\r\n", status.usb_src ? 1 : 0);
    printf("  AUDIO_SEL    : %d\r\n", status.audio_sel ? 1 : 0);
    printf("  DEVICE_ID    : 0x%02X\r\n", status.reg_device_id);
    printf("  INTERRUPT    : 0x%02X\r\n", status.reg_interrupt);
    printf("  STATUS       : 0x%02X\r\n", status.reg_status);
    printf("  TYPE         : 0x%02X\r\n", status.reg_type);
    printf("\r\n");

    return ESP_OK;
}