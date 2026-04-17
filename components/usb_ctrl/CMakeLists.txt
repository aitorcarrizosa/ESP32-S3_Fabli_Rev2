idf_component_register(
    SRCS "usb_ctrl.c"
    INCLUDE_DIRS "."
    REQUIRES 
        i2c_bus 
        gpio_ctrl board
)