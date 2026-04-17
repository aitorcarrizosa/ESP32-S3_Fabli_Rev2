#include "sdcard_ctrl.h"

#include <stdio.h>
#include <dirent.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board.h"

#include "driver/sdmmc_host.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#define SDCARD_BUS_WIDTH              4
#define SDCARD_MAX_RETRIES            3
#define SDCARD_RETRY_DELAY_MS         100
#define SDCARD_STABILIZATION_DELAY_MS 100

static const char *TAG = "sdcard_ctrl";
static const char *s_mount_point = "/sdcard";

static sdmmc_card_t *s_card = NULL;
static bool s_mounted = false;

static esp_err_t sdcard_ctrl_check_card_status(void)
{
    if (s_card == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = sdmmc_get_status(s_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get card status: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

esp_err_t sdcard_ctrl_mount(void)
{
    if (s_mounted) {
        ESP_LOGW(TAG, "uSD card already mounted");
        return ESP_OK;
    }

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags = SDMMC_HOST_FLAG_4BIT;
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = SDCARD_BUS_WIDTH;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    slot_config.clk = BOARD_SD_CLK_PIN;
    slot_config.cmd = BOARD_SD_CMD_PIN;
    slot_config.d0  = BOARD_SD_D0_PIN;
    slot_config.d1  = BOARD_SD_D1_PIN;
    slot_config.d2  = BOARD_SD_D2_PIN;
    slot_config.d3  = BOARD_SD_D3_PIN;

    slot_config.cd = SDMMC_SLOT_NO_CD;
    slot_config.wp = SDMMC_SLOT_NO_WP;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = true,
        .use_one_fat = false,
    };

    ESP_LOGI(TAG, "Mounting uSD card");
    ESP_LOGI(TAG, "uSD pins: CLK=%d CMD=%d D0=%d D1=%d D2=%d D3=%d",
             BOARD_SD_CLK_PIN,
             BOARD_SD_CMD_PIN,
             BOARD_SD_D0_PIN,
             BOARD_SD_D1_PIN,
             BOARD_SD_D2_PIN,
             BOARD_SD_D3_PIN);

    vTaskDelay(pdMS_TO_TICKS(SDCARD_STABILIZATION_DELAY_MS));

    esp_err_t ret = ESP_FAIL;

    for (int retry = 0; retry < SDCARD_MAX_RETRIES; retry++) {
        ret = esp_vfs_fat_sdmmc_mount(s_mount_point,
                                      &host,
                                      &slot_config,
                                      &mount_config,
                                      &s_card);

        if (ret == ESP_OK) {
            ret = sdcard_ctrl_check_card_status();
            if (ret == ESP_OK) {
                s_mounted = true;
                ESP_LOGI(TAG, "uSD card mounted successfully");
                sdmmc_card_print_info(stdout, s_card);
                return ESP_OK;
            }

            ESP_LOGW(TAG, "Card mounted but status check failed: %s", esp_err_to_name(ret));
            esp_vfs_fat_sdcard_unmount(s_mount_point, s_card);
            s_card = NULL;
        }

        ESP_LOGW(TAG, "Mount attempt %d/%d failed: %s",
                 retry + 1,
                 SDCARD_MAX_RETRIES,
                 esp_err_to_name(ret));

        vTaskDelay(pdMS_TO_TICKS(SDCARD_RETRY_DELAY_MS));
    }

    s_card = NULL;
    s_mounted = false;

    ESP_LOGE(TAG, "Failed to mount uSD card after %d attempts", SDCARD_MAX_RETRIES);
    return ret;
}

esp_err_t sdcard_ctrl_unmount(void)
{
    if (!s_mounted || s_card == NULL) {
        ESP_LOGW(TAG, "uSD card is not mounted");
        return ESP_OK;
    }

    esp_err_t ret = esp_vfs_fat_sdcard_unmount(s_mount_point, s_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to unmount uSD card: %s", esp_err_to_name(ret));
        return ret;
    }

    s_card = NULL;
    s_mounted = false;

    ESP_LOGI(TAG, "uSD card unmounted");
    return ESP_OK;
}

bool sdcard_ctrl_is_mounted(void)
{
    return s_mounted;
}

esp_err_t sdcard_ctrl_print_info(void)
{
    if (!s_mounted || s_card == NULL) {
        ESP_LOGW(TAG, "uSD card is not mounted");
        return ESP_ERR_INVALID_STATE;
    }

    printf("\r\n");
    printf("uSD card info:\r\n");
    sdmmc_card_print_info(stdout, s_card);
    printf("Mount point: %s\r\n", s_mount_point);

    DIR *dir = opendir(s_mount_point);
    if (dir == NULL) {
        ESP_LOGW(TAG, "Mounted but failed to open mount point");
        return ESP_OK;
    }

    printf("Directory listing:\r\n");

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        printf("  %s\r\n", entry->d_name);
    }

    closedir(dir);
    printf("\r\n");

    return ESP_OK;
}