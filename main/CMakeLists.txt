idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
   REQUIRES esp_driver_ledc driver hd44780 pid_ctrl as5600 esp_driver_uart nvs_flash
)
