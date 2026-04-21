#include "led_ctrl.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#include "board.h"
#include "esp_err.h"
#include "esp_log.h"
#include "gpio_ctrl.h"
#include "i2c_bus.h"

#include "unity.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "led_ctrl";

/* -------------------------------------------------------------------------- */
/* AL5887 devices and bus                                                     */
/* -------------------------------------------------------------------------- */
#define LED_CTRL_I2C_BUS              I2C_BUS_1
#define AL5887_ADDR_1                 0x30
#define AL5887_ADDR_2                 0x31
#define TOTAL_ADDRESSABLE_LEDS        16
#define AL5887_LEDS_PER_DEVICE        8

/* -------------------------------------------------------------------------- */
/* AL5887 register map                                                        */
/* -------------------------------------------------------------------------- */
/* Register Addresses */
#define AL5887_REG_DEVICE_CONFIG0     0x00
#define AL5887_REG_DEVICE_CONFIG1     0x01
#define AL5887_REG_LED_CONFIG0        0x02
#define AL5887_REG_LED_CONFIG1        0x03
#define AL5887_REG_BANK_BRIGHTNESS    0x04
#define AL5887_REG_BANK_A_COLOR       0x05
#define AL5887_REG_BANK_B_COLOR       0x06
#define AL5887_REG_BANK_C_COLOR       0x07
/* RGB Brightness registers (0x08-0x13) */
#define AL5887_REG_RGB0_BRIGHTNESS    0x08
#define AL5887_REG_RGB_BRIGHTNESS_MAX 0x13
/* RGB Color registers (0x14-0x37) */
#define AL5887_REG_R0_COLOR           0x14
#define AL5887_REG_COLOR_MAX          0x37
/* Control and Status registers */
#define AL5887_REG_RESET              0x38
#define AL5887_REG_FLAG               0x65
#define AL5887_REG_LED_GLOBAL_DIM     0x66
#define AL5887_REG_FAULT_WAIT         0x67
#define AL5887_REG_MASK_CLR           0x68
/* Fault registers */
#define AL5887_REG_OPEN_MASK0         0x6A
#define AL5887_REG_SHORT_MASK0        0x6F
#define AL5887_REG_OPEN_FAULT0        0x76
#define AL5887_REG_SHORT_FAULT0       0x7B
/* Configuration bits */
#define AL5887_CHIP_EN                (1U << 6)
#define AL5887_PHASE_SHIFT_EN         (1U << 7)
#define AL5887_LOG_SCALE_EN           (1U << 5)
#define AL5887_POWER_SAVE_EN          (1U << 4)
#define AL5887_DITHER_EN              (1U << 2)
#define AL5887_MAX_CURRENT_OPT        (1U << 1)
#define AL5887_LED_GLOBAL_OFF         (1U << 0)

/* -------------------------------------------------------------------------- */
/* Rev1 used this exact logical LED mapping.                                  */
/* We keep it exactly as-is so behavior matches the previous design.          */
/* -------------------------------------------------------------------------- */
static const uint8_t led_mappings[48] = {
    0,  1,  2,      /* LED1 - OUT0-2 */
    3,  4,  5,      /* LED2 - OUT3-5 */
    6,  7,  8,      /* LED3 - OUT6-8 */
    10, 11, 12,     /* LED4 - OUT10-12 */
    13, 14, 15,     /* LED5 - OUT13-15 */
    16, 17, 18,     /* LED6 - OUT16-18 */
    20, 21, 22,     /* LED7 - OUT20-22 */
    23, 24, 25,     /* LED8 - OUT23-25 */
    /* First device ends here, Second device starts */
    0,  1,  2,      /* LED9  - OUT0-2 */
    3,  4,  5,      /* LED10 - OUT3-5 */
    6,  7,  8,      /* LED11 - OUT6-8 */
    10, 11, 12,     /* LED12 - OUT10-12 */
    13, 14, 15,     /* LED13 - OUT13-15 */
    16, 17, 18,     /* LED14 - OUT16-18 */
    20, 21, 22,     /* LED15 - OUT20-22 */
    23, 24, 25      /* LED16 - OUT23-25 */
    /* LED17 is the battery indicator LED */
};

static bool initialized = false;
static bool hw_enabled = false;
static esp_err_t led_ctrl_run_diagnostic_check(void);
static void test_al5887_init(void);
static void test_al5887_individual_led_control(void);
static void test_al5887_color_control(void);
static void test_al5887_global_brightness(void);
static void test_al5887_power_modes(void);

/* -------------------------------------------------------------------------- */
/* Local helpers                                                              */
/* -------------------------------------------------------------------------- */
static uint8_t led_ctrl_get_device_address(led_ctrl_device_t device)
{
    return (device == LED_CTRL_DEVICE_1) ? AL5887_ADDR_1 : AL5887_ADDR_2;
}

static bool led_ctrl_device_present(led_ctrl_device_t device)
{
    return i2c_bus_probe(LED_CTRL_I2C_BUS, led_ctrl_get_device_address(device)) == ESP_OK;
}

static esp_err_t led_ctrl_write_reg(led_ctrl_device_t device, uint8_t reg_addr, uint8_t data)
{
    if (device >= LED_CTRL_DEVICE_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    return i2c_bus_write_reg(LED_CTRL_I2C_BUS, led_ctrl_get_device_address(device), reg_addr, data);
}

static esp_err_t led_ctrl_read_reg(led_ctrl_device_t device, uint8_t reg_addr, uint8_t *data)
{
    if (device >= LED_CTRL_DEVICE_MAX || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return i2c_bus_read_reg(LED_CTRL_I2C_BUS, led_ctrl_get_device_address(device), reg_addr, data);
}

static esp_err_t led_ctrl_clear_faults(led_ctrl_device_t device)
{
    /*
     * Rev1 wrote 0x03 to MASK_CLR to clear fault and POR flags.
     */
    return led_ctrl_write_reg(device, AL5887_REG_MASK_CLR, 0x03);
}

static esp_err_t led_ctrl_hw_sequence(void)
{
    /*
     * Keep the same basic enable/reset sequence as Rev1.
     */
    ESP_ERROR_CHECK(led_ctrl_set_enable(false));
    ESP_ERROR_CHECK(led_ctrl_set_reset(false));
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_ERROR_CHECK(led_ctrl_set_enable(true));
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_ERROR_CHECK(led_ctrl_set_reset(true));
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_ERROR_CHECK(led_ctrl_set_reset(false));
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_ERROR_CHECK(led_ctrl_set_reset(true));
    vTaskDelay(pdMS_TO_TICKS(100));

    hw_enabled = true;
    return ESP_OK;
}

static esp_err_t led_ctrl_init_device(led_ctrl_device_t device)
{
    esp_err_t ret;

    /*
     * Same basic initialization philosophy as Rev1:
     * - reset device registers
     * - enable chip
     * - set default bank brightness
     * - clear POR/fault flags
     * - set usable global dimming
     */
    ret = led_ctrl_write_reg(device, AL5887_REG_RESET, 0xFF);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = led_ctrl_write_reg(device, AL5887_REG_DEVICE_CONFIG0, AL5887_CHIP_EN);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = led_ctrl_write_reg(device, AL5887_REG_BANK_BRIGHTNESS, 0x80);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = led_ctrl_write_reg(device, AL5887_REG_LED_GLOBAL_DIM, 0x3F);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = led_ctrl_clear_faults(device);
    if (ret != ESP_OK) {
        return ret;
    }

    return ESP_OK;
}

static esp_err_t led_ctrl_get_flag(led_ctrl_device_t device, uint8_t *flag)
{
    return led_ctrl_read_reg(device, AL5887_REG_FLAG, flag);
}

static esp_err_t led_ctrl_run_diagnostic_check(void)
{
    for (led_ctrl_device_t dev = LED_CTRL_DEVICE_1; dev < LED_CTRL_DEVICE_MAX; dev++) {
        uint8_t reg_value = 0;
        esp_err_t ret;

        ret = led_ctrl_read_reg(dev, AL5887_REG_DEVICE_CONFIG0, &reg_value);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read DEVICE_CONFIG0 from device %d", dev + 1);
            return ret;
        }

        if ((reg_value & AL5887_CHIP_EN) == 0) {
            ESP_LOGE(TAG, "Device %d is not enabled", dev + 1);
            return ESP_FAIL;
        }

        ret = led_ctrl_read_reg(dev, AL5887_REG_BANK_BRIGHTNESS, &reg_value);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read BANK_BRIGHTNESS from device %d", dev + 1);
            return ret;
        }

        ret = led_ctrl_read_reg(dev, AL5887_REG_FLAG, &reg_value);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read FLAG from device %d", dev + 1);
            return ret;
        }
    }

    return ESP_OK;
}

/* -------------------------------------------------------------------------- */
/* Initialization                                                             */
/* -------------------------------------------------------------------------- */
esp_err_t led_ctrl_init(void)
{
    if (initialized) {
        return ESP_OK;
    }
    esp_err_t ret;

    /*
     * Hardware sequence only needs to happen once.
     */
    ret = led_ctrl_hw_sequence();
    if (ret != ESP_OK) {
        return ret;
    }

    /*
     * Initialize both AL5887 devices if present.
     */
    if (led_ctrl_device_present(LED_CTRL_DEVICE_1)) {
        ret = led_ctrl_init_device(LED_CTRL_DEVICE_1);
        if (ret != ESP_OK) {
            return ret;
        }
    } else {
        ESP_LOGW(TAG, "AL5887 device 1 not detected at 0x%02X", AL5887_ADDR_1);
    }

    if (led_ctrl_device_present(LED_CTRL_DEVICE_2)) {
        ret = led_ctrl_init_device(LED_CTRL_DEVICE_2);
        if (ret != ESP_OK) {
            return ret;
        }
    } else {
        ESP_LOGW(TAG, "AL5887 device 2 not detected at 0x%02X", AL5887_ADDR_2);
    }

    initialized = true;
    ESP_LOGI(TAG, "LED control initialized");
    return ESP_OK;
}

/* -------------------------------------------------------------------------- */
/* GPIO control                                                               */
/* -------------------------------------------------------------------------- */
esp_err_t led_ctrl_set_enable(bool on)
{
    return gpio_ctrl_set_led_enable(on);
}

esp_err_t led_ctrl_set_reset(bool released)
{
    return gpio_ctrl_set_led_reset(released);
}

/* -------------------------------------------------------------------------- */
/* LED color / brightness                                                     */
/* -------------------------------------------------------------------------- */
esp_err_t led_ctrl_set_color(uint8_t led_index, uint8_t red, uint8_t green, uint8_t blue)
{
    if (led_index >= TOTAL_ADDRESSABLE_LEDS) {
        return ESP_ERR_INVALID_ARG;
    }

    /*
     * Keep Rev1 device split:
     * - LEDs 0..7  -> device 1
     * - LEDs 8..15 -> device 2
     */
    led_ctrl_device_t device = (led_index < AL5887_LEDS_PER_DEVICE) ? LED_CTRL_DEVICE_1 : LED_CTRL_DEVICE_2;

    /*
     * Rev1 used the exact mapping table above.
     * The table stores the starting register offset for each logical RGB LED triplet.
     */
    uint8_t mapping_base;
    if (device == LED_CTRL_DEVICE_1) {
        mapping_base = led_index * 3;
    } else {
        mapping_base = 24 + ((led_index - AL5887_LEDS_PER_DEVICE) * 3);
    }

    uint8_t device_led_index = led_mappings[mapping_base];
    uint8_t base_addr = AL5887_REG_R0_COLOR + device_led_index;

    esp_err_t ret;

    ret = led_ctrl_write_reg(device, base_addr, red);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = led_ctrl_write_reg(device, base_addr + 1, green);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = led_ctrl_write_reg(device, base_addr + 2, blue);
    if (ret != ESP_OK) {
        return ret;
    }

    return ESP_OK;
}

esp_err_t led_ctrl_set_brightness(led_ctrl_device_t device, uint8_t brightness)
{
    if (device >= LED_CTRL_DEVICE_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    return led_ctrl_write_reg(device, AL5887_REG_LED_GLOBAL_DIM, brightness & 0x3F);
}

/* -------------------------------------------------------------------------- */
/* Status                                                                     */
/* -------------------------------------------------------------------------- */
void led_ctrl_print_status(void)
{
    uint8_t flag1 = 0;
    uint8_t flag2 = 0;
    uint8_t dim1 = 0;
    uint8_t dim2 = 0;

    bool dev1_present = led_ctrl_device_present(LED_CTRL_DEVICE_1);
    bool dev2_present = led_ctrl_device_present(LED_CTRL_DEVICE_2);

    printf("\r\n");
    printf("LED status:\r\n");
    printf("  LED_EN        = %d\r\n", gpio_ctrl_get_led_enable() ? 1 : 0);
    printf("  LED_nRESET    = %d\r\n", gpio_ctrl_get_led_reset() ? 1 : 0);
    printf("  LED_nFAULT_1  = %d\r\n", gpio_ctrl_get_led_fault1_n() ? 1 : 0);
    printf("  LED_nFAULT_2  = %d\r\n", gpio_ctrl_get_led_fault2_n() ? 1 : 0);
    printf("  INIT_DONE     = %d\r\n", initialized ? 1 : 0);
    printf("  DEV1_PRESENT  = %d\r\n", dev1_present ? 1 : 0);
    printf("  DEV2_PRESENT  = %d\r\n", dev2_present ? 1 : 0);

    if (dev1_present) {
        if (led_ctrl_get_flag(LED_CTRL_DEVICE_1, &flag1) == ESP_OK) {
            printf("  DEV1_FLAG     = 0x%02X\r\n", flag1);
        } else {
            printf("  DEV1_FLAG     = read error\r\n");
        }

        if (led_ctrl_read_reg(LED_CTRL_DEVICE_1, AL5887_REG_LED_GLOBAL_DIM, &dim1) == ESP_OK) {
            printf("  DEV1_DIM      = 0x%02X\r\n", dim1);
        } else {
            printf("  DEV1_DIM      = read error\r\n");
        }
    }

    if (dev2_present) {
        if (led_ctrl_get_flag(LED_CTRL_DEVICE_2, &flag2) == ESP_OK) {
            printf("  DEV2_FLAG     = 0x%02X\r\n", flag2);
        } else {
            printf("  DEV2_FLAG     = read error\r\n");
        }

        if (led_ctrl_read_reg(LED_CTRL_DEVICE_2, AL5887_REG_LED_GLOBAL_DIM, &dim2) == ESP_OK) {
            printf("  DEV2_DIM      = 0x%02X\r\n", dim2);
        } else {
            printf("  DEV2_DIM      = read error\r\n");
        }
    }

    printf("\r\n");
}

/* -------------------------------------------------------------------------- */
/* Test                                                                       */
/* -------------------------------------------------------------------------- */
void led_ctrl_run_test(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_al5887_init);

    /* Same extra diagnostic check used in Rev1 */
    TEST_ASSERT_EQUAL(ESP_OK, led_ctrl_run_diagnostic_check());

    RUN_TEST(test_al5887_individual_led_control);
    RUN_TEST(test_al5887_color_control);
    RUN_TEST(test_al5887_global_brightness);
    RUN_TEST(test_al5887_power_modes);
    UNITY_END();
}

/**
 * @brief Unit test for the al5887_init function.
 * @note Verifies that the al5887_init function correctly initializes the
 *       AL5887 device, sets the device configuration, enables the LED banks,
 *       sets the initial bank brightness, and clears the fault flags.
 */
static void test_al5887_init(void)
{
    esp_err_t ret;
    uint8_t reg_value;

    ret = led_ctrl_init();
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    for (led_ctrl_device_t device = LED_CTRL_DEVICE_1; device < LED_CTRL_DEVICE_MAX; device++) {
        if (!led_ctrl_device_present(device)) {
            continue;
        }

        ret = led_ctrl_read_reg(device, AL5887_REG_DEVICE_CONFIG0, &reg_value);
        TEST_ASSERT_EQUAL(ESP_OK, ret);
        TEST_ASSERT_EQUAL(AL5887_CHIP_EN, reg_value & AL5887_CHIP_EN);

        ret = led_ctrl_read_reg(device, AL5887_REG_BANK_BRIGHTNESS, &reg_value);
        TEST_ASSERT_EQUAL(ESP_OK, ret);
        TEST_ASSERT_EQUAL(0x80, reg_value);

        ret = led_ctrl_read_reg(device, AL5887_REG_FLAG, &reg_value);
        TEST_ASSERT_EQUAL(ESP_OK, ret);
        TEST_ASSERT_EQUAL(0x00, reg_value & 0x0F);
    }
}

/**
 * @brief Unit test for individual LED control using the led_ctrl_set_color function.
 * @note Verifies that the led_ctrl_set_color function correctly sets the color of
 *       each LED individually, and that the color values can be read back from
 *       the AL5887 registers. The test turns each LED on and off in sequence.
 */
static void test_al5887_individual_led_control(void)
{
    esp_err_t ret;
    /* Test each LED individually */
    for (uint8_t i = 0; i < TOTAL_ADDRESSABLE_LEDS; i++) {
        /* Set white color */
        ret = led_ctrl_set_color(i, 255, 255, 255);
        TEST_ASSERT_EQUAL(ESP_OK, ret);

        vTaskDelay(pdMS_TO_TICKS(100));
        /* Turn off LED */
        ret = led_ctrl_set_color(i, 0, 0, 0);
        TEST_ASSERT_EQUAL(ESP_OK, ret);

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/**
 * @brief Unit test for color control using the led_ctrl_set_color function.
 * @note Verifies that the led_ctrl_set_color function correctly sets various colors
 *       (red, green, blue, yellow, cyan, magenta) for a single LED, and that
 *       the color values can be read back from the AL5887 registers. The test
 *       iterates through a list of predefined colors and checks the resulting
 *       color values.
 */
static void test_al5887_color_control(void)
{
    esp_err_t ret;

    struct {
        uint8_t r, g, b;
        const char *name;
    } 
    
    colors[] = {
        {255,   0,   0, "Red"},
        {  0, 255,   0, "Green"},
        {  0,   0, 255, "Blue"},
        {255, 255,   0, "Yellow"},
        {  0, 255, 255, "Cyan"},
        {255,   0, 255, "Magenta"}
    };

    for (int i = 0; i < sizeof(colors)/sizeof(colors[0]); i++) {
        ESP_LOGI(TAG, "Testing %s color", colors[i].name);

        for (uint8_t j = 0; j < TOTAL_ADDRESSABLE_LEDS; j++) {
            ret = led_ctrl_set_color(j, colors[i].r, colors[i].g, colors[i].b);
            vTaskDelay(pdMS_TO_TICKS(10));
            TEST_ASSERT_EQUAL(ESP_OK, ret);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/**
 * @brief Unit test for global brightness control using the led_ctrl_set_brightness function.
 * @note Verifies that the led_ctrl_set_brightness function correctly sets different
 *       brightness levels (0, 16, 32, 48, 63) and that the resulting brightness
 *       value can be read back from the AL5887 register.
 */
static void test_al5887_global_brightness(void)
{
    esp_err_t ret;
    uint8_t reg_value;

    /* Test different brightness levels */
    uint8_t brightness_levels[] = {0, 16, 32, 48, 63}; /* Max 6-bit value (0x3F) */

    for (int i = 0; i < sizeof(brightness_levels); i++) {
        /* Set global brightness */
        ret = led_ctrl_set_brightness(LED_CTRL_DEVICE_1, brightness_levels[i]);
        TEST_ASSERT_EQUAL(ESP_OK, ret);

        ret = led_ctrl_set_brightness(LED_CTRL_DEVICE_2, brightness_levels[i]);
        TEST_ASSERT_EQUAL(ESP_OK, ret);
        /* Verify brightness register */
        ret = led_ctrl_read_reg(LED_CTRL_DEVICE_1, AL5887_REG_LED_GLOBAL_DIM, &reg_value);
        TEST_ASSERT_EQUAL(ESP_OK, ret);
        TEST_ASSERT_EQUAL(brightness_levels[i], reg_value);

        ret = led_ctrl_read_reg(LED_CTRL_DEVICE_2, AL5887_REG_LED_GLOBAL_DIM, &reg_value);
        TEST_ASSERT_EQUAL(ESP_OK, ret);
        TEST_ASSERT_EQUAL(brightness_levels[i], reg_value);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    /* Set global brightness to half */
    ret = led_ctrl_set_brightness(LED_CTRL_DEVICE_1, 16);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

/**
 * @brief Unit test for AL5887 power mode control.
 * @note Verifies that the power save mode and global off mode can be enabled
 *       correctly, and that the written values can be read back from the
 *       DEVICE_CONFIG1 register. Finally, the device is restored to normal
 *       operation by enabling dithering.
 */
static void test_al5887_power_modes(void)
{
    esp_err_t ret;
    uint8_t reg_value;

    for (led_ctrl_device_t device = LED_CTRL_DEVICE_1; device < LED_CTRL_DEVICE_MAX; device++) {
        /* Test power save mode */
        ret = led_ctrl_write_reg(device, AL5887_REG_DEVICE_CONFIG1, AL5887_POWER_SAVE_EN);
        TEST_ASSERT_EQUAL(ESP_OK, ret);

        ret = led_ctrl_read_reg(device, AL5887_REG_DEVICE_CONFIG1, &reg_value);
        TEST_ASSERT_EQUAL(ESP_OK, ret);
        TEST_ASSERT_EQUAL(AL5887_POWER_SAVE_EN, reg_value & AL5887_POWER_SAVE_EN);
        /* Test global off mode */
        ret = led_ctrl_write_reg(device, AL5887_REG_DEVICE_CONFIG1, AL5887_LED_GLOBAL_OFF);
        TEST_ASSERT_EQUAL(ESP_OK, ret);

        ret = led_ctrl_read_reg(device, AL5887_REG_DEVICE_CONFIG1, &reg_value);
        TEST_ASSERT_EQUAL(ESP_OK, ret);
        TEST_ASSERT_EQUAL(AL5887_LED_GLOBAL_OFF, reg_value & AL5887_LED_GLOBAL_OFF);
        /* Restore normal operation */
        ret = led_ctrl_write_reg(device, AL5887_REG_DEVICE_CONFIG1, AL5887_DITHER_EN);
        TEST_ASSERT_EQUAL(ESP_OK, ret);
    }
}