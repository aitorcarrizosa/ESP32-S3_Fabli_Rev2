idf_component_register(
    SRCS "usb_ctrl.c"
    INCLUDE_DIRS "."
    REQUIRES 
        board 
        gpio_ctrl 
)