#include "usb_ctrl.h"

#include <stdio.h>

#include "board.h"
#include "gpio_ctrl.h"

#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "usb_ctrl";

/* -------------------------------------------------------------------------- */
/* Local helpers                                                              */
/* -------------------------------------------------------------------------- */
static esp_err_t usb_ctrl_update_usb_src(void)
{
    bool usb_valid = !gpio_ctrl_get_ac_nok();
    return gpio_ctrl_set_usb_src(usb_valid);
}

/* -------------------------------------------------------------------------- */
/* Initialization                                                             */
/* -------------------------------------------------------------------------- */
esp_err_t usb_ctrl_init(void)
{
    /*
     * Default connector mode at boot:
     * keep the connector in USB mode.
     */
    ESP_ERROR_CHECK(usb_ctrl_set_mode_usb());

    /*
     * USB_SRC remains automatic:
     * valid input present -> USB_SRC = 1
     * no valid input      -> USB_SRC = 0
     */
    ESP_ERROR_CHECK(usb_ctrl_update_usb_src());

    ESP_LOGI(TAG, "USB control initialized");
    return ESP_OK;
}

/* -------------------------------------------------------------------------- */
/* Connector mode control                                                     */
/* -------------------------------------------------------------------------- */
esp_err_t usb_ctrl_set_mode_usb(void)
{
    /*
     * AUDIO_SEL = 0 -> connector routed for USB mode
     */
    return gpio_ctrl_set_audio_sel(false);
}

esp_err_t usb_ctrl_set_mode_audio(void)
{
    /*
     * AUDIO_SEL = 1 -> connector routed for audio mode
     */
    return gpio_ctrl_set_audio_sel(true);
}

/* -------------------------------------------------------------------------- */
/* Status                                                                     */
/* -------------------------------------------------------------------------- */
void usb_ctrl_print_status(void)
{
    bool audio_mode = gpio_ctrl_get_audio_sel();

    /*
     * Refresh automatic USB_SRC state before printing.
     */
    (void)usb_ctrl_update_usb_src();

    printf("\r\n");
    printf("USB status:\r\n");
    printf("  MODE      = %s\r\n", audio_mode ? "AUDIO" : "USB");
    printf("  USB_SRC   = %d\r\n", gpio_ctrl_get_usb_src() ? 1 : 0);
    printf("  AUDIO_SEL = %d\r\n", gpio_ctrl_get_audio_sel() ? 1 : 0);
    printf("  FUSB_nINT = %d\r\n", gpio_ctrl_get_fusb_int_n() ? 1 : 0);
    printf("  FUSB_ID   = %d\r\n", gpio_ctrl_get_fusb_id() ? 1 : 0);
    printf("\r\n");
}