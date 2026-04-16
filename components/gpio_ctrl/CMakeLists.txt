idf_component_register(
    SRCS "gpio_ctrl.c"
    INCLUDE_DIRS "."
    REQUIRES 
        board 
        esp_driver_gpio
)