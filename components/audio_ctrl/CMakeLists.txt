idf_component_register(
    SRCS "audio_ctrl.c"
    INCLUDE_DIRS "."
    REQUIRES
        driver
        esp_driver_i2s
        gpio_ctrl
        i2c_bus
        unity
)