#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "board.h"
#include "gpio_ctrl.h"
#include "i2c_bus.h"

void console_start(void);

static const char *TAG = "ESP32-S3_Fabli_Rev2";

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

    xTaskCreate(console_task, "console_task", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "System initialization complete");
}