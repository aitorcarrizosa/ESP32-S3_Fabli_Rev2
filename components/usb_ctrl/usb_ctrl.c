#include "usb_ctrl.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#include "board.h"
#include "esp_err.h"
#include "esp_log.h"
#include "gpio_ctrl.h"
#include "i2c_bus.h"

static const char *TAG = "usb_ctrl";

/* -------------------------------------------------------------------------- */
/* FUSB303 register map and bit definitions                                   */
/* -------------------------------------------------------------------------- */
#define USB_CTRL_I2C_BUS        I2C_BUS_0        //I2C bus 0
#define USB_CTRL_I2C_ADDR       0x42             //7-bit I2C address as 0x42
#define FUSB303_REG_DEVICE_ID   0x01
#define FUSB303_REG_DEVICE_TYPE 0x02
#define FUSB303_REG_PORTROLE    0x03
#define FUSB303_REG_CONTROL     0x04
#define FUSB303_REG_CONTROL1    0x05
#define FUSB303_REG_STATUS      0x11
#define FUSB303_REG_STATUS1     0x12
#define FUSB303_REG_TYPE        0x13
#define FUSB303_REG_INTERRUPT   0x14
#define FUSB303_REG_INTERRUPT1  0x15

/*
 * INT_MASK is the global interrupt mask bit.
 * 1 = all interrupts masked
 * 0 = interrupt pin can assert on enabled events
 */
#define FUSB303_CONTROL_INT_MASK        (1U << 0)

/*
 * CONTROL1 register bits. ENABLE must be set in I2C mode for the device to operate.
 */
#define FUSB303_CONTROL1_ENABLE         (1U << 3)

/*
 * STATUS register bits.
 */
#define FUSB303_STATUS_ATTACH           (1U << 0)
#define FUSB303_STATUS_BC_LVL_MASK      (0x3U << 1)
#define FUSB303_STATUS_VBUSOK           (1U << 3)
#define FUSB303_STATUS_ORIENT_MASK      (0x3U << 4)
#define FUSB303_STATUS_ORIENT_SHIFT     4
#define FUSB303_STATUS_VSAFE0V          (1U << 6)
#define FUSB303_STATUS_AUTOSNK          (1U << 7)

/*
 * TYPE register bits.
 */
#define FUSB303_TYPE_AUDIO              (1U << 0)
#define FUSB303_TYPE_AUDIOVBUS          (1U << 1)
#define FUSB303_TYPE_ACTIVECABLE        (1U << 2)
#define FUSB303_TYPE_SOURCE             (1U << 3)
#define FUSB303_TYPE_SINK               (1U << 4)
#define FUSB303_TYPE_DEBUGSNK           (1U << 5)
#define FUSB303_TYPE_DEBUGSRC           (1U << 6)

/*
 * Flag indicating usb_ctrl_init() has already run.
 */
static bool s_initialized = false;

/* -------------------------------------------------------------------------- */
/* Local helpers                                                              */
/* -------------------------------------------------------------------------- */
/*
 * - If AC_nOK = 0, valid input power is present -> enable USB_SRC
 * - If AC_nOK = 1, no valid input power        -> disable USB_SRC
 */
static esp_err_t usb_ctrl_update_usb_src(void)
{
    bool usb_valid = !gpio_ctrl_get_ac_nok();
    return gpio_ctrl_set_usb_src(usb_valid);
}

/*
 * Convert the ORIENT[1:0] field from the STATUS register into readable text.
 *
 * 00 = No or unresolved connection
 * 01 = Cable CC connected through CC1
 * 10 = Cable CC connected through CC2
 * 11 = Fault during detection
 */
static const char *usb_ctrl_orient_to_str(uint8_t orient)
{
    switch (orient) {
        case 0x0: return "NONE/UNRESOLVED";
        case 0x1: return "CC1";
        case 0x2: return "CC2";
        case 0x3: return "FAULT";
        default:  return "UNKNOWN";
    }
}

/*
 * Convert the TYPE register into a simple role/connection
 */
static const char *usb_ctrl_role_to_str(uint8_t type_reg)
{
    if (type_reg & FUSB303_TYPE_SOURCE) {
        return "SOURCE";
    }

    if (type_reg & FUSB303_TYPE_SINK) {
        return "SINK";
    }

    if (type_reg & FUSB303_TYPE_DEBUGSRC) {
        return "DEBUG_SOURCE";
    }

    if (type_reg & FUSB303_TYPE_DEBUGSNK) {
        return "DEBUG_SINK";
    }

    if (type_reg & FUSB303_TYPE_AUDIOVBUS) {
        return "AUDIO_WITH_VBUS";
    }

    if (type_reg & FUSB303_TYPE_AUDIO) {
        return "AUDIO";
    }

    return "NONE";
}

/* -------------------------------------------------------------------------- */
/* Raw register access                                                        */
/* -------------------------------------------------------------------------- */
static esp_err_t usb_ctrl_read_reg(uint8_t reg, uint8_t *value)     //Read one byte from a FUSB303 register over I2C.
{
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return i2c_bus_read_reg(USB_CTRL_I2C_BUS, USB_CTRL_I2C_ADDR, reg, value);
}

static esp_err_t usb_ctrl_write_reg(uint8_t reg, uint8_t value)     //Write one byte to a FUSB303 register over I2C.
{
    return i2c_bus_write_reg(USB_CTRL_I2C_BUS, USB_CTRL_I2C_ADDR, reg, value);
}

static bool usb_ctrl_fusb_present(void)     //Check if the FUSB303 is present on the I2C bus.
{
    return (i2c_bus_probe(USB_CTRL_I2C_BUS, USB_CTRL_I2C_ADDR) == ESP_OK);
}

/* -------------------------------------------------------------------------- */
/* FUSB303 configuration                                                      */
/* -------------------------------------------------------------------------- */
/*
 * In I2C mode, the datasheet requires CONTROL1.ENABLE = 1
 * for the device to operate when EN_N is externally low.
 */
static esp_err_t usb_ctrl_fusb_enable_i2c_mode(void)
{
    uint8_t control1 = 0;
    esp_err_t ret = usb_ctrl_read_reg(FUSB303_REG_CONTROL1, &control1);
    if (ret != ESP_OK) {
        return ret;
    }

    control1 |= FUSB303_CONTROL1_ENABLE;

    return usb_ctrl_write_reg(FUSB303_REG_CONTROL1, control1);
}

/*
 * By default, the global interrupt mask is set after reset/power-up.
 * This function clears INT_MASK so that INT_N can assert on interrupt events.
 */
static esp_err_t usb_ctrl_fusb_enable_interrupt_pin(void)
{
    uint8_t control = 0;
    esp_err_t ret = usb_ctrl_read_reg(FUSB303_REG_CONTROL, &control);
    if (ret != ESP_OK) {
        return ret;
    }

    control &= (uint8_t)(~FUSB303_CONTROL_INT_MASK);

    return usb_ctrl_write_reg(FUSB303_REG_CONTROL, control);
}

/* -------------------------------------------------------------------------- */
/* Initialization                                                             */
/* -------------------------------------------------------------------------- */

/*
 * Initialize the USB control block.
 *
 * What this function does:
 * 1. Sets the connector to USB mode by default
 * 2. Updates USB_SRC automatically from AC_nOK
 * 3. Checks whether the FUSB303 is present on I2C
 * 4. If present, enables the device in I2C mode
 * 5. Unmasks the interrupt pin
 */
esp_err_t usb_ctrl_init(void)
{
    esp_err_t ret;

    ret = usb_ctrl_set_mode_usb();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = usb_ctrl_update_usb_src();
    if (ret != ESP_OK) {
        return ret;
    }

    if (!usb_ctrl_fusb_present()) {
        ESP_LOGW(TAG, "FUSB303 not detected on I2C address 0x%02X", USB_CTRL_I2C_ADDR);
        return ESP_OK;
    }

    ret = usb_ctrl_fusb_enable_i2c_mode();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable FUSB303 in I2C mode: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = usb_ctrl_fusb_enable_interrupt_pin();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to unmask FUSB303 interrupt pin: %s", esp_err_to_name(ret));
        return ret;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "USB control initialized");
    return ESP_OK;
}

/* -------------------------------------------------------------------------- */
/* Connector mode control                                                     */
/* -------------------------------------------------------------------------- */
esp_err_t usb_ctrl_set_mode_usb(void)       //AUDIO_SEL = 0 -> USB mode
{
    return gpio_ctrl_set_audio_sel(false);
}

esp_err_t usb_ctrl_set_mode_audio(void)     //AUDIO_SEL = 1 -> audio mode
{
    return gpio_ctrl_set_audio_sel(true);
}

/* -------------------------------------------------------------------------- */
/* Status                                                                     */
/* -------------------------------------------------------------------------- */
void usb_ctrl_print_status(void)
{
    uint8_t dev_id = 0;
    uint8_t dev_type = 0;
    uint8_t status = 0;
    uint8_t type = 0;
    uint8_t intr = 0;

    bool audio_mode = gpio_ctrl_get_audio_sel();

    (void)usb_ctrl_update_usb_src();

    printf("\r\n");
    printf("USB status:\r\n");
    printf("  MODE        = %s\r\n", audio_mode ? "AUDIO" : "USB");
    printf("  USB_SRC     = %d\r\n", gpio_ctrl_get_usb_src() ? 1 : 0);
    printf("  AUDIO_SEL   = %d\r\n", gpio_ctrl_get_audio_sel() ? 1 : 0);
    printf("  FUSB_nINT   = %d\r\n", gpio_ctrl_get_fusb_int_n() ? 1 : 0);
    printf("  FUSB_ID     = %d\r\n", gpio_ctrl_get_fusb_id() ? 1 : 0);
    printf("  INIT_DONE   = %d\r\n", s_initialized ? 1 : 0);

    if (!usb_ctrl_fusb_present()) {
        printf("  FUSB I2C    = NOT DETECTED\r\n");
        printf("\r\n");
        return;
    }

    printf("  FUSB I2C    = OK\r\n");

    /*
     * DEVICE_ID: identifies device version/revision.
     */
    if (usb_ctrl_read_reg(FUSB303_REG_DEVICE_ID, &dev_id) == ESP_OK) {
        printf("  DEVICE_ID   = 0x%02X\r\n", dev_id);
    } else {
        printf("  DEVICE_ID   = read error\r\n");
    }

    /*
     * DEVICE_TYPE: should identify the chip family/type.
     */
    if (usb_ctrl_read_reg(FUSB303_REG_DEVICE_TYPE, &dev_type) == ESP_OK) {
        printf("  DEV_TYPE    = 0x%02X\r\n", dev_type);
    } else {
        printf("  DEV_TYPE    = read error\r\n");
    }

    if (usb_ctrl_read_reg(FUSB303_REG_STATUS, &status) == ESP_OK) {
        uint8_t orient = (status & FUSB303_STATUS_ORIENT_MASK) >> FUSB303_STATUS_ORIENT_SHIFT;
        uint8_t bc_lvl = (status & FUSB303_STATUS_BC_LVL_MASK) >> 1;

        printf("  STATUS      = 0x%02X\r\n", status);
        printf("  ATTACH      = %s\r\n", (status & FUSB303_STATUS_ATTACH) ? "YES" : "NO");
        printf("  ORIENT      = %s\r\n", usb_ctrl_orient_to_str(orient));
        printf("  VBUSOK      = %s\r\n", (status & FUSB303_STATUS_VBUSOK) ? "YES" : "NO");
        printf("  BC_LVL      = %u\r\n", bc_lvl);
        printf("  VSAFE0V     = %s\r\n", (status & FUSB303_STATUS_VSAFE0V) ? "YES" : "NO");
        printf("  AUTOSNK     = %s\r\n", (status & FUSB303_STATUS_AUTOSNK) ? "YES" : "NO");
    } else {
        printf("  STATUS      = read error\r\n");
    }

    if (usb_ctrl_read_reg(FUSB303_REG_TYPE, &type) == ESP_OK) {
        printf("  TYPE        = 0x%02X\r\n", type);
        printf("  ROLE        = %s\r\n", usb_ctrl_role_to_str(type));
    } else {
        printf("  TYPE        = read error\r\n");
    }

    if (usb_ctrl_read_reg(FUSB303_REG_INTERRUPT, &intr) == ESP_OK) {
        printf("  INTERRUPT   = 0x%02X\r\n", intr);
    } else {
        printf("  INTERRUPT   = read error\r\n");
    }

    printf("\r\n");
}