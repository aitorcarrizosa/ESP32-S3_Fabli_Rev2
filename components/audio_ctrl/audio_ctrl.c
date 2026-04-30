#include "audio_ctrl.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_log.h"
#include "unity.h"

#include "driver/i2s.h"

#include "board.h"
#include "i2c_bus.h"

/* -------------------------------------------------------------------------- */
/* Defines                                                                    */
/* -------------------------------------------------------------------------- */
#define AUDIO_CTRL_I2C_BUS          I2C_BUS_0
#define AUDIO_CTRL_I2C_ADDR         0x69    /* 7-bit address. Rev1 used 0xD2 as 8-bit address */

#define TSCS42_REG_DACVOLL          0x04
#define TSCS42_REG_DACVOLR          0x05
#define TSCS42_REG_INVOLL           0x08
#define TSCS42_REG_INVOLR           0x09
#define TSCS42_REG_INSELL           0x0C
#define TSCS42_REG_INSELR           0x0D
#define TSCS42_REG_AIC1             0x13
#define TSCS42_REG_AIC2             0x14
#define TSCS42_REG_CNVRTR0          0x16
#define TSCS42_REG_ADCSR            0x17
#define TSCS42_REG_CNVRTR1          0x18
#define TSCS42_REG_DACSR            0x19
#define TSCS42_REG_PWRM1            0x1A
#define TSCS42_REG_PWRM2            0x1B
#define TSCS42_REG_CONFIG1          0x20

#define PWRM1_DIGENB                (1 << 0)
#define PWRM1_MICB                  (1 << 1)
#define PWRM1_PGAL                  (1 << 4)
#define PWRM1_PGAR                  (1 << 5)
#define PWRM1_ADCL                  (1 << 6)
#define PWRM1_ADCR                  (1 << 7)

#define PWRM2_VREF                  (1 << 0)
#define PWRM2_SPKL                  (1 << 3)
#define PWRM2_SPKR                  (1 << 4)
#define PWRM2_HPL                   (1 << 5)
#define PWRM2_HPR                   (1 << 6)

#define SAMPLES_PER_CYCLE           48

/* -------------------------------------------------------------------------- */
/* Local data                                                                 */
/* -------------------------------------------------------------------------- */
static const char *TAG = "audio_ctrl";

static audio_ctrl_handle_t codec = {
    .config = {0},
    .volume = 0,
    .is_initialized = false
};

static audio_ctrl_config_t codec_config = {
    .i2s_port = I2S_NUM_0,
    .sample_rate = 48000,
    .bit_depth = 16
};

/* 1 kHz sine wave, 48 kHz sample rate */
static const int16_t test_tone[SAMPLES_PER_CYCLE] = {
    0, 4276, 8480, 12539, 16383, 19947, 23170, 25995,
    28377, 30273, 31650, 32486, 32767, 32486, 31650, 30273,
    28377, 25995, 23170, 19947, 16383, 12539, 8480, 4276,
    0, -4276, -8480, -12539, -16383, -19947, -23170, -25995,
    -28377, -30273, -31650, -32486, -32767, -32486, -31650, -30273,
    -28377, -25995, -23170, -19947, -16383, -12539, -8480, -4276
};

/* -------------------------------------------------------------------------- */
/* Internal prototypes                                                        */
/* -------------------------------------------------------------------------- */
static esp_err_t play_test_tone(void);
static void test_playback_control(void);
static void test_playback_volume_sequence(void);
static void test_rapid_start_stop(void);

/* -------------------------------------------------------------------------- */
/* I2C register access                                                        */
/* -------------------------------------------------------------------------- */
static esp_err_t audio_ctrl_write_reg(audio_ctrl_handle_t *handle, uint8_t reg, uint8_t value)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return i2c_bus_write_reg(AUDIO_CTRL_I2C_BUS, AUDIO_CTRL_I2C_ADDR, reg, value);
}

static esp_err_t audio_ctrl_read_reg(audio_ctrl_handle_t *handle, uint8_t reg, uint8_t *value)
{
    if (handle == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return i2c_bus_read_reg(AUDIO_CTRL_I2C_BUS, AUDIO_CTRL_I2C_ADDR, reg, value);
}

/* -------------------------------------------------------------------------- */
/* Initialization                                                             */
/* -------------------------------------------------------------------------- */
esp_err_t audio_ctrl_init(void)
{
    if (codec.is_initialized) {
        return ESP_OK;
    }

    codec.config = codec_config;
    codec.volume = 50;
    codec.is_initialized = false;

    i2s_config_t i2s_config = {
        .mode = I2S_MODE_MASTER | I2S_MODE_TX,
        .sample_rate = codec.config.sample_rate,
        .bits_per_sample = codec.config.bit_depth,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .dma_buf_count = 8,
        .dma_buf_len = 64,
        .use_apll = true,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1
    };

    ESP_ERROR_CHECK(i2s_driver_install(codec.config.i2s_port, &i2s_config, 0, NULL));

    i2s_pin_config_t pin_config = {
        .mck_io_num = PIN_CODEC_MCLK,
        .bck_io_num = PIN_CODEC_BCLK,
        .ws_io_num = PIN_CODEC_LRCK,
        .data_out_num = PIN_CODEC_DIN,
        .data_in_num = I2S_PIN_NO_CHANGE
    };

    ESP_ERROR_CHECK(i2s_set_pin(codec.config.i2s_port, &pin_config));

    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_ERROR_CHECK(audio_ctrl_write_reg(&codec, TSCS42_REG_PWRM1,
        PWRM1_DIGENB | PWRM1_MICB | PWRM1_PGAL | PWRM1_PGAR |
        PWRM1_ADCL | PWRM1_ADCR));

    ESP_ERROR_CHECK(audio_ctrl_write_reg(&codec, TSCS42_REG_PWRM2,
        PWRM2_VREF | PWRM2_SPKL | PWRM2_SPKR | PWRM2_HPL | PWRM2_HPR));

    ESP_ERROR_CHECK(audio_ctrl_write_reg(&codec, TSCS42_REG_AIC1, 0x02));

    ESP_ERROR_CHECK(audio_ctrl_write_reg(&codec, TSCS42_REG_DACVOLL, 0x7F));
    ESP_ERROR_CHECK(audio_ctrl_write_reg(&codec, TSCS42_REG_DACVOLR, 0x7F));

    ESP_ERROR_CHECK(audio_ctrl_write_reg(&codec, TSCS42_REG_DACSR, 0x0C));

    codec.is_initialized = true;

    ESP_LOGI(TAG, "TSCS42 codec initialized successfully");
    return ESP_OK;
}

/* -------------------------------------------------------------------------- */
/* Playback control                                                           */
/* -------------------------------------------------------------------------- */
esp_err_t audio_ctrl_start(void)
{
    if (!codec.is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_ERROR_CHECK(i2s_start(codec.config.i2s_port));

    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t cnvrtr1_val = 0;
    ESP_ERROR_CHECK(audio_ctrl_read_reg(&codec, TSCS42_REG_CNVRTR1, &cnvrtr1_val));

    cnvrtr1_val &= ~(1 << 3); /* Clear DACMU bit */

    ESP_ERROR_CHECK(audio_ctrl_write_reg(&codec, TSCS42_REG_CNVRTR1, cnvrtr1_val));

    ESP_LOGI(TAG, "Playback started");
    return ESP_OK;
}

esp_err_t audio_ctrl_stop(void)
{
    if (!codec.is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t cnvrtr1_val = 0;

    ESP_ERROR_CHECK(audio_ctrl_read_reg(&codec, TSCS42_REG_CNVRTR1, &cnvrtr1_val));

    cnvrtr1_val |= (1 << 3); /* Set DACMU bit */

    ESP_ERROR_CHECK(audio_ctrl_write_reg(&codec, TSCS42_REG_CNVRTR1, cnvrtr1_val));
    ESP_ERROR_CHECK(i2s_stop(codec.config.i2s_port));

    ESP_LOGI(TAG, "Playback stopped");
    return ESP_OK;
}

/* -------------------------------------------------------------------------- */
/* Volume                                                                     */
/* -------------------------------------------------------------------------- */
esp_err_t audio_ctrl_set_volume(uint8_t volume)
{
    if (!codec.is_initialized || volume > 100) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t codec_vol = (volume * 255) / 100;

    ESP_ERROR_CHECK(audio_ctrl_write_reg(&codec, TSCS42_REG_DACVOLL, codec_vol));
    ESP_ERROR_CHECK(audio_ctrl_write_reg(&codec, TSCS42_REG_DACVOLR, codec_vol));

    codec.volume = volume;

    ESP_LOGI(TAG, "Volume set to %d%%", volume);
    return ESP_OK;
}

/* -------------------------------------------------------------------------- */
/* Status                                                                     */
/* -------------------------------------------------------------------------- */
void audio_ctrl_print_status(void)
{
    bool codec_present = (i2c_bus_probe(AUDIO_CTRL_I2C_BUS, AUDIO_CTRL_I2C_ADDR) == ESP_OK);

    printf("\r\n");
    printf("Audio status:\r\n");
    printf("  CODEC_I2C      = %s\r\n", codec_present ? "OK" : "NOT DETECTED");
    printf("  INIT_DONE      = %d\r\n", codec.is_initialized ? 1 : 0);
    printf("  I2S_PORT       = %d\r\n", codec.config.i2s_port);
    printf("  SAMPLE_RATE    = %lu\r\n", codec.config.sample_rate);
    printf("  BIT_DEPTH      = %u\r\n", codec.config.bit_depth);
    printf("  VOLUME         = %u%%\r\n", codec.volume);
    printf("\r\n");
}

/* -------------------------------------------------------------------------- */
/* Test tone                                                                  */
/* -------------------------------------------------------------------------- */
static esp_err_t play_test_tone(void)
{
    size_t bytes_written = 0;

    for (int i = 0; i < 100; i++) {
        TEST_ASSERT_EQUAL(ESP_OK,
            i2s_write(codec_config.i2s_port, test_tone, sizeof(test_tone),
                      &bytes_written, portMAX_DELAY));

        TEST_ASSERT_EQUAL(sizeof(test_tone), bytes_written);
    }

    return ESP_OK;
}

/* -------------------------------------------------------------------------- */
/* Unity tests                                                                */
/* -------------------------------------------------------------------------- */
void audio_ctrl_run_test(void)
{
    audio_ctrl_init();

    UNITY_BEGIN();
    RUN_TEST(test_playback_control);
    RUN_TEST(test_playback_volume_sequence);
    RUN_TEST(test_rapid_start_stop);
    UNITY_END();
}

/**
 * @brief Tests playback control by starting and stopping audio playback.
 * @note Starts playback, plays a test tone, then stops playback.
 */
static void test_playback_control(void)
{
    ESP_LOGI(TAG, "Testing playback control...");

    TEST_ASSERT_EQUAL(ESP_OK, audio_ctrl_start());
    TEST_ASSERT_EQUAL(ESP_OK, play_test_tone());
    TEST_ASSERT_EQUAL(ESP_OK, audio_ctrl_stop());
}

/**
 * @brief Tests playback at various volume levels.
 * @note Starts playback, changes volume through several levels and plays a test tone at each level.
 */
static void test_playback_volume_sequence(void)
{
    ESP_LOGI(TAG, "Testing playback with volume changes...");

    TEST_ASSERT_EQUAL(ESP_OK, audio_ctrl_start());

    const uint8_t test_volumes[] = {25, 50, 75, 100};

    for (int i = 0; i < sizeof(test_volumes) / sizeof(test_volumes[0]); i++) {
        TEST_ASSERT_EQUAL(ESP_OK, audio_ctrl_set_volume(test_volumes[i]));

        ESP_LOGI(TAG, "Playing at volume %d%%", test_volumes[i]);

        TEST_ASSERT_EQUAL(ESP_OK, play_test_tone());

        vTaskDelay(pdMS_TO_TICKS(100));
    }

    TEST_ASSERT_EQUAL(ESP_OK, audio_ctrl_stop());
}

/**
 * @brief Tests rapid start and stop of audio playback.
 * @note Repeats start/stop playback several times to validate stability.
 */
static void test_rapid_start_stop(void)
{
    ESP_LOGI(TAG, "Testing rapid start/stop sequences...");

    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL(ESP_OK, audio_ctrl_start());
        vTaskDelay(pdMS_TO_TICKS(100));

        TEST_ASSERT_EQUAL(ESP_OK, audio_ctrl_stop());
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}