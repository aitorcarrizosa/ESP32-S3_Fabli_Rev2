#include "acc_ctrl.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "esp_log.h"
#include "esp_err.h"
#include "driver/gpio.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board.h"
#include "i2c_bus.h"
#include "unity.h"

#define ACC_CTRL_I2C_BUS      I2C_BUS_0
#define ACC_CTRL_I2C_ADDR     0x18
#define ACC_CTRL_INT1_PIN     PIN_MEMS_INT1

#define ACC_TEST_DURATION_MS 10000

#define WHO_AM_I_REG          0x0F
#define CTRL_REG1             0x20
#define CTRL_REG2             0x21
#define CTRL_REG3             0x22
#define CTRL_REG4             0x23
#define CTRL_REG5             0x24
#define STATUS_REG            0x27
#define OUT_X_L               0x28
#define INT1_CFG              0x30
#define INT1_SRC              0x31
#define INT1_THS              0x32
#define INT1_DURATION         0x33

#define AUTO_INCREMENT        0x80
#define WHO_AM_I_VALUE        0x33
#define SETUP_DELAY_MS        100

#define SCALE_2G              0.98f

static const char *TAG = "acc_ctrl";

static bool s_acc_initialized = false;
static volatile bool s_acc_interrupt_triggered = false;

static void acc_ctrl_isr_handler(void *arg)
{
    (void)arg;
    s_acc_interrupt_triggered = true;
}

static esp_err_t acc_ctrl_write_reg(uint8_t reg, uint8_t value)
{
    return i2c_bus_write_reg(ACC_CTRL_I2C_BUS, ACC_CTRL_I2C_ADDR, reg, value);
}

static esp_err_t acc_ctrl_read_reg(uint8_t reg, uint8_t *value)
{
    return i2c_bus_read_reg(ACC_CTRL_I2C_BUS, ACC_CTRL_I2C_ADDR, reg, value);
}

static esp_err_t acc_ctrl_read_reg_multi(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_bus_read_regs(ACC_CTRL_I2C_BUS, ACC_CTRL_I2C_ADDR, reg, data, len);
}

esp_err_t acc_ctrl_init(void)
{
    esp_err_t ret;

    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_POSEDGE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << ACC_CTRL_INT1_PIN),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };

    ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = gpio_isr_handler_add(ACC_CTRL_INT1_PIN, acc_ctrl_isr_handler, NULL);
    if ((ret != ESP_OK) && (ret != ESP_ERR_INVALID_STATE)) {
        return ret;
    }

    uint8_t who_am_i = 0;

    ret = acc_ctrl_read_reg(WHO_AM_I_REG, &who_am_i);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read WHO_AM_I");
        return ret;
    }

    if (who_am_i != WHO_AM_I_VALUE) {
        ESP_LOGE(TAG, "Invalid WHO_AM_I: 0x%02X", who_am_i);
        return ESP_ERR_INVALID_RESPONSE;
    }

    ret = acc_ctrl_write_reg(CTRL_REG5, 0x80);
    if (ret != ESP_OK) {
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(SETUP_DELAY_MS));

    ret = acc_ctrl_write_reg(CTRL_REG1, 0x57);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = acc_ctrl_write_reg(CTRL_REG2, 0x00);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = acc_ctrl_write_reg(CTRL_REG3, 0x00);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = acc_ctrl_write_reg(CTRL_REG4, 0x88);
    if (ret != ESP_OK) {
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(SETUP_DELAY_MS));

    s_acc_initialized = true;
    s_acc_interrupt_triggered = false;

    ESP_LOGI(TAG, "LIS2DH12 initialized");

    return ESP_OK;
}

esp_err_t acc_ctrl_read(acc_ctrl_data_t *data)
{
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_acc_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t status = 0;
    esp_err_t ret;

    do {
        ret = acc_ctrl_read_reg(STATUS_REG, &status);
        if (ret != ESP_OK) {
            return ret;
        }

        if (!(status & 0x08)) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    } while (!(status & 0x08));

    uint8_t raw_data[6] = {0};

    ret = acc_ctrl_read_reg_multi(OUT_X_L | AUTO_INCREMENT, raw_data, sizeof(raw_data));
    if (ret != ESP_OK) {
        return ret;
    }

    int16_t raw_x = (int16_t)(raw_data[0] | (raw_data[1] << 8));
    int16_t raw_y = (int16_t)(raw_data[2] | (raw_data[3] << 8));
    int16_t raw_z = (int16_t)(raw_data[4] | (raw_data[5] << 8));

    data->x = (int16_t)((float)raw_x * SCALE_2G);
    data->y = (int16_t)((float)raw_y * SCALE_2G);
    data->z = (int16_t)((float)raw_z * SCALE_2G);

    return ESP_OK;
}

esp_err_t acc_ctrl_set_interrupt(uint8_t int_config, uint8_t threshold, uint8_t duration)
{
    if (!s_acc_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret;

    ret = acc_ctrl_write_reg(INT1_CFG, int_config);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = acc_ctrl_write_reg(INT1_THS, threshold);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = acc_ctrl_write_reg(INT1_DURATION, duration);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = acc_ctrl_write_reg(CTRL_REG3, 0x40);
    if (ret != ESP_OK) {
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(SETUP_DELAY_MS));

    s_acc_interrupt_triggered = false;

    return ESP_OK;
}

bool acc_ctrl_interrupt_triggered(void)
{
    return s_acc_interrupt_triggered;
}

esp_err_t acc_ctrl_clear_interrupt(void)
{
    uint8_t int_src = 0;
    esp_err_t ret = acc_ctrl_read_reg(INT1_SRC, &int_src);

    if (ret == ESP_OK) {
        s_acc_interrupt_triggered = false;
    }

    return ret;
}

bool acc_ctrl_is_initialized(void)
{
    return s_acc_initialized;
}

static void test_acc_ctrl_init(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, acc_ctrl_init());
    TEST_ASSERT_TRUE(acc_ctrl_is_initialized());
}

static void test_acc_ctrl_read(void)
{
    acc_ctrl_data_t data;

    if (!acc_ctrl_is_initialized()) {
        TEST_ASSERT_EQUAL(ESP_OK, acc_ctrl_init());
    }

    TEST_ASSERT_EQUAL(ESP_OK, acc_ctrl_read(&data));

    TEST_ASSERT_TRUE(abs(data.x) <= 2000);
    TEST_ASSERT_TRUE(abs(data.y) <= 2000);
    TEST_ASSERT_TRUE(abs(data.z) <= 2000);

    ESP_LOGI(TAG, "ACC Data: X=%d mg, Y=%d mg, Z=%d mg", data.x, data.y, data.z);
}

static void test_acc_ctrl_read_loop(void)
{
    acc_ctrl_data_t data;

    if (!acc_ctrl_is_initialized()) {
        TEST_ASSERT_EQUAL(ESP_OK, acc_ctrl_init());
    }

    TickType_t start_time = xTaskGetTickCount();

    while ((xTaskGetTickCount() - start_time) < pdMS_TO_TICKS(ACC_TEST_DURATION_MS)) {
        TEST_ASSERT_EQUAL(ESP_OK, acc_ctrl_read(&data));

        TEST_ASSERT_TRUE(abs(data.x) <= 2000);
        TEST_ASSERT_TRUE(abs(data.y) <= 2000);
        TEST_ASSERT_TRUE(abs(data.z) <= 2000);

        ESP_LOGI(TAG, "ACC Data: X=%d mg, Y=%d mg, Z=%d mg", data.x, data.y, data.z);

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void test_acc_ctrl_interrupt_config(void)
{
    if (!acc_ctrl_is_initialized()) {
        TEST_ASSERT_EQUAL(ESP_OK, acc_ctrl_init());
    }

    ESP_LOGI(TAG, "\n\n=== ACC Interrupt Test Starting ===");
    ESP_LOGI(TAG, "You have %d seconds to test the interrupt.", ACC_TEST_DURATION_MS / 1000);
    ESP_LOGI(TAG, "Move the board to trigger the accelerometer interrupt.");

    uint8_t int_config = 0x4A;
    uint8_t threshold = 0x20;
    uint8_t duration = 0x00;

    TEST_ASSERT_EQUAL(ESP_OK, acc_ctrl_set_interrupt(int_config, threshold, duration));

    TickType_t start_time = xTaskGetTickCount();
    bool test_passed = false;

    while ((xTaskGetTickCount() - start_time) < pdMS_TO_TICKS(ACC_TEST_DURATION_MS)) {
        if (acc_ctrl_interrupt_triggered()) {
            TEST_ASSERT_EQUAL(ESP_OK, acc_ctrl_clear_interrupt());
            ESP_LOGI(TAG, "ACC interrupt triggered");
            test_passed = true;
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (!test_passed) {
        TEST_FAIL_MESSAGE("No ACC interrupt detected");
    }
}

void acc_ctrl_run_test(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_acc_ctrl_init);
    RUN_TEST(test_acc_ctrl_read);
    RUN_TEST(test_acc_ctrl_read_loop);
    RUN_TEST(test_acc_ctrl_interrupt_config);

    UNITY_END();
}