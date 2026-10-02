/*
 * HD44780 LCD Driver for ESP32
 * Copyright (c) 2026 Anton Petrusevich
 * Licensed under the MIT License
 */

#ifndef HD44780_H_
#define HD44780_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * THREAD-SAFETY
 * -------------
 * This driver is NOT internally synchronized. The HD44780 controller, the
 * bus vtable, and every backend assume that all calls -- including the
 * underlying bus writes triggered from the controller helpers -- originate
 * from a single FreeRTOS task. Funnel all hd44780_t / lcd_* operations
 * through one dedicated display task; if other tasks need to update the
 * screen, post a message to that task instead of touching the API
 * directly. Adding a mutex here would only paper over higher-level races
 * (interleaved cursor-set / write_str pairs, etc.).
 */

/* RS line passed to bus write operations. */
typedef enum
{
    HD44780_MODE_COMMAND = 0,
    HD44780_MODE_DATA    = 1,
} hd44780_mode_t;

/* Common single-controller HD44780 module geometries supported by this
 * driver. Dual-enable layouts such as 40x4 are intentionally excluded. */
typedef enum
{
    HD44780_GEOMETRY_8X2,
    HD44780_GEOMETRY_16X2,
    HD44780_GEOMETRY_16X4,
    HD44780_GEOMETRY_20X2,
    HD44780_GEOMETRY_20X4,
    HD44780_GEOMETRY_40X2,
} hd44780_geometry_t;

typedef struct lcd_bus_hd44780_s lcd_bus_hd44780_t;

/*
 * Bus vtable abstracting the transport between the HD44780 controller and
 * the physical wiring (PCF8574-over-I2C, direct 4-bit/8-bit GPIO, ...).
 * All ops take the bus pointer as first arg so the implementation may
 * embed the vtable in a larger context struct (the vtable must be the
 * first member, so an upcast to (lcd_bus_hd44780_t *) is legal).
 */
struct lcd_bus_hd44780_s
{
    /* Write a full data/command byte to the controller (4-bit transports
     * are responsible for splitting it into two nibbles internally). */
    esp_err_t (*write_byte)(lcd_bus_hd44780_t *bus, uint8_t data, hd44780_mode_t mode);

    /* Write a single high nibble (data in bits 7..4). Required for the
     * 4-bit init/reset sequence executed before the controller knows
     * about 4-bit mode. */
    esp_err_t (*write_nibble)(lcd_bus_hd44780_t *bus, uint8_t nibble, hd44780_mode_t mode);

    /* Optional: toggle the LCD backlight. NULL when the backend has no
     * backlight control; lcd_backlight_*() then becomes a no-op. */
    esp_err_t (*set_backlight)(lcd_bus_hd44780_t *bus, bool on);

    /* Release any bus-owned resources, free the bus context and set
     * *bus to NULL. Implementations must tolerate *bus == NULL. */
    void (*destroy)(lcd_bus_hd44780_t **bus);

    /* Physical data-bus width: 4 or 8. Set by the backend factory; the
     * controller reads it once during lcd_init() to pick the right
     * FUNCTION SET byte and to skip the "switch to 4-bit" step when the
     * backend is already in 8-bit mode. */
    uint8_t data_width;
};

typedef struct hd44780_s
{
    uint8_t            cols;
    uint8_t            rows;
    uint8_t            display_control;
    uint8_t            entry_mode;
    uint8_t            row_offsets[4];
    lcd_bus_hd44780_t *bus;
    bool               owns_bus; /* if true, lcd_deinit() calls bus->destroy() */
} hd44780_t;

/* Bind a bus to a controller and run the HD44780 init sequence.
 * If `owns_bus` is true, lcd_deinit() will also destroy the bus. */
hd44780_t *lcd_init(lcd_bus_hd44780_t *bus, hd44780_geometry_t geometry, bool owns_bus);
esp_err_t  lcd_deinit(hd44780_t **lcd);

void lcd_display_on(hd44780_t *lcd);
void lcd_display_off(hd44780_t *lcd);
void lcd_cursor_on(hd44780_t *lcd);
void lcd_cursor_off(hd44780_t *lcd);
void lcd_blink_on(hd44780_t *lcd);
void lcd_blink_off(hd44780_t *lcd);
void lcd_left_to_right(hd44780_t *lcd);
void lcd_right_to_left(hd44780_t *lcd);
void lcd_autoscroll_on(hd44780_t *lcd);
void lcd_autoscroll_off(hd44780_t *lcd);

/* Backlight control. No-op when the active bus backend doesn't implement
 * set_backlight (e.g. the direct-GPIO backends). */
void lcd_backlight_on(hd44780_t *lcd);
void lcd_backlight_off(hd44780_t *lcd);

void lcd_set_cursor(hd44780_t *lcd, uint8_t col, uint8_t row);
void lcd_home(hd44780_t *lcd);
void lcd_clear_screen(hd44780_t *lcd);

void lcd_write_char(hd44780_t *lcd, char c);
void lcd_write_str(hd44780_t *lcd, const char *str);
void lcd_write_strn(hd44780_t *lcd, const char *str, size_t len);
void lcd_write_cgram(hd44780_t *lcd, uint8_t location, const uint8_t *charmap);

/*
 * Create an HD44780 bus driven by a PCF8574 I/O expander reached over I2C.
 * Bit layout on the expander (typical "I2C LCD backpack"):
 *   P0 -> RS   P1 -> RW   P2 -> E   P3 -> Backlight
 *   P4..P7 -> D4..D7
 *
 * The first call for a given i2c_num initializes the I2C master bus on
 * the given (sda, scl) pins; subsequent calls reuse that bus and only
 * attach a new device. The master bus is reference-counted: when the
 * last PCF8574 device on a port is destroyed, the bus itself is torn
 * down too.
 *
 * `scl_hz` selects the SCL frequency. Pass 0 for the conservative
 * 100 kHz default; 400 kHz works on virtually every backpack.
 *
 * Returns NULL on failure.
 */
lcd_bus_hd44780_t *lcd_bus_pcf8574_i2c_create( //
    i2c_port_t i2c_num,                        //
    uint8_t    i2c_addr,                       //
    gpio_num_t sda,                            //
    gpio_num_t scl,                            //
    uint32_t   scl_hz                          //
);

i2c_master_bus_handle_t lcd_bus_pcf8574_get_i2c_bus(i2c_port_t i2c_num);        //CREADA POR AMIIIII



/*
 * Create an HD44780 bus driven directly from GPIOs in 4-bit mode.
 * Only D4..D7 are wired; RW is assumed tied to GND on the board.
 * Pins are configured as outputs by the factory.
 * Returns NULL on failure.
 */
lcd_bus_hd44780_t *lcd_bus_gpio4_create( //
    gpio_num_t rs,                       //
    gpio_num_t en,                       //
    gpio_num_t d4,                       //
    gpio_num_t d5,                       //
    gpio_num_t d6,                       //
    gpio_num_t d7                        //
);

/*
 * Create an HD44780 bus driven directly from GPIOs in 8-bit mode.
 * D0..D7 are all wired; RW is assumed tied to GND on the board.
 * Pins are configured as outputs by the factory.
 * Returns NULL on failure.
 */
lcd_bus_hd44780_t *lcd_bus_gpio8_create( //
    gpio_num_t rs,                       //
    gpio_num_t en,                       //
    gpio_num_t d0,                       //
    gpio_num_t d1,                       //
    gpio_num_t d2,                       //
    gpio_num_t d3,                       //
    gpio_num_t d4,                       //
    gpio_num_t d5,                       //
    gpio_num_t d6,                       //
    gpio_num_t d7                        //
);

#ifdef __cplusplus
}
#endif
#endif /* HD44780_H_ */