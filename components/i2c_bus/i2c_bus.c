#include "i2c_bus.h"

#include <stdio.h>

#include "board.h"

#include "esp_log.h"

static const char *TAG = "i2c_bus";

#define I2C_BUS0_PORT              0
#define I2C_BUS1_PORT              1
#define I2C_GLITCH_IGNORE_CNT      7
#define I2C_TRANS_QUEUE_DEPTH      8
#define I2C_DEFAULT_FREQ_HZ        400000
#define I2C_PROBE_TIMEOUT_MS       50

static i2c_master_bus_handle_t s_i2c_bus0_handle = NULL;
static i2c_master_bus_handle_t s_i2c_bus1_handle = NULL;
static bool s_i2c_bus0_initialized = false;
static bool s_i2c_bus1_initialized = false;

static esp_err_t i2c_bus_init_single(i2c_bus_id_t bus_id)
{
    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = (bus_id == I2C_BUS_0) ? I2C_BUS0_PORT : I2C_BUS1_PORT,
        .sda_io_num = (bus_id == I2C_BUS_0) ? PIN_I2C0_SDA : PIN_I2C1_SDA,
        .scl_io_num = (bus_id == I2C_BUS_0) ? PIN_I2C0_SCL : PIN_I2C1_SCL,
        .glitch_ignore_cnt = I2C_GLITCH_IGNORE_CNT,
        .flags.enable_internal_pullup = true,
    };

    i2c_master_bus_handle_t *bus_handle =
        (bus_id == I2C_BUS_0) ? &s_i2c_bus0_handle : &s_i2c_bus1_handle;

    bool *initialized =
        (bus_id == I2C_BUS_0) ? &s_i2c_bus0_initialized : &s_i2c_bus1_initialized;

    if (*initialized) {
        ESP_LOGW(TAG, "I2C bus %d already initialized", bus_id);
        return ESP_OK;
    }

    esp_err_t ret = i2c_new_master_bus(&bus_config, bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init I2C bus %d: %s", bus_id, esp_err_to_name(ret));
        return ret;
    }

    *initialized = true;

    ESP_LOGI(TAG,
             "I2C bus %d initialized (SDA=%d, SCL=%d, Freq=%d Hz)",
             bus_id,
             bus_config.sda_io_num,
             bus_config.scl_io_num,
             I2C_DEFAULT_FREQ_HZ);

    return ESP_OK;
}

esp_err_t i2c_bus_init(void)
{
    esp_err_t ret;

    ret = i2c_bus_init_single(I2C_BUS_0);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = i2c_bus_init_single(I2C_BUS_1);
    if (ret != ESP_OK) {
        return ret;
    }

    return ESP_OK;
}

bool i2c_bus_is_initialized(i2c_bus_id_t bus_id)
{
    if (bus_id == I2C_BUS_0) {
        return s_i2c_bus0_initialized;
    }

    if (bus_id == I2C_BUS_1) {
        return s_i2c_bus1_initialized;
    }

    return false;
}

i2c_master_bus_handle_t i2c_bus_get_handle(i2c_bus_id_t bus_id)
{
    if (bus_id == I2C_BUS_0) {
        return s_i2c_bus0_handle;
    }

    if (bus_id == I2C_BUS_1) {
        return s_i2c_bus1_handle;
    }

    return NULL;
}

esp_err_t i2c_bus_probe(i2c_bus_id_t bus_id, uint8_t address)
{
    i2c_master_bus_handle_t bus_handle = i2c_bus_get_handle(bus_id);
    if (bus_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    return i2c_master_probe(bus_handle, address, I2C_PROBE_TIMEOUT_MS);
}

esp_err_t i2c_bus_scan(i2c_bus_id_t bus_id)
{
    if (!i2c_bus_is_initialized(bus_id)) {
        ESP_LOGW(TAG, "I2C bus %d is not initialized", bus_id);
        return ESP_ERR_INVALID_STATE;
    }

    printf("\r\n");
    printf("Scanning I2C bus %d...\r\n", bus_id);
    printf("     ");

    for (int col = 0; col < 16; col++) {
        printf("%02X ", col);
    }
    printf("\r\n");

    int devices_found = 0;

    for (int row = 0; row < 8; row++) {
        printf("%02X: ", row << 4);

        for (int col = 0; col < 16; col++) {
            uint8_t addr = (row << 4) | col;

            if (addr < 0x08 || addr > 0x77) {
                printf("-- ");
                continue;
            }

            esp_err_t ret = i2c_bus_probe(bus_id, addr);
            if (ret == ESP_OK) {
                printf("%02X ", addr);
                devices_found++;
            } else {
                printf("-- ");
            }
        }

        printf("\r\n");
    }

    printf("Devices found on I2C bus %d: %d\r\n", bus_id, devices_found);
    printf("\r\n");

    return ESP_OK;
}

esp_err_t i2c_bus_read_reg(i2c_bus_id_t bus, uint8_t dev_addr, uint8_t reg, uint8_t *value)
{
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_master_bus_handle_t bus_handle = i2c_bus_get_handle(bus);
    if (bus_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = dev_addr,
        .scl_speed_hz = 100000,
    };

    i2c_master_dev_handle_t dev_handle = NULL;
    esp_err_t ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle);
    if (ret != ESP_OK) {
        return ret;
    }

    uint8_t reg_buf = reg;

    ret = i2c_master_transmit_receive(
        dev_handle,
        &reg_buf,
        1,
        value,
        1,
        100
    );

    esp_err_t remove_ret = i2c_master_bus_rm_device(dev_handle);
    if (ret == ESP_OK && remove_ret != ESP_OK) {
        return remove_ret;
    }

    return ret;
}

esp_err_t i2c_bus_write_reg(i2c_bus_id_t bus, uint8_t dev_addr, uint8_t reg, uint8_t value)
{
    i2c_master_bus_handle_t bus_handle = i2c_bus_get_handle(bus);
    if (bus_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = dev_addr,
        .scl_speed_hz = 100000,
    };

    i2c_master_dev_handle_t dev_handle = NULL;
    esp_err_t ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle);
    if (ret != ESP_OK) {
        return ret;
    }

    uint8_t buf[2] = { reg, value };

    ret = i2c_master_transmit(
        dev_handle,
        buf,
        sizeof(buf),
        100
    );

    esp_err_t remove_ret = i2c_master_bus_rm_device(dev_handle);
    if (ret == ESP_OK && remove_ret != ESP_OK) {
        return remove_ret;
    }

    return ret;
}