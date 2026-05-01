idf_component_register(
    SRCS "acc_ctrl.c"
    INCLUDE_DIRS "."
    REQUIRES
        board
        i2c_bus
        esp_driver_gpio
        unity
)