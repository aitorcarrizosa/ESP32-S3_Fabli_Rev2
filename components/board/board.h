#pragma once

#include "sdkconfig.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PIN_PWR_ON          ((gpio_num_t)CONFIG_BOARD_PIN_PWR_ON)
#define PIN_LED_EN          ((gpio_num_t)CONFIG_BOARD_PIN_LED_EN)
#define PIN_LED_RESET_N     ((gpio_num_t)CONFIG_BOARD_PIN_LED_RESET_N)

#define PIN_HP_DET          ((gpio_num_t)CONFIG_BOARD_PIN_HP_DET)
#define PIN_AC_NOK          ((gpio_num_t)CONFIG_BOARD_PIN_AC_NOK)
#define PIN_CHG_OK          ((gpio_num_t)CONFIG_BOARD_PIN_CHG_OK)
#define PIN_LED_FAULT1_N    ((gpio_num_t)CONFIG_BOARD_PIN_LED_FAULT1_N)
#define PIN_LED_FAULT2_N    ((gpio_num_t)CONFIG_BOARD_PIN_LED_FAULT2_N)

#define PIN_KEY_INT_N       ((gpio_num_t)CONFIG_BOARD_PIN_KEY_INT_N)
#define PIN_FUSB_INT_N      ((gpio_num_t)CONFIG_BOARD_PIN_FUSB_INT_N)
#define PIN_MEMS_INT1       ((gpio_num_t)CONFIG_BOARD_PIN_MEMS_INT1)

#define PIN_VBAT_ADC        ((gpio_num_t)CONFIG_BOARD_PIN_VBAT_ADC)
#define PIN_REV_ADC         ((gpio_num_t)CONFIG_BOARD_PIN_REV_ADC)

#define PIN_I2C0_SDA        ((gpio_num_t)CONFIG_BOARD_PIN_I2C0_SDA)
#define PIN_I2C0_SCL        ((gpio_num_t)CONFIG_BOARD_PIN_I2C0_SCL)
#define PIN_I2C1_SDA        ((gpio_num_t)CONFIG_BOARD_PIN_I2C1_SDA)
#define PIN_I2C1_SCL        ((gpio_num_t)CONFIG_BOARD_PIN_I2C1_SCL)

#define PIN_DEBUG_TX        ((gpio_num_t)CONFIG_BOARD_PIN_DEBUG_TX)
#define PIN_DEBUG_RX        ((gpio_num_t)CONFIG_BOARD_PIN_DEBUG_RX)

#define PIN_ENCODER_A       ((gpio_num_t)CONFIG_BOARD_PIN_ENCODER_A)
#define PIN_ENCODER_B       ((gpio_num_t)CONFIG_BOARD_PIN_ENCODER_B)
#define PIN_ENCODER_SW_N    ((gpio_num_t)CONFIG_BOARD_PIN_ENCODER_SW_N)

#define BOARD_SD_D0_PIN     ((gpio_num_t)CONFIG_BOARD_SD_D0_PIN)
#define BOARD_SD_D1_PIN     ((gpio_num_t)CONFIG_BOARD_SD_D1_PIN)
#define BOARD_SD_D2_PIN     ((gpio_num_t)CONFIG_BOARD_SD_D2_PIN)
#define BOARD_SD_D3_PIN     ((gpio_num_t)CONFIG_BOARD_SD_D3_PIN)
#define BOARD_SD_CMD_PIN    ((gpio_num_t)CONFIG_BOARD_SD_CMD_PIN)
#define BOARD_SD_CLK_PIN    ((gpio_num_t)CONFIG_BOARD_SD_CLK_PIN)

void board_init(void);

#ifdef __cplusplus
}
#endif