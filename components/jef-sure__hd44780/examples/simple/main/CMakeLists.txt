FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/main/*.c)

idf_component_register(
    SRCS ${app_sources}
    REQUIRES  driver esp_timer hd44780
    INCLUDE_DIRS "."
)
