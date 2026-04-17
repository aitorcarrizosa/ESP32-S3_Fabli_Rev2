#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "board.h"
#include "gpio_ctrl.h"
#include "i2c_bus.h"

void console_start(void);

static const char *TAG = "ESP32-S3_Fabli_Rev2";

static void update_usb_src_from_input_status(void)
{
    bool usb_valid = !gpio_ctrl_get_ac_nok();

    gpio_ctrl_set_usb_src(usb_valid);

    ESP_LOGI(TAG, "USB_SRC set to %d (AC_nOK=%d)",
             usb_valid ? 1 : 0,
             gpio_ctrl_get_ac_nok() ? 1 : 0);
}

static void console_task(void *arg)
{
    (void)arg;
    console_start();
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Application start");

    board_init();
    ESP_ERROR_CHECK(gpio_ctrl_init());
    ESP_ERROR_CHECK(i2c_bus_init());

    update_usb_src_from_input_status();

    xTaskCreate(console_task, "console_task", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "System initialization complete");
}