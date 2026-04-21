idf_component_register(
    SRCS "led_ctrl.c"
    INCLUDE_DIRS "."
    REQUIRES 
        board 
        gpio_ctrl
        i2c_bus
        unity
)