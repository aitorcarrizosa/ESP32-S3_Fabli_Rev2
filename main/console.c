#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_console.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "sdkconfig.h"

#include "driver/uart.h"

#include "gpio_ctrl.h"
#include "sdcard_ctrl.h"
#include "i2c_bus.h"
#include "usb_ctrl.h"

/* Command handlers */
static int cmd_reset(int argc, char **argv);
static int cmd_gpio(int argc, char **argv);
static int cmd_sd(int argc, char **argv);
static int cmd_i2c(int argc, char **argv);
static int cmd_usb(int argc, char **argv);

/* Internal helpers */
static void register_commands(void);
static int uart_readline_echo(uart_port_t uart, char *out, int out_sz);

/* --------------------- Command registration --------------------- */
static void register_commands(void)
{
    esp_console_register_help_command();

    const esp_console_cmd_t cmd_reset_def = {
        .command = "reset",
        .help = "Reset the device",
        .hint = NULL,
        .func = &cmd_reset,
        .argtable = NULL,
    };

    const esp_console_cmd_t cmd_gpio_def = {
        .command = "gpio",
        .help = "GPIO utilities",
        .hint = NULL,
        .func = &cmd_gpio,
        .argtable = NULL,
    };

    const esp_console_cmd_t cmd_sd_def = {
        .command = "sd",
        .help = "SD card control",
        .hint = NULL,
        .func = &cmd_sd,
        .argtable = NULL,
    };

    const esp_console_cmd_t cmd_i2c_def = {
        .command = "i2c",
        .help = "I2C bus control and scan",
        .hint = NULL,
        .func = &cmd_i2c,
        .argtable = NULL,
    };

    const esp_console_cmd_t cmd_usb_def = {
        .command = "usb",
        .help = "USB Type-C controller and connector control",
        .hint = NULL,
        .func = &cmd_usb,
        .argtable = NULL,
    };

    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd_reset_def));
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd_gpio_def));
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd_sd_def));
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd_i2c_def));
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd_usb_def));
}

/* ------------------------- Reset command ------------------------ */
static int cmd_reset(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("System resetting...\r\n");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(50));
    esp_restart();
    return 0;
}

/* ------------------------- GPIO command ------------------------- */
static int cmd_gpio(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage:\r\n");
        printf("  gpio status\r\n");
        return 0;
    }
    
    gpio_ctrl_print_status();
    return 0;
}

/* ---------------------- UART readline echo ---------------------- */
static int uart_readline_echo(uart_port_t uart, char *out, int out_sz)
{
    int n = 0;

    while (1) {
        uint8_t c;
        int r = uart_read_bytes(uart, &c, 1, pdMS_TO_TICKS(50));
        if (r <= 0) {
            continue;
        }

        if (c == '\r' || c == '\n') {
            const char *nl = "\r\n";
            uart_write_bytes(uart, nl, 2);
            out[n] = 0;
            return n;
        }

        if (c == 0x08 || c == 0x7F) {
            if (n > 0) {
                n--;
                const char bs_seq[3] = {'\b', ' ', '\b'};
                uart_write_bytes(uart, bs_seq, 3);
            }
            continue;
        }

        if (c < 0x20) {
            continue;
        }

        if (n < (out_sz - 1)) {
            out[n++] = (char)c;
            uart_write_bytes(uart, (const char *)&c, 1);
        }
    }
}

void console_start(void)
{
#if CONFIG_FABLI_CONSOLE_UART
    const uart_port_t console_uart = (uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM;

    const uart_config_t uart_cfg = {
        .baud_rate = CONFIG_ESP_CONSOLE_UART_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_driver_install(console_uart, 1024, 0, 0, NULL, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(err);
    }

    ESP_ERROR_CHECK(uart_param_config(console_uart, &uart_cfg));

    esp_console_config_t console_cfg = {
        .max_cmdline_args = 8,
        .max_cmdline_length = 256,
    };

    err = esp_console_init(&console_cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(err);
    }

    register_commands();

    char line[256];

    vTaskDelay(pdMS_TO_TICKS(100));
    printf("\r\nESP32-S3_Fabli_Rev2> ");
    fflush(stdout);

    while (true) {
        int len = uart_readline_echo(console_uart, line, sizeof(line));
        if (len <= 0) {
            printf("ESP32-S3_Fabli_Rev2> ");
            fflush(stdout);
            continue;
        }

        int ret = 0;
        err = esp_console_run(line, &ret);
        if (err == ESP_ERR_NOT_FOUND) {
            printf("Unknown command\r\n");
        } else if (err == ESP_ERR_INVALID_ARG) {
            printf("Invalid args\r\n");
        } else if (err != ESP_OK) {
            printf("Console error: %s\r\n", esp_err_to_name(err));
        }

        printf("ESP32-S3_Fabli_Rev2> ");
        fflush(stdout);
    }
#elif CONFIG_FABLI_CONSOLE_USB
    esp_err_t err;

    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "ESP32-S3_Fabli_Rev2> ";
    repl_cfg.max_cmdline_length = 256;

    esp_console_dev_usb_serial_jtag_config_t usb_cfg = { 0 };
    esp_console_repl_t *repl = NULL;

    err = esp_console_new_repl_usb_serial_jtag(&usb_cfg, &repl_cfg, &repl);
    ESP_ERROR_CHECK(err);

    register_commands();

    err = esp_console_start_repl(repl);
    ESP_ERROR_CHECK(err);

#else
#error "No console selected"
#endif
}

/* ------------------------- SD card ------------------------- */
static int cmd_sd(int argc, char **argv)
{
    esp_err_t err;

    if (argc < 2) {
        printf("Usage:\r\n");
        printf("  sd mount\r\n");
        printf("  sd unmount\r\n");
        printf("  sd info\r\n");
        return 0;
    }

    if (!strcmp(argv[1], "mount")) {
        err = sdcard_ctrl_mount();
        if (err == ESP_OK) {
            printf("SD card mounted\r\n");
        } else {
            printf("SD mount failed: %s\r\n", esp_err_to_name(err));
        }
        return 0;
    }

    if (!strcmp(argv[1], "unmount")) {
        err = sdcard_ctrl_unmount();
        if (err == ESP_OK) {
            printf("SD card unmounted\r\n");
        } else {
            printf("SD unmount failed: %s\r\n", esp_err_to_name(err));
        }
        return 0;
    }

    if (!strcmp(argv[1], "info")) {
        err = sdcard_ctrl_print_info();
        if (err != ESP_OK) {
            printf("SD info failed: %s\r\n", esp_err_to_name(err));
        }
        return 0;
    }

    printf("Unknown sd subcommand\r\n");
    return 0;
}

/* ------------------------- I2C ------------------------- */
static int cmd_i2c(int argc, char **argv)
{
    esp_err_t err;

    if (argc < 2) {
        printf("Usage:\r\n");
        printf("  i2c scan <0|1>\r\n");
        printf("  i2c probe <0|1> <addr>\r\n");
        printf("Examples:\r\n");
        printf("  i2c scan 0\r\n");
        printf("  i2c probe 1 0x20\r\n");
        return 0;
    }

    if (!strcmp(argv[1], "scan")) {
        if (argc < 3) {
            printf("Usage: i2c scan <0|1>\r\n");
            return 0;
        }

        int bus_num = atoi(argv[2]);
        if (bus_num != 0 && bus_num != 1) {
            printf("Invalid bus. Use 0 or 1\r\n");
            return 0;
        }

        err = i2c_bus_scan((bus_num == 0) ? I2C_BUS_0 : I2C_BUS_1);
        if (err != ESP_OK) {
            printf("I2C scan failed: %s\r\n", esp_err_to_name(err));
        }
        return 0;
    }

    if (!strcmp(argv[1], "probe")) {
        if (argc < 4) {
            printf("Usage: i2c probe <0|1> <addr>\r\n");
            printf("Example: i2c probe 0 0x18\r\n");
            return 0;
        }

        int bus_num = atoi(argv[2]);
        if (bus_num != 0 && bus_num != 1) {
            printf("Invalid bus. Use 0 or 1\r\n");
            return 0;
        }

        char *endptr = NULL;
        long addr = strtol(argv[3], &endptr, 0);
        if ((endptr == argv[3]) || (addr < 0x08) || (addr > 0x77)) {
            printf("Invalid I2C address. Use 0x08..0x77\r\n");
            return 0;
        }

        err = i2c_bus_probe((bus_num == 0) ? I2C_BUS_0 : I2C_BUS_1, (uint8_t)addr);
        if (err == ESP_OK) {
            printf("Device found on bus %d at address 0x%02lX\r\n", bus_num, addr);
        } else {
            printf("No response on bus %d at address 0x%02lX (%s)\r\n",
                   bus_num, addr, esp_err_to_name(err));
        }
        return 0;
    }

    printf("Unknown i2c subcommand\r\n");
    return 0;
}

/* ------------------------- USB ------------------------- */
static int cmd_usb(int argc, char **argv)
{
    esp_err_t err;

    if (argc < 2) {
        printf("Usage:\r\n");
        printf("  usb status\r\n");
        printf("  usb mode usb\r\n");
        printf("  usb mode audio\r\n");
        return 0;
    }

    if (!strcmp(argv[1], "status")) {
        err = usb_ctrl_print_status();
        if (err != ESP_OK) {
            printf("USB status failed: %s\r\n", esp_err_to_name(err));
        }
        return 0;
    }

    if (!strcmp(argv[1], "mode")) {
        if (argc < 3) {
            printf("Usage: usb mode <usb|audio>\r\n");
            return 0;
        }

        if (!strcmp(argv[2], "usb")) {
            err = usb_ctrl_set_mode_usb();
            if (err == ESP_OK) {
                printf("USB connector set to USB mode\r\n");
            } else {
                printf("Failed to set USB mode: %s\r\n", esp_err_to_name(err));
            }
            return 0;
        }

        if (!strcmp(argv[2], "audio")) {
            err = usb_ctrl_set_mode_audio();
            if (err == ESP_OK) {
                printf("USB connector set to audio mode\r\n");
            } else {
                printf("Failed to set audio mode: %s\r\n", esp_err_to_name(err));
            }
            return 0;
        }
    }

    printf("Unknown usb subcommand\r\n");
    return 0;
}