#include "board.h"
#include "esp_log.h"

static const char *TAG = "board";

void board_init(void)
{
    ESP_LOGI(TAG, "Board configuration loaded from menuconfig");
    ESP_LOGI(TAG, "PWR_ON=%d, LED_EN=%d, LED_nRESET=%d",
             PIN_PWR_ON, PIN_LED_EN, PIN_LED_RESET_N);
    ESP_LOGI(TAG, "HP_DET=%d, AC_nOK=%d, CHG_OK=%d",
             PIN_HP_DET, PIN_AC_NOK, PIN_CHG_OK);
}