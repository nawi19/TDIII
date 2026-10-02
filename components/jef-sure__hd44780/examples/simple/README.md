# Simple HD44780 I2C Example

This example demonstrates an HD44780-compatible character LCD connected through a typical PCF8574 I2C backpack.

## Overview

The example shows how to:

- create a PCF8574 I2C LCD bus
- initialize an HD44780 controller
- turn on the backpack backlight
- run a short LCD self-test
- update the display once per second

## Hardware Setup

The default configuration in `main/sample.c` targets a 20x4 LCD backpack at address `0x27` on I2C port 0.

| Backpack signal | ESP32 pin |
| --- | --- |
| VCC | 5V |
| GND | GND |
| SDA | GPIO 21 |
| SCL | GPIO 22 |

Common PCF8574 backpack addresses are `0x27` and `0x3F`. Adjust `SAMPLE_LCD_I2C_ADDR`, `SAMPLE_LCD_SDA`, `SAMPLE_LCD_SCL`, and `SAMPLE_LCD_GEOMETRY` in `main/sample.c` if your hardware differs.

## Building and Running

From this example directory:

```bash
idf.py set-target esp32
idf.py build
idf.py flash monitor
```

## Expected Output

The LCD first shows a splash screen with the configured I2C address and pins. If `SAMPLE_RUN_SELF_TEST` is enabled, it then toggles the backlight, writes corner markers, checks row mapping, and verifies home/clear behavior. After that, it switches to a runtime screen that updates once per second.

The serial monitor logs LCD initialization and each self-test step.

## Notes

- Make sure SDA and SCL have pull-ups. Many LCD backpacks include them.
- If initialization fails, check the I2C address first.
- The sample uses the I2C backend. Direct GPIO wiring is documented in the component README.
