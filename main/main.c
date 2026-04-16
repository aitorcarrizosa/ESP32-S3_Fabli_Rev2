#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "board.h"
#include "gpio_ctrl.h"

void console_start_uart0(void);

static const char *TAG = "ESP32-S3_Fabli_Rev2";

static void console_task(void *arg)
{
    (void)arg;
    console_start_uart0();
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Application start");

    board_init();
    gpio_ctrl_init();

    xTaskCreate(console_task, "console_task", 4096, NULL, 5, NULL);

    while (1) {
        gpio_ctrl_test_toggle();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}