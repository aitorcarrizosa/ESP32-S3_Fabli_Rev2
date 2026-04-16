idf_component_register(
    SRCS "sdcard_ctrl.c"
    INCLUDE_DIRS "."
    REQUIRES
        board
        fatfs
        vfs
        sdmmc
        esp_driver_sdmmc
)