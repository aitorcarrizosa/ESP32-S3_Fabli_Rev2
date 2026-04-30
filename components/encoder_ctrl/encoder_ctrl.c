#include "encoder_ctrl.h"

#include <stdio.h>

#include "esp_err.h"
#include "esp_log.h"
#include "unity.h"

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board.h"

/* -------------------------------------------------------------------------- */
/* Defines                                                                    */
/* -------------------------------------------------------------------------- */
#define ENCODER_INT_PRIO          3
#define TICKS_PER_REVOLUTION      62

/* -------------------------------------------------------------------------- */
/* Local data                                                                 */
/* -------------------------------------------------------------------------- */
static const char *TAG = "encoder_ctrl";

static portMUX_TYPE encoder_spinlock = portMUX_INITIALIZER_UNLOCKED;

typedef struct {
    int32_t position;
    int32_t revolutions;
    uint8_t last_state;
} encoder_ctrl_state_t;

static encoder_ctrl_state_t encoder = {0};

/* -------------------------------------------------------------------------- */
/* Internal prototypes                                                        */
/* -------------------------------------------------------------------------- */
static void encoder_isr_handler(void *arg);
static void encoder_sw_isr_handler(void *arg);

static void test_encoder_init(void);
static void test_encoder_read_position(void);
static void test_encoder_read_revolution(void);
static void test_encoder_reset(void);

/* -------------------------------------------------------------------------- */
/* ISR handlers                                                               */
/* -------------------------------------------------------------------------- */
/**
 * @brief Interrupt Service Routine handler for the rotary encoder A/B signals.
 * @note Updates encoder position and revolution count based on quadrature state.
 */
static void encoder_isr_handler(void *arg)
{
    (void)arg;

    uint8_t a = gpio_get_level(PIN_ENCODER_A);
    uint8_t b = gpio_get_level(PIN_ENCODER_B);
    uint8_t current_state = (b << 1) | a;

    /* Protect the encoder position updates with a spinlock to prevent race conditions */
    portENTER_CRITICAL_ISR(&encoder_spinlock);

    switch(encoder.last_state) {
        case 0:
            if(current_state == 1) encoder.position++;      // 00 -> 01 (CW)
            if(current_state == 2) encoder.position--;      // 00 -> 10 (CCW)
            break;
        case 1:
            if(current_state == 3) encoder.position++;      // 01 -> 11 (CW)
            if(current_state == 0) encoder.position--;      // 01 -> 00 (CCW)
            break;
        case 2:
            if(current_state == 0) encoder.position++;      // 10 -> 00 (CW)
            if(current_state == 3) encoder.position--;      // 10 -> 11 (CCW)
            break;
        case 3:
            if(current_state == 2) encoder.position++;      // 11 -> 10 (CW)
            if(current_state == 1) encoder.position--;      // 11 -> 01 (CCW)
            break;
    }

    encoder.last_state = current_state;

    if (encoder.position >= TICKS_PER_REVOLUTION) {
        encoder.revolutions++;
        encoder.position -= TICKS_PER_REVOLUTION;
    } else if (encoder.position <= -TICKS_PER_REVOLUTION) {
        encoder.revolutions--;
        encoder.position += TICKS_PER_REVOLUTION;
    }

    portEXIT_CRITICAL_ISR(&encoder_spinlock);
}

/**
 * @brief Interrupt Service Routine handler for the encoder switch.
 * @note Resets encoder position when the switch is pressed.
 */
static void encoder_sw_isr_handler(void *arg)
{
    (void)arg;

    portENTER_CRITICAL_ISR(&encoder_spinlock);
    encoder.position = 0;
    portEXIT_CRITICAL_ISR(&encoder_spinlock);
}

/* -------------------------------------------------------------------------- */
/* API                                                                 */
/* -------------------------------------------------------------------------- */
esp_err_t encoder_ctrl_init(void)
{
    esp_err_t err = gpio_install_isr_service(ENCODER_INT_PRIO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to install GPIO ISR service: %s", esp_err_to_name(err));
        return err;
    }

    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_ANYEDGE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << PIN_ENCODER_A) |
                        (1ULL << PIN_ENCODER_B) |
                        (1ULL << PIN_ENCODER_SW_N),
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };

    err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure encoder GPIO pins");
        return err;
    }

    gpio_isr_handler_remove(PIN_ENCODER_A);
    gpio_isr_handler_remove(PIN_ENCODER_B);
    gpio_isr_handler_remove(PIN_ENCODER_SW_N);

    err = gpio_isr_handler_add(PIN_ENCODER_A, encoder_isr_handler, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add ISR handler for encoder A");
        return err;
    }

    err = gpio_isr_handler_add(PIN_ENCODER_B, encoder_isr_handler, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add ISR handler for encoder B");
        return err;
    }

    err = gpio_isr_handler_add(PIN_ENCODER_SW_N, encoder_sw_isr_handler, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add ISR handler for encoder switch");
        return err;
    }

    ESP_LOGI(TAG, "Encoder initialized successfully");
    return ESP_OK;
}

void encoder_ctrl_get_position(int32_t *position, int32_t *revolutions)
{
    if (position == NULL || revolutions == NULL) {
        return;
    }

    portENTER_CRITICAL(&encoder_spinlock);
    *position = encoder.position;
    *revolutions = encoder.revolutions;
    portEXIT_CRITICAL(&encoder_spinlock);
}

void encoder_ctrl_reset(void)
{
    portENTER_CRITICAL(&encoder_spinlock);
    encoder.position = 0;
    encoder.revolutions = 0;
    encoder.last_state = 0;
    portEXIT_CRITICAL(&encoder_spinlock);
}

void encoder_ctrl_print_status(void)
{
    int32_t position = 0;
    int32_t revolutions = 0;

    encoder_ctrl_get_position(&position, &revolutions);

    printf("\r\n");
    printf("Encoder status:\r\n");
    printf("  ENCODER_A      = %d\r\n", gpio_get_level(PIN_ENCODER_A));
    printf("  ENCODER_B      = %d\r\n", gpio_get_level(PIN_ENCODER_B));
    printf("  ENCODER_SW_N   = %d\r\n", gpio_get_level(PIN_ENCODER_SW_N));
    printf("  Position       = %ld\r\n", (long)position);
    printf("  Revolutions    = %ld\r\n", (long)revolutions);
    printf("\r\n");
}

/* -------------------------------------------------------------------------- */
/* Unity tests                                                                */
/* -------------------------------------------------------------------------- */
void encoder_ctrl_run_test(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_encoder_init);
    RUN_TEST(test_encoder_read_position);
    RUN_TEST(test_encoder_read_revolution);
    RUN_TEST(test_encoder_reset);
    UNITY_END();
}

/**
 * @brief Unit test for initializing the RIC11 rotary encoder.
 * @note Verifies that encoder_ctrl_init successfully initializes the RIC11 encoder.
 */
static void test_encoder_init(void)
{
    esp_err_t ret = encoder_ctrl_init();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

/**
 * @brief Tests encoder position reading functionality.
 * @note Manually asks the user to rotate the encoder clockwise by 2 clicks within 5 seconds.
 */
static void test_encoder_read_position(void)
{
    int32_t position = 0;
    int32_t revolutions = 0;

    encoder_ctrl_reset();

    ESP_LOGI(TAG, "******************************************");
    ESP_LOGI(TAG, "* PHYSICAL ENCODER ROTATION REQUIRED     *");
    ESP_LOGI(TAG, "* Rotate encoder clockwise by 2 clicks   *");
    ESP_LOGI(TAG, "* You have 5 seconds                     *");
    ESP_LOGI(TAG, "******************************************");

    TickType_t start_time = xTaskGetTickCount();

    while ((xTaskGetTickCount() - start_time) < pdMS_TO_TICKS(5000)) {
        encoder_ctrl_get_position(&position, &revolutions);
        ESP_LOGI(TAG, "encoder: pos: %ld rev: %ld", (long)position, (long)revolutions);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    TEST_ASSERT_EQUAL(2, position);
    TEST_ASSERT_EQUAL(0, revolutions);
}

/**
 * @brief Tests encoder revolution reading functionality.
 * @note Manually asks the user to rotate the encoder clockwise by 1 full revolution within 10 seconds.
 */
static void test_encoder_read_revolution(void)
{
    int32_t position = 0;
    int32_t revolutions = 0;

    encoder_ctrl_reset();

    ESP_LOGI(TAG, "******************************************");
    ESP_LOGI(TAG, "* PHYSICAL ENCODER ROTATION REQUIRED     *");
    ESP_LOGI(TAG, "* Rotate encoder clockwise 1 full turn   *");
    ESP_LOGI(TAG, "* You have 10 seconds                    *");
    ESP_LOGI(TAG, "******************************************");

    TickType_t start_time = xTaskGetTickCount();

    while ((xTaskGetTickCount() - start_time) < pdMS_TO_TICKS(10000)) {
        encoder_ctrl_get_position(&position, &revolutions);
        ESP_LOGI(TAG, "encoder: pos: %ld rev: %ld", (long)position, (long)revolutions);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    TEST_ASSERT_EQUAL(1, revolutions);
}

/**
 * @brief Unit test for resetting the RIC11 rotary encoder.
 * @note Verifies that encoder_ctrl_reset resets position and revolutions to 0.
 */
static void test_encoder_reset(void)
{
    int32_t position = 0;
    int32_t revolutions = 0;

    encoder_ctrl_reset();
    encoder_ctrl_get_position(&position, &revolutions);

    TEST_ASSERT_EQUAL(0, position);
    TEST_ASSERT_EQUAL(0, revolutions);
}