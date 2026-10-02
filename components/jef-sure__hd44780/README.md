# HD44780 ESP-IDF Component

ESP-IDF component for HD44780-compatible character LCD modules.

This driver supports three transport backends:

- PCF8574 I/O expander over I2C
- Direct GPIO 4-bit mode
- Direct GPIO 8-bit mode

The controller API is shared across all three backends, so the application logic stays the same after the bus is created.

## Features

- Supports common single-controller geometries: 8x2, 16x2, 16x4, 20x2, 20x4, 40x2
- Handles the HD44780 power-on and 4-bit reset sequence internally
- Works with typical PCF8574 LCD backpacks over I2C
- Supports direct GPIO wiring in 4-bit and 8-bit modes
- Provides display, cursor, blink, entry-mode, clear, home, backlight, and CGRAM helpers
- Fails early if the bus backend cannot initialize or the LCD init sequence cannot complete

## Limitations

- Not internally synchronized. Use one dedicated task for all LCD access.
- Dual-enable modules such as 40x4 are not supported.
- The driver is write-only. It does not poll the HD44780 busy flag.
- Direct GPIO backends assume RW is tied to GND.
- Backlight control is only available when the active backend implements it, such as the PCF8574 backpack backend.

## Public Header

Include the driver with:

```c
#include "hd44780.h"
```

## Supported Geometries

Use one of these geometry values when calling `lcd_init()`:

- `HD44780_GEOMETRY_8X2`
- `HD44780_GEOMETRY_16X2`
- `HD44780_GEOMETRY_16X4`
- `HD44780_GEOMETRY_20X2`
- `HD44780_GEOMETRY_20X4`
- `HD44780_GEOMETRY_40X2`

## Bus Creation

### I2C Backpack via PCF8574

Create an HD44780 bus on a typical PCF8574 backpack with:

```c
lcd_bus_hd44780_t *lcd_bus_pcf8574_i2c_create(
    i2c_port_t i2c_num,
    uint8_t i2c_addr,
    gpio_num_t sda,
    gpio_num_t scl,
    uint32_t scl_hz
);
```

Typical backpack bit mapping expected by the driver:

| PCF8574 pin | LCD signal |
| --- | --- |
| P0 | RS |
| P1 | RW |
| P2 | E |
| P3 | Backlight |
| P4 | D4 |
| P5 | D5 |
| P6 | D6 |
| P7 | D7 |

Notes:

- `scl_hz = 0` selects a conservative 100 kHz default.
- Common backpack addresses are `0x27` and `0x3F`.
- The driver probes the address before attaching the device.
- The first device created on an I2C port creates the master bus. Later devices on the same port reuse it.

### Direct GPIO 4-Bit Mode

Create a write-only 4-bit bus with:

```c
lcd_bus_hd44780_t *lcd_bus_gpio4_create(
    gpio_num_t rs,
    gpio_num_t en,
    gpio_num_t d4,
    gpio_num_t d5,
    gpio_num_t d6,
    gpio_num_t d7
);
```

### Direct GPIO 8-Bit Mode

Create a write-only 8-bit bus with:

```c
lcd_bus_hd44780_t *lcd_bus_gpio8_create(
    gpio_num_t rs,
    gpio_num_t en,
    gpio_num_t d0,
    gpio_num_t d1,
    gpio_num_t d2,
    gpio_num_t d3,
    gpio_num_t d4,
    gpio_num_t d5,
    gpio_num_t d6,
    gpio_num_t d7
);
```

All GPIO pins passed to the direct backends must be output-capable GPIOs.

## Controller Lifetime

Bind a bus to the controller with:

```c
hd44780_t *lcd_init(lcd_bus_hd44780_t *bus, hd44780_geometry_t geometry, bool owns_bus);
```

If `owns_bus` is `true`, `lcd_deinit()` also destroys the bus object.

```c
esp_err_t lcd_deinit(hd44780_t **lcd);
```

## Minimal I2C Example

```c
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hd44780.h"

void app_main(void)
{
    lcd_bus_hd44780_t *bus = lcd_bus_pcf8574_i2c_create(
        I2C_NUM_0,
        0x27,
        GPIO_NUM_21,
        GPIO_NUM_22,
        100000
    );

    if (!bus) {
        return;
    }

    hd44780_t *lcd = lcd_init(bus, HD44780_GEOMETRY_20X4, true);
    if (!lcd) {
        if (bus->destroy) {
            bus->destroy(&bus);
        }
        return;
    }

    lcd_backlight_on(lcd);
    lcd_clear_screen(lcd);
    lcd_set_cursor(lcd, 0, 0);
    lcd_write_str(lcd, "Hello, HD44780");
    lcd_set_cursor(lcd, 0, 1);
    lcd_write_str(lcd, "over I2C");
}
```

## Minimal GPIO 4-Bit Example

```c
#include "hd44780.h"

void app_main(void)
{
    lcd_bus_hd44780_t *bus = lcd_bus_gpio4_create(
        GPIO_NUM_4,
        GPIO_NUM_5,
        GPIO_NUM_18,
        GPIO_NUM_19,
        GPIO_NUM_21,
        GPIO_NUM_22
    );

    if (!bus) {
        return;
    }

    hd44780_t *lcd = lcd_init(bus, HD44780_GEOMETRY_16X2, true);
    if (!lcd) {
        if (bus->destroy) {
            bus->destroy(&bus);
        }
        return;
    }

    lcd_clear_screen(lcd);
    lcd_write_str(lcd, "GPIO 4-bit mode");
}
```

## Common Operations

Display and cursor control:

- `lcd_display_on()` / `lcd_display_off()`
- `lcd_cursor_on()` / `lcd_cursor_off()`
- `lcd_blink_on()` / `lcd_blink_off()`
- `lcd_backlight_on()` / `lcd_backlight_off()`

Cursor and text:

- `lcd_set_cursor()`
- `lcd_home()`
- `lcd_clear_screen()`
- `lcd_write_char()`
- `lcd_write_str()`
- `lcd_write_strn()`

Entry mode:

- `lcd_left_to_right()`
- `lcd_right_to_left()`
- `lcd_autoscroll_on()`
- `lcd_autoscroll_off()`

Custom characters:

- `lcd_write_cgram()` writes one 8-byte character pattern into CGRAM slot 0-7

## Thread Safety

This component is not thread-safe.

Use one FreeRTOS task as the sole owner of the display. If other tasks need to update the LCD, send messages to that display task instead of calling `lcd_*()` from multiple tasks.

## Troubleshooting

- No output on I2C: verify the backpack address, SDA/SCL pins, power, and pull-ups.
- Initialization fails immediately: confirm the selected geometry matches the actual module.
- Text appears on unexpected rows: double-check the geometry, especially for 16x4 and 20x4 modules.
- Backlight helpers appear to do nothing: direct GPIO backends do not implement backlight control.
- Random display corruption: keep all LCD access in one task.
