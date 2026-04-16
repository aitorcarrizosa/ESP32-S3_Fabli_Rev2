idf_component_register(
    SRCS "main.c" "console.c"
    INCLUDE_DIRS "."
    REQUIRES 
        board 
        gpio_ctrl
        sdcard_ctrl
        console
        vfs
        esp_driver_uart
)
