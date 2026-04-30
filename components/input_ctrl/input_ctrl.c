/*
*   KEY1..KEY16
*       → Print: KEYx pressed
*
*    SIDE_nSW_1
*        → Print: SIDE 1 pressed
*
*    SIDE_nSW_2
*        → Print: SIDE 2 pressed
*
*    VolUp_nSW
*        → audio_ctrl_set_volume(volume + step)
*
*    VolDwn_nSW
*        → audio_ctrl_set_volume(volume - step)
*
*    BLE_nSW
*        → Print: BLE Switch pressed
*
*    PWR_nSW
*        → reset system
*/

#include "input_ctrl.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board.h"
#include "i2c_bus.h"
#include "audio_ctrl.h"

/* -------------------------------------------------------------------------- */
/* PCAL6524 configuration                                                     */
/* -------------------------------------------------------------------------- */
#define INPUT_CTRL_I2C_BUS              I2C_BUS_0
#define PCAL6524_I2C_ADDR               0x40

#define PCAL6524_REG_INPUT_PORT0        0x00
#define PCAL6524_REG_INPUT_PORT1        0x01
#define PCAL6524_REG_INPUT_PORT2        0x02

#define PCAL6524_REG_OUTPUT_PORT0       0x04
#define PCAL6524_REG_OUTPUT_PORT1       0x05
#define PCAL6524_REG_OUTPUT_PORT2       0x06

#define PCAL6524_REG_CONFIG_PORT0       0x0C
#define PCAL6524_REG_CONFIG_PORT1       0x0D
#define PCAL6524_REG_CONFIG_PORT2       0x0E

#define PCAL6524_REG_PULL_EN_PORT0      0x4C
#define PCAL6524_REG_PULL_EN_PORT1      0x4D
#define PCAL6524_REG_PULL_EN_PORT2      0x4E

#define PCAL6524_REG_PULL_SEL_PORT0     0x50
#define PCAL6524_REG_PULL_SEL_PORT1     0x51
#define PCAL6524_REG_PULL_SEL_PORT2     0x52

#define PCAL6524_REG_INT_MASK_PORT0     0x54
#define PCAL6524_REG_INT_MASK_PORT1     0x55
#define PCAL6524_REG_INT_MASK_PORT2     0x56

#define INPUT_TASK_STACK_SIZE           4096
#define INPUT_TASK_PRIORITY             6
#define INPUT_VOLUME_STEP               5

/* -------------------------------------------------------------------------- */
/* Port 0 mapping                                                             */
/* -------------------------------------------------------------------------- */
#define P0_VOL_UP_NSW                   (1U << 0)
#define P0_VOL_DOWN_NSW                 (1U << 1)
#define P0_BLE_NSW                      (1U << 2)
#define P0_SIDE_NSW_1                   (1U << 3)
#define P0_SIDE_NSW_2                   (1U << 4)
#define P0_PWR_NSW                      (1U << 5)
#define P0_AUDIO_NRST                   (1U << 6)

/*
 * P0_0..P0_5 are button inputs.
 * P0_6 is AUDIO_nRST output.
 */
#define P0_INPUT_MASK (P0_VOL_UP_NSW | P0_VOL_DOWN_NSW | P0_BLE_NSW | P0_SIDE_NSW_1 | P0_SIDE_NSW_2 | P0_PWR_NSW)
#define P0_CONFIG_VALUE                 0xBF    // bit6 output, others inputs
#define P0_INTERRUPT_MASK               0xC0    // enable interrupts on P0_0..P0_5

static const char *TAG = "input_ctrl";
static TaskHandle_t s_input_task_handle = NULL;
static uint8_t s_prev_p0 = 0xFF;
static uint8_t s_prev_p1 = 0xFF;
static uint8_t s_prev_p2 = 0xFF;
static uint8_t s_audio_volume = 50;

/* -------------------------------------------------------------------------- */
/* I2C helpers                                                                */
/* -------------------------------------------------------------------------- */
static esp_err_t input_ctrl_write_reg(uint8_t reg, uint8_t value)
{
    return i2c_bus_write_reg(INPUT_CTRL_I2C_BUS, PCAL6524_I2C_ADDR, reg, value);
}

static esp_err_t input_ctrl_read_reg(uint8_t reg, uint8_t *value)
{
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return i2c_bus_read_reg(INPUT_CTRL_I2C_BUS, PCAL6524_I2C_ADDR, reg, value);
}

/* -------------------------------------------------------------------------- */
/* Key helpers                                                                */
/* -------------------------------------------------------------------------- */
static const char *input_ctrl_key_name_from_p1(uint8_t bit)
{
    switch (bit) {
        case 0: return "KEY8";
        case 1: return "KEY12";
        case 2: return "KEY16";
        case 3: return "KEY15";
        case 4: return "KEY11";
        case 5: return "KEY7";
        case 6: return "KEY6";
        case 7: return "KEY10";
        default: return "UNKNOWN";
    }
}

static const char *input_ctrl_key_name_from_p2(uint8_t bit)
{
    switch (bit) {
        case 0: return "KEY14";
        case 1: return "KEY13";
        case 2: return "KEY9";
        case 3: return "KEY5";
        case 4: return "KEY1";
        case 5: return "KEY2";
        case 6: return "KEY3";
        case 7: return "KEY4";
        default: return "UNKNOWN";
    }
}

static void input_ctrl_volume_up(void)
{
    if (s_audio_volume <= (100 - INPUT_VOLUME_STEP)) {
        s_audio_volume += INPUT_VOLUME_STEP;
    } else {
        s_audio_volume = 100;
    }

    if (audio_ctrl_init() == ESP_OK) {
        (void)audio_ctrl_set_volume(s_audio_volume);
    }

    ESP_LOGI(TAG, "Volume up pressed -> %u%%", s_audio_volume);
}

static void input_ctrl_volume_down(void)
{
    if (s_audio_volume >= INPUT_VOLUME_STEP) {
        s_audio_volume -= INPUT_VOLUME_STEP;
    } else {
        s_audio_volume = 0;
    }

    if (audio_ctrl_init() == ESP_OK) {
        (void)audio_ctrl_set_volume(s_audio_volume);
    }

    ESP_LOGI(TAG, "Volume down pressed -> %u%%", s_audio_volume);
}

/* -------------------------------------------------------------------------- */
/* Event processing                                                           */
/* -------------------------------------------------------------------------- */
static void input_ctrl_process_p0_pressed(uint8_t falling_edges)
{
    if (falling_edges & P0_VOL_UP_NSW) {
        input_ctrl_volume_up();
    }

    if (falling_edges & P0_VOL_DOWN_NSW) {
        input_ctrl_volume_down();
    }

    if (falling_edges & P0_BLE_NSW) {
        ESP_LOGI(TAG, "BLE switch pressed");
    }

    if (falling_edges & P0_SIDE_NSW_1) {
        ESP_LOGI(TAG, "SIDE 1 pressed");
    }

    if (falling_edges & P0_SIDE_NSW_2) {
        ESP_LOGI(TAG, "SIDE 2 pressed");
    }

    if (falling_edges & P0_PWR_NSW) {
        ESP_LOGW(TAG, "Power button pressed -> restarting ESP32");
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_restart();
    }
}

static void input_ctrl_process_keypad_pressed(uint8_t falling_p1, uint8_t falling_p2)
{
    for (uint8_t bit = 0; bit < 8; bit++) {
        if (falling_p1 & (1U << bit)) {
            ESP_LOGI(TAG, "%s pressed", input_ctrl_key_name_from_p1(bit));
        }

        if (falling_p2 & (1U << bit)) {
            ESP_LOGI(TAG, "%s pressed", input_ctrl_key_name_from_p2(bit));
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Interrupt handling                                                         */
/* -------------------------------------------------------------------------- */
static void IRAM_ATTR input_ctrl_int_isr_handler(void *arg)
{
    (void)arg;

    BaseType_t higher_priority_task_woken = pdFALSE;

    if (s_input_task_handle != NULL) {
        vTaskNotifyGiveFromISR(s_input_task_handle, &higher_priority_task_woken);
    }

    if (higher_priority_task_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static void input_ctrl_task(void *arg)
{
    (void)arg;

    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        uint8_t p0 = 0xFF;
        uint8_t p1 = 0xFF;
        uint8_t p2 = 0xFF;

        if (input_ctrl_read_reg(PCAL6524_REG_INPUT_PORT0, &p0) != ESP_OK ||
            input_ctrl_read_reg(PCAL6524_REG_INPUT_PORT1, &p1) != ESP_OK ||
            input_ctrl_read_reg(PCAL6524_REG_INPUT_PORT2, &p2) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read PCAL6524 input ports");
            continue;
        }

        /*
         * Buttons are active-low.
         * A press is detected as previous=1 and current=0.
         */
        uint8_t falling_p0 = (uint8_t)(s_prev_p0 & ~p0) & P0_INPUT_MASK;
        uint8_t falling_p1 = (uint8_t)(s_prev_p1 & ~p1);
        uint8_t falling_p2 = (uint8_t)(s_prev_p2 & ~p2);

        input_ctrl_process_p0_pressed(falling_p0);
        input_ctrl_process_keypad_pressed(falling_p1, falling_p2);

        s_prev_p0 = p0;
        s_prev_p1 = p1;
        s_prev_p2 = p2;
    }
}

/* -------------------------------------------------------------------------- */
/* Initialization                                                             */
/* -------------------------------------------------------------------------- */
esp_err_t input_ctrl_init(void)
{
    esp_err_t ret;

    ret = i2c_bus_probe(INPUT_CTRL_I2C_BUS, PCAL6524_I2C_ADDR);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PCAL6524 not detected at 0x%02X", PCAL6524_I2C_ADDR);
        return ret;
    }

    /*
     * Keep all outputs high by default.
     * This releases active-low controlled lines such as AUDIO_nRST.
     */
    ESP_ERROR_CHECK(input_ctrl_write_reg(PCAL6524_REG_OUTPUT_PORT0, 0xFF));
    ESP_ERROR_CHECK(input_ctrl_write_reg(PCAL6524_REG_OUTPUT_PORT1, 0xFF));
    ESP_ERROR_CHECK(input_ctrl_write_reg(PCAL6524_REG_OUTPUT_PORT2, 0xFF));

    /*
     * Configure directions:
     * - P0_0..P0_5 inputs
     * - P0_6 output for AUDIO_nRST
     * - P1/P2 all inputs for keypad
     */
    ESP_ERROR_CHECK(input_ctrl_write_reg(PCAL6524_REG_CONFIG_PORT0, P0_CONFIG_VALUE));
    ESP_ERROR_CHECK(input_ctrl_write_reg(PCAL6524_REG_CONFIG_PORT1, 0xFF));
    ESP_ERROR_CHECK(input_ctrl_write_reg(PCAL6524_REG_CONFIG_PORT2, 0xFF));

    /*
     * Enable internal pull-ups for all button/key inputs.
     */
    ESP_ERROR_CHECK(input_ctrl_write_reg(PCAL6524_REG_PULL_SEL_PORT0, P0_INPUT_MASK));
    ESP_ERROR_CHECK(input_ctrl_write_reg(PCAL6524_REG_PULL_SEL_PORT1, 0xFF));
    ESP_ERROR_CHECK(input_ctrl_write_reg(PCAL6524_REG_PULL_SEL_PORT2, 0xFF));

    ESP_ERROR_CHECK(input_ctrl_write_reg(PCAL6524_REG_PULL_EN_PORT0, P0_INPUT_MASK));
    ESP_ERROR_CHECK(input_ctrl_write_reg(PCAL6524_REG_PULL_EN_PORT1, 0xFF));
    ESP_ERROR_CHECK(input_ctrl_write_reg(PCAL6524_REG_PULL_EN_PORT2, 0xFF));

    /*
     * Enable interrupts for button/key inputs.
     */
    ESP_ERROR_CHECK(input_ctrl_write_reg(PCAL6524_REG_INT_MASK_PORT0, P0_INTERRUPT_MASK));
    ESP_ERROR_CHECK(input_ctrl_write_reg(PCAL6524_REG_INT_MASK_PORT1, 0x00));
    ESP_ERROR_CHECK(input_ctrl_write_reg(PCAL6524_REG_INT_MASK_PORT2, 0x00));

    /*
     * Read initial state.
     */
    ESP_ERROR_CHECK(input_ctrl_read_reg(PCAL6524_REG_INPUT_PORT0, &s_prev_p0));
    ESP_ERROR_CHECK(input_ctrl_read_reg(PCAL6524_REG_INPUT_PORT1, &s_prev_p1));
    ESP_ERROR_CHECK(input_ctrl_read_reg(PCAL6524_REG_INPUT_PORT2, &s_prev_p2));

    /*
     * Configure ESP32 interrupt input from PCAL6524.
     */
    gpio_config_t int_conf = {
        .pin_bit_mask = (1ULL << PIN_KEY_INT_N),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };

    ret = gpio_config(&int_conf);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    gpio_isr_handler_remove(PIN_KEY_INT_N);

    ret = gpio_isr_handler_add(PIN_KEY_INT_N, input_ctrl_int_isr_handler, NULL);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = xTaskCreate(input_ctrl_task,
                      "input_ctrl_task",
                      INPUT_TASK_STACK_SIZE,
                      NULL,
                      INPUT_TASK_PRIORITY,
                      &s_input_task_handle);

    if (ret != pdPASS) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Input controller initialized");
    return ESP_OK;
}
