set(srcs "src/hd44780.c")

idf_component_register(
	SRCS "${srcs}"
	REQUIRES driver esp_driver_i2c esp_driver_gpio esp_timer
	INCLUDE_DIRS "include"
)
