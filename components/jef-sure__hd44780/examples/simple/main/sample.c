/*
 * HD44780 LCD Driver for ESP32
 * Copyright (c) 2026 Anton Petrusevich
 * Licensed under the MIT License
 */

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hd44780.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Typical ESP32 + PCF8574 backpack wiring. Adjust these for your board. */
enum
{
    SAMPLE_LCD_I2C_PORT        = I2C_NUM_0,
    SAMPLE_LCD_I2C_ADDR        = 0x27,
    SAMPLE_LCD_SDA             = GPIO_NUM_21,
    SAMPLE_LCD_SCL             = GPIO_NUM_22,
    SAMPLE_LCD_SCL_HZ          = 100000,
    SAMPLE_LCD_GEOMETRY        = HD44780_GEOMETRY_20X4,
    SAMPLE_RUN_SELF_TEST       = 1,
    SAMPLE_SELF_TEST_STEP_MS   = 1200,
    SAMPLE_BACKLIGHT_TOGGLE_MS = 250,
    SAMPLE_SPLASH_MS           = 2000,
    SAMPLE_UPDATE_MS           = 1000,
};

static const char *TAG = "hd44780_sample";

static void lcd_write_row(hd44780_t *lcd, uint8_t row, uint8_t cols, const char *text)
{
    char   line[41] = {0};
    size_t copy_len = strlen(text);

    if (cols > sizeof(line) - 1) {
        cols = sizeof(line) - 1;
    }
    if (copy_len > cols) {
        copy_len = cols;
    }

    memset(line, ' ', cols);
    memcpy(line, text, copy_len);
    line[cols] = '\0';

    lcd_set_cursor(lcd, 0, row);
    lcd_write_strn(lcd, line, cols);
}

static void render_splash(hd44780_t *lcd, uint8_t cols, uint8_t rows)
{
    char line[41];

    lcd_write_row(lcd, 0, cols, "HD44780 over I2C");

    if (rows > 1) {
        snprintf(line, sizeof(line), "Addr 0x%02X", SAMPLE_LCD_I2C_ADDR);
        lcd_write_row(lcd, 1, cols, line);
    }
    if (rows > 2) {
        snprintf(line, sizeof(line), "SDA %d SCL %d", SAMPLE_LCD_SDA, SAMPLE_LCD_SCL);
        lcd_write_row(lcd, 2, cols, line);
    }
    if (rows > 3) {
        snprintf(line, sizeof(line), "%u Hz", (unsigned int)SAMPLE_LCD_SCL_HZ);
        lcd_write_row(lcd, 3, cols, line);
    }
}

static void render_runtime(hd44780_t *lcd, uint8_t cols, uint8_t rows, uint32_t seconds)
{
    char line[41];

    lcd_write_row(lcd, 0, cols, "ESP-IDF sample");

    if (rows > 1) {
        snprintf(line, sizeof(line), "Uptime %lus", (unsigned long)seconds);
        lcd_write_row(lcd, 1, cols, line);
    }
    if (rows > 2) {
        snprintf(line, sizeof(line), "I2C 0x%02X %uHz", SAMPLE_LCD_I2C_ADDR, (unsigned int)SAMPLE_LCD_SCL_HZ);
        lcd_write_row(lcd, 2, cols, line);
    }
    if (rows > 3) {
        snprintf(line, sizeof(line), "Pins %d/%d", SAMPLE_LCD_SDA, SAMPLE_LCD_SCL);
        lcd_write_row(lcd, 3, cols, line);
    }
}

static void sample_delay(uint32_t delay_ms)
{
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
}

static void run_self_test(hd44780_t *lcd, uint8_t cols, uint8_t rows)
{
    char line[41];

    ESP_LOGI(TAG, "self-test: backlight toggle");
    lcd_clear_screen(lcd);
    lcd_write_row(lcd, 0, cols, "Self-test");
    if (rows > 1) {
        lcd_write_row(lcd, 1, cols, "Backlight");
    }
    lcd_backlight_off(lcd);
    sample_delay(SAMPLE_BACKLIGHT_TOGGLE_MS);
    lcd_backlight_on(lcd);
    sample_delay(SAMPLE_BACKLIGHT_TOGGLE_MS);
    if (rows > 1) {
        lcd_write_row(lcd, 1, cols, "Backlight OK");
    }
    sample_delay(SAMPLE_SELF_TEST_STEP_MS);

    ESP_LOGI(TAG, "self-test: corner markers");
    lcd_clear_screen(lcd);
    if (rows > 2) {
        lcd_write_row(lcd, 1, cols, "Corner test");
        lcd_write_row(lcd, 2, cols, "1 TL 2 TR");
    }
    lcd_set_cursor(lcd, 0, 0);
    lcd_write_char(lcd, '1');
    if (cols > 1) {
        lcd_set_cursor(lcd, cols - 1, 0);
        lcd_write_char(lcd, '2');
    }
    if (rows > 1) {
        lcd_set_cursor(lcd, 0, rows - 1);
        lcd_write_char(lcd, '3');
        if (cols > 1) {
            lcd_set_cursor(lcd, cols - 1, rows - 1);
            lcd_write_char(lcd, '4');
        }
    }
    sample_delay(SAMPLE_SELF_TEST_STEP_MS);

    ESP_LOGI(TAG, "self-test: row mapping");
    lcd_clear_screen(lcd);
    for (uint8_t row = 0; row < rows; ++row) {
        snprintf(line, sizeof(line), "Row %u of %u", (unsigned int)(row + 1), (unsigned int)rows);
        lcd_write_row(lcd, row, cols, line);
    }
    sample_delay(SAMPLE_SELF_TEST_STEP_MS);

    ESP_LOGI(TAG, "self-test: home and clear");
    lcd_clear_screen(lcd);
    lcd_set_cursor(lcd, cols > 6 ? 6 : 0, rows > 1 ? 1 : 0);
    lcd_write_str(lcd, "Offset");
    sample_delay(SAMPLE_BACKLIGHT_TOGGLE_MS);
    lcd_home(lcd);
    lcd_write_row(lcd, 0, cols, "Home OK");
    if (rows > 1) {
        lcd_write_row(lcd, 1, cols, "Clear next");
    }
    sample_delay(SAMPLE_SELF_TEST_STEP_MS);

    lcd_clear_screen(lcd);
    lcd_write_row(lcd, 0, cols, "Clear OK");
    if (rows > 1) {
        lcd_write_row(lcd, 1, cols, "Runtime next");
    }
    sample_delay(SAMPLE_SELF_TEST_STEP_MS);
}

void app_main(void)
{
    lcd_bus_hd44780_t *bus = lcd_bus_pcf8574_i2c_create(SAMPLE_LCD_I2C_PORT, SAMPLE_LCD_I2C_ADDR, SAMPLE_LCD_SDA,
                                                        SAMPLE_LCD_SCL, SAMPLE_LCD_SCL_HZ);
    if (!bus) {
        ESP_LOGE(TAG, "failed to create I2C backpack bus");
        return;
    }

    hd44780_t *lcd = lcd_init(bus, SAMPLE_LCD_GEOMETRY, true);
    if (!lcd) {
        ESP_LOGE(TAG, "failed to initialize LCD");
        if (bus->destroy) {
            bus->destroy(&bus);
        }
        return;
    }

    lcd_backlight_on(lcd);
    lcd_clear_screen(lcd);
    render_splash(lcd, lcd->cols, lcd->rows);

    ESP_LOGI(TAG, "LCD ready on I2C%d addr=0x%02X SDA=%d SCL=%d", SAMPLE_LCD_I2C_PORT, SAMPLE_LCD_I2C_ADDR,
             SAMPLE_LCD_SDA, SAMPLE_LCD_SCL);

    vTaskDelay(pdMS_TO_TICKS(SAMPLE_SPLASH_MS));

    if (SAMPLE_RUN_SELF_TEST) {
        run_self_test(lcd, lcd->cols, lcd->rows);
    }

    while (true) {
        uint32_t seconds = (uint32_t)(esp_timer_get_time() / 1000000ULL);
        render_runtime(lcd, lcd->cols, lcd->rows, seconds);
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_UPDATE_MS));
    }
}
