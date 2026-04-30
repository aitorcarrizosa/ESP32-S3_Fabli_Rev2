idf_component_register(
    SRCS "input_ctrl.c"
    INCLUDE_DIRS "."
    REQUIRES
        board
        i2c_bus
        audio_ctrl
        esp_driver_gpio
        esp_system
)