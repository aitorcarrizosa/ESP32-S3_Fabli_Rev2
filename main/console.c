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

/* ------------------------- Info command ------------------------- */
static int cmd_info(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("ESP32-S3_Fabli_Rev2 console OK\r\n");
    return 0;
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
        printf("  gpio pwr on|off\r\n");
        printf("  gpio led_en on|off\r\n");
        printf("  gpio led_rst release|assert\r\n");
        return 0;
    }

    if (!strcmp(argv[1], "status")) {
        gpio_ctrl_print_status();
        return 0;
    }

    if (argc < 3) {
        printf("Missing argument\r\n");
        return 0;
    }

    if (!strcmp(argv[1], "pwr")) {
        if (!strcmp(argv[2], "on")) {
            gpio_ctrl_set_pwr_on(true);
        } else if (!strcmp(argv[2], "off")) {
            gpio_ctrl_set_pwr_on(false);
        } else {
            printf("Usage: gpio pwr on|off\r\n");
        }
        return 0;
    }

    if (!strcmp(argv[1], "led_en")) {
        if (!strcmp(argv[2], "on")) {
            gpio_ctrl_set_led_enable(true);
        } else if (!strcmp(argv[2], "off")) {
            gpio_ctrl_set_led_enable(false);
        } else {
            printf("Usage: gpio led_en on|off\r\n");
        }
        return 0;
    }

    if (!strcmp(argv[1], "led_rst")) {
        if (!strcmp(argv[2], "release")) {
            gpio_ctrl_set_led_reset(true);
        } else if (!strcmp(argv[2], "assert")) {
            gpio_ctrl_set_led_reset(false);
        } else {
            printf("Usage: gpio led_rst release|assert\r\n");
        }
        return 0;
    }

    printf("Unknown gpio subcommand\r\n");
    return 0;
}

/* --------------------- Command registration --------------------- */
static void register_commands(void)
{
    esp_console_register_help_command();

    const esp_console_cmd_t cmd_info_def = {
        .command = "info",
        .help = "Print basic info",
        .hint = NULL,
        .func = &cmd_info,
        .argtable = NULL,
    };

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

    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd_info_def));
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd_reset_def));
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd_gpio_def));
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

void console_start_uart0(void)
{
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
}