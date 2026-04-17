idf_component_register(
    SRCS "main.c" "console.c"
    INCLUDE_DIRS "."
    REQUIRES 
        board 
        console
        vfs
        esp_driver_uart
        gpio_ctrl
        sdcard_ctrl
        i2c_bus
)
