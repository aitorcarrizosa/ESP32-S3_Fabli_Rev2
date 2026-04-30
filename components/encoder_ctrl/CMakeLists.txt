idf_component_register(
    SRCS "encoder_ctrl.c"
    INCLUDE_DIRS "."
    REQUIRES 
        board 
        esp_driver_gpio
        gpio_ctrl
        unity
)