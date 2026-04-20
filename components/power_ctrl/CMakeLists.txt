idf_component_register(
    SRCS "power_ctrl.c"
    INCLUDE_DIRS "."
    REQUIRES
        board
        gpio_ctrl
        esp_adc
)