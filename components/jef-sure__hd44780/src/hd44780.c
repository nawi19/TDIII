/*
 * HD44780 LCD Driver for ESP32
 * Copyright (c) 2026 Anton Petrusevich
 * Licensed under the MIT License
 */

#include "hd44780.h"
#include "esp_timer.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * THREAD-SAFETY: see the comment block at the top of include/hd44780.h.
 * All entry points -- the public lcd_* helpers and the per-backend bus
 * writes triggered from them -- must be invoked from a single FreeRTOS
 * task. This module never takes a lock of its own.
 */

/* DDRAM line offsets (HD44780 datasheet). */
#define LCD_LINEONE   0x00
#define LCD_LINETWO   0x40
#define LCD_LINETHREE 0x14
#define LCD_LINEFOUR  0x54

/* Mode aliases for in-file readability (RS = 0/1). */
#define LCD_COMMAND HD44780_MODE_COMMAND
#define LCD_WRITE   HD44780_MODE_DATA

#define LCD_SET_CGRAM_ADDR 0x40
#define LCD_SET_DDRAM_ADDR 0x80

/* LCD instructions. */
#define LCD_CLEAR                 0x01
#define LCD_HOME                  0x02
#define LCD_ENTRY_MODE_SET        0x04
#define LCD_ENTRY_SHIFT_INCREMENT 0x01
#define LCD_ENTRY_LEFT            0x02
#define LCD_DISPLAY_CONTROL       0x08
#define LCD_BLINK_ENABLE          0x01
#define LCD_CURSOR_ENABLE         0x02
#define LCD_DISPLAY_ENABLE        0x04
#define LCD_FUNCTION_RESET        0x30 /* 4-bit reset nibble */
#define LCD_FUNCTION_8BIT         0x10
#define LCD_FUNCTION_4BIT         0x20
#define LCD_FUNCTION_2LINE        0x08

static const char TAG[] = "LCD HD44780";

/* clear/home both need >= 1.52 ms per the datasheet; round up to one
 * tick at minimum so very coarse FreeRTOS tick rates still wait. */
static const TickType_t s_clear_home_ticks = (pdMS_TO_TICKS(2) > 0) ? pdMS_TO_TICKS(2) : 1;

static inline esp_err_t lcd_try_write_byte(hd44780_t *lcd, uint8_t data, hd44780_mode_t mode)
{
    esp_err_t rc = lcd->bus->write_byte(lcd->bus, data, mode);
    if (rc != ESP_OK) {
        ESP_LOGW(TAG, "bus write_byte failed: %s", esp_err_to_name(rc));
    }
    return rc;
}

static inline esp_err_t lcd_try_write_nibble(hd44780_t *lcd, uint8_t nibble, hd44780_mode_t mode)
{
    esp_err_t rc = lcd->bus->write_nibble(lcd->bus, nibble, mode);
    if (rc != ESP_OK) {
        ESP_LOGW(TAG, "bus write_nibble failed: %s", esp_err_to_name(rc));
    }
    return rc;
}

static inline void lcd_write_byte(hd44780_t *lcd, uint8_t data, hd44780_mode_t mode)
{
    (void)lcd_try_write_byte(lcd, data, mode);
}

static inline void lcd_write_nibble(hd44780_t *lcd, uint8_t nibble, hd44780_mode_t mode)
{
    (void)lcd_try_write_nibble(lcd, nibble, mode);
}

static void lcd_write_command(hd44780_t *lcd, uint8_t command)
{
    lcd_write_byte(lcd, command, LCD_COMMAND);
}

static void lcd_update_display_control(hd44780_t *lcd)
{
    lcd_write_command(lcd, LCD_DISPLAY_CONTROL | lcd->display_control);
}

static void lcd_update_entry_mode(hd44780_t *lcd)
{
    lcd_write_command(lcd, LCD_ENTRY_MODE_SET | lcd->entry_mode);
}

static bool lcd_geometry_dimensions(hd44780_geometry_t geometry, uint8_t *cols, uint8_t *rows)
{
    switch (geometry) {
    case HD44780_GEOMETRY_8X2:
        *cols = 8;
        *rows = 2;
        return true;
    case HD44780_GEOMETRY_16X2:
        *cols = 16;
        *rows = 2;
        return true;
    case HD44780_GEOMETRY_16X4:
        *cols = 16;
        *rows = 4;
        return true;
    case HD44780_GEOMETRY_20X2:
        *cols = 20;
        *rows = 2;
        return true;
    case HD44780_GEOMETRY_20X4:
        *cols = 20;
        *rows = 4;
        return true;
    case HD44780_GEOMETRY_40X2:
        *cols = 40;
        *rows = 2;
        return true;
    default:
        return false;
    }
}

static void lcd_set_row_offsets(hd44780_t *lcd, uint8_t row0, uint8_t row1, uint8_t row2, uint8_t row3)
{
    lcd->row_offsets[0] = row0;
    lcd->row_offsets[1] = row1;
    lcd->row_offsets[2] = row2;
    lcd->row_offsets[3] = row3;
}

static void lcd_configure_geometry(hd44780_t *lcd)
{
    if (lcd->rows <= 1) {
        lcd_set_row_offsets(lcd, LCD_LINEONE, LCD_LINEONE, LCD_LINEONE, LCD_LINEONE);
        return;
    }

    if (lcd->rows == 2) {
        /* Standard 1602/2002-style layout. */
        lcd_set_row_offsets(lcd, LCD_LINEONE, LCD_LINETWO, LCD_LINEONE, LCD_LINETWO);
        return;
    }

    if (lcd->cols == 16) {
        /* 16x4 panels wrap rows 3 and 4 onto the second half of rows 1 and 2. */
        lcd_set_row_offsets(lcd, LCD_LINEONE, LCD_LINETWO, LCD_LINEONE + 16, LCD_LINETWO + 16);
        return;
    }

    lcd_set_row_offsets(lcd, LCD_LINEONE, LCD_LINETWO, LCD_LINETHREE, LCD_LINEFOUR);
}

hd44780_t *lcd_init(lcd_bus_hd44780_t *bus, hd44780_geometry_t geometry, bool owns_bus)
{
    uint8_t   cols = 0;
    uint8_t   rows = 0;
    esp_err_t rc   = ESP_OK;

    if (!bus || !bus->write_byte || !bus->write_nibble) {
        ESP_LOGE(TAG, "invalid bus");
        return NULL;
    }

    if (!lcd_geometry_dimensions(geometry, &cols, &rows)) {
        ESP_LOGE(TAG, "unsupported geometry %d", (int)geometry);
        return NULL;
    }
   
    hd44780_t *lcd = calloc(1, sizeof(*lcd));
    if (!lcd) {
        return NULL;
    }

    lcd->bus             = bus;
    lcd->owns_bus        = owns_bus;
    lcd->cols            = cols;
    lcd->rows            = rows;
    lcd->display_control = 0;
    lcd->entry_mode      = LCD_ENTRY_LEFT;
    lcd_configure_geometry(lcd);

    /* HD44780 function-set byte: bus width derived from the backend, N
     * derived from row count, 5x8 font. */
    const bool    four_bit     = (bus->data_width != 8);
    const uint8_t width_bit    = four_bit ? LCD_FUNCTION_4BIT : LCD_FUNCTION_8BIT;
    const uint8_t function_set = width_bit | (rows > 1 ? LCD_FUNCTION_2LINE : 0);

    vTaskDelay(pdMS_TO_TICKS(200));                                  /* power-on settle */
    rc = lcd_try_write_nibble(lcd, LCD_FUNCTION_RESET, LCD_COMMAND); /* reset 1/3 */
    if (rc != ESP_OK) {
        goto fail;
    }
    vTaskDelay(pdMS_TO_TICKS(10));                                   /* > 4.1 ms */
    rc = lcd_try_write_nibble(lcd, LCD_FUNCTION_RESET, LCD_COMMAND); /* reset 2/3 */
    if (rc != ESP_OK) {
        goto fail;
    }
    esp_rom_delay_us(200);                                           /* > 100 us */
    rc = lcd_try_write_nibble(lcd, LCD_FUNCTION_RESET, LCD_COMMAND); /* reset 3/3 */
    if (rc != ESP_OK) {
        goto fail;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    if (four_bit) {
        rc = lcd_try_write_nibble(lcd, function_set, LCD_COMMAND); /* switch to 4-bit */
        if (rc != ESP_OK) {
            goto fail;
        }
    }

    /* Controller now accepts standard instructions. This driver uses
     * fixed delays rather than polling the busy flag. */
    rc = lcd_try_write_byte(lcd, function_set, LCD_COMMAND); /* mode, lines, font */
    if (rc != ESP_OK) {
        goto fail;
    }
    rc = lcd_try_write_byte(lcd, LCD_DISPLAY_CONTROL | lcd->display_control, LCD_COMMAND); /* display off */
    if (rc != ESP_OK) {
        goto fail;
    }
    rc = lcd_try_write_byte(lcd, LCD_CLEAR, LCD_COMMAND); /* clear DDRAM */
    if (rc != ESP_OK) {
        goto fail;
    }
    vTaskDelay(s_clear_home_ticks);
    rc = lcd_try_write_byte(lcd, LCD_ENTRY_MODE_SET | lcd->entry_mode, LCD_COMMAND);
    if (rc != ESP_OK) {
        goto fail;
    }

    lcd->display_control = LCD_DISPLAY_ENABLE;
    rc                   = lcd_try_write_byte(lcd, LCD_DISPLAY_CONTROL | lcd->display_control, LCD_COMMAND);
    if (rc != ESP_OK) {
        goto fail;
    }
    return lcd;

fail:
    ESP_LOGE(TAG, "lcd init failed: %s", esp_err_to_name(rc));
    if (lcd->owns_bus && lcd->bus && lcd->bus->destroy) {
        lcd->bus->destroy(&lcd->bus);
    }
    free(lcd);
    return NULL;
}

esp_err_t lcd_deinit(hd44780_t **lcd)
{
    if (!lcd || !*lcd) {
        return ESP_ERR_INVALID_ARG;
    }

    hd44780_t *instance = *lcd;

    if (instance->owns_bus && instance->bus && instance->bus->destroy) {
        instance->bus->destroy(&instance->bus);
    }

    free(instance);
    *lcd = NULL;
    return ESP_OK;
}

void lcd_display_on(hd44780_t *lcd)
{
    lcd->display_control |= LCD_DISPLAY_ENABLE;
    lcd_update_display_control(lcd);
}

void lcd_display_off(hd44780_t *lcd)
{
    lcd->display_control &= ~LCD_DISPLAY_ENABLE;
    lcd_update_display_control(lcd);
}

void lcd_cursor_on(hd44780_t *lcd)
{
    lcd->display_control |= LCD_CURSOR_ENABLE;
    lcd_update_display_control(lcd);
}

void lcd_cursor_off(hd44780_t *lcd)
{
    lcd->display_control &= ~LCD_CURSOR_ENABLE;
    lcd_update_display_control(lcd);
}

void lcd_blink_on(hd44780_t *lcd)
{
    lcd->display_control |= LCD_BLINK_ENABLE;
    lcd_update_display_control(lcd);
}

void lcd_blink_off(hd44780_t *lcd)
{
    lcd->display_control &= ~LCD_BLINK_ENABLE;
    lcd_update_display_control(lcd);
}

void lcd_left_to_right(hd44780_t *lcd)
{
    lcd->entry_mode |= LCD_ENTRY_LEFT;
    lcd_update_entry_mode(lcd);
}

void lcd_right_to_left(hd44780_t *lcd)
{
    lcd->entry_mode &= ~LCD_ENTRY_LEFT;
    lcd_update_entry_mode(lcd);
}

void lcd_autoscroll_on(hd44780_t *lcd)
{
    lcd->entry_mode |= LCD_ENTRY_SHIFT_INCREMENT;
    lcd_update_entry_mode(lcd);
}

void lcd_autoscroll_off(hd44780_t *lcd)
{
    lcd->entry_mode &= ~LCD_ENTRY_SHIFT_INCREMENT;
    lcd_update_entry_mode(lcd);
}

void lcd_backlight_on(hd44780_t *lcd)
{
    if (!lcd || !lcd->bus || !lcd->bus->set_backlight) {
        return;
    }
    esp_err_t rc = lcd->bus->set_backlight(lcd->bus, true);
    if (rc != ESP_OK) {
        ESP_LOGW(TAG, "set_backlight on failed: %s", esp_err_to_name(rc));
    }
}

void lcd_backlight_off(hd44780_t *lcd)
{
    if (!lcd || !lcd->bus || !lcd->bus->set_backlight) {
        return;
    }
    esp_err_t rc = lcd->bus->set_backlight(lcd->bus, false);
    if (rc != ESP_OK) {
        ESP_LOGW(TAG, "set_backlight off failed: %s", esp_err_to_name(rc));
    }
}

void lcd_set_cursor(hd44780_t *lcd, uint8_t col, uint8_t row)
{
    if (col > lcd->cols - 1) {
        ESP_LOGE(TAG, "Cannot write to col %d. Please select a col in the range (0, %d)", col, lcd->cols - 1);
        col = lcd->cols - 1;
    }
    if (row > lcd->rows - 1) {
        ESP_LOGE(TAG, "Cannot write to row %d. Please select a row in the range (0, %d)", row, lcd->rows - 1);
        row = lcd->rows - 1;
    }
    lcd_write_command(lcd, LCD_SET_DDRAM_ADDR | (col + lcd->row_offsets[row]));
}

void lcd_write_char(hd44780_t *lcd, char c)
{
    lcd_write_byte(lcd, (uint8_t)c, LCD_WRITE);
}

void lcd_write_str(hd44780_t *lcd, const char *str)
{
    while (*str) {
        lcd_write_char(lcd, *str++);
    }
}

void lcd_write_strn(hd44780_t *lcd, const char *str, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        lcd_write_char(lcd, str[i]);
    }
}

void lcd_home(hd44780_t *lcd)
{
    lcd_write_command(lcd, LCD_HOME);
    vTaskDelay(s_clear_home_ticks);
}

void lcd_clear_screen(hd44780_t *lcd)
{
    lcd_write_command(lcd, LCD_CLEAR);
    vTaskDelay(s_clear_home_ticks);
}

void lcd_write_cgram(hd44780_t *lcd, uint8_t location, const uint8_t *charmap)
{
    location &= 0x7; /* 8 CGRAM slots */
    lcd_write_command(lcd, LCD_SET_CGRAM_ADDR | (location << 3));
    esp_rom_delay_us(80);
    for (uint8_t i = 0; i < 8; ++i) {
        lcd_write_byte(lcd, charmap[i], LCD_WRITE);
    }
}

/* ------------------------------------------------------------------------- */
/*  Bus backend: PCF8574 I/O expander over I2C                               */
/* ------------------------------------------------------------------------- */

/* PCF8574 -> HD44780 backpack bit layout.
 * P1 (RW) is tied low on the backpack and never written. */
#define PCF_BL 0x08 /* P3 -> Backlight enable */
#define PCF_EN 0x04 /* P2 -> E */
#define PCF_RS 0x01 /* P0 -> RS */

#define PCF_I2C_TIMEOUT_MS 20

static const char TAG_PCF[] = "HD44780/PCF8574";

/* Shared I2C master bus cache, keyed by port number. The bus is created
 * lazily on first use and torn down when the last device is destroyed. */
static i2c_master_bus_handle_t s_i2c_buses[I2C_NUM_MAX];
static uint16_t                s_i2c_bus_refcount[I2C_NUM_MAX];

typedef struct
{
    lcd_bus_hd44780_t       base; /* must be first: enables upcast */
    i2c_port_t              i2c_num;
    i2c_master_dev_handle_t dev;
    uint8_t                 backlight; /* PCF_BL or 0 */
} pcf8574_bus_t;

static esp_err_t ensure_i2c_bus(i2c_port_t i2c_num, gpio_num_t sda, gpio_num_t scl, i2c_master_bus_handle_t *out)
{
    if (i2c_num < I2C_NUM_0 || i2c_num >= I2C_NUM_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_i2c_buses[i2c_num]) {
        *out = s_i2c_buses[i2c_num];
        return ESP_OK;
    }

    i2c_master_bus_config_t conf = {
        .i2c_port                     = i2c_num,
        .sda_io_num                   = sda,
        .scl_io_num                   = scl,
        .clk_source                   = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt            = 7,
        .trans_queue_depth            = 0,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t rc = i2c_new_master_bus(&conf, &s_i2c_buses[i2c_num]);
    if (rc == ESP_OK) {
        *out = s_i2c_buses[i2c_num];
    }
    return rc;
}

static inline int pcf_i2c_timeout_ms(void)
{
    return PCF_I2C_TIMEOUT_MS;
}

static inline esp_err_t pcf_write(pcf8574_bus_t *b, uint8_t data)
{
    return i2c_master_transmit(b->dev, &data, 1, pcf_i2c_timeout_ms());
}

/* Toggle E to latch the four data bits currently presented on D4..D7. */
static esp_err_t pcf_pulse(pcf8574_bus_t *b, uint8_t data)
{
    esp_err_t rc = pcf_write(b, data | PCF_EN);
    if (rc != ESP_OK) {
        return rc;
    }
    esp_rom_delay_us(1);
    rc = pcf_write(b, data & ~PCF_EN);
    esp_rom_delay_us(80);
    return rc;
}

static esp_err_t pcf_write_nibble(lcd_bus_hd44780_t *bus, uint8_t nibble, hd44780_mode_t mode)
{
    pcf8574_bus_t *b    = (pcf8574_bus_t *)bus;
    uint8_t        data = (nibble & 0xF0) | b->backlight | (mode == HD44780_MODE_DATA ? PCF_RS : 0);
    return pcf_pulse(b, data);
}

static esp_err_t pcf_write_byte(lcd_bus_hd44780_t *bus, uint8_t data, hd44780_mode_t mode)
{
    esp_err_t rc = pcf_write_nibble(bus, data & 0xF0, mode);
    if (rc != ESP_OK) {
        return rc;
    }
    return pcf_write_nibble(bus, (data << 4) & 0xF0, mode);
}

static esp_err_t pcf_set_backlight(lcd_bus_hd44780_t *bus, bool on)
{
    pcf8574_bus_t *b = (pcf8574_bus_t *)bus;
    b->backlight     = on ? PCF_BL : 0;
    /* Refresh PCF outputs so the BL bit takes effect immediately, with
     * RS=0, RW=0, E=0 so the controller doesn't latch anything. */
    return pcf_write(b, b->backlight);
}

static void pcf_destroy(lcd_bus_hd44780_t **bus)
{
    if (!bus || !*bus) {
        return;
    }
    pcf8574_bus_t *b = (pcf8574_bus_t *)*bus;
    if (b->dev) {
        (void)i2c_master_bus_rm_device(b->dev);
    }
    if (b->i2c_num >= I2C_NUM_0 && b->i2c_num < I2C_NUM_MAX && s_i2c_bus_refcount[b->i2c_num] > 0) {
        if (--s_i2c_bus_refcount[b->i2c_num] == 0 && s_i2c_buses[b->i2c_num]) {
            (void)i2c_del_master_bus(s_i2c_buses[b->i2c_num]);
            s_i2c_buses[b->i2c_num] = NULL;
        }
    }
    free(b);
    *bus = NULL;
}

lcd_bus_hd44780_t *lcd_bus_pcf8574_i2c_create(i2c_port_t i2c_num, uint8_t i2c_addr, gpio_num_t sda, gpio_num_t scl,
                                              uint32_t scl_hz)
{
    i2c_master_bus_handle_t bus_handle;
    if (ensure_i2c_bus(i2c_num, sda, scl, &bus_handle) != ESP_OK) {
        ESP_LOGE(TAG_PCF, "I2C bus init failed");
        return NULL;
    }

    esp_err_t rc = i2c_master_probe(bus_handle, i2c_addr, pcf_i2c_timeout_ms());
    if (rc != ESP_OK) {
        ESP_LOGE(TAG_PCF, "I2C probe 0x%02X failed: %s", i2c_addr, esp_err_to_name(rc));
        return NULL;
    }

    pcf8574_bus_t *b = calloc(1, sizeof(*b));
    if (!b) {
        return NULL;
    }

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = i2c_addr,
        .scl_speed_hz    = (scl_hz != 0) ? scl_hz : 100000,
    };
    rc = i2c_master_bus_add_device(bus_handle, &dev_config, &b->dev);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG_PCF, "I2C add device 0x%02X failed: %s", i2c_addr, esp_err_to_name(rc));
        free(b);
        return NULL;
    }

    b->i2c_num            = i2c_num;
    b->backlight          = PCF_BL;
    b->base.write_byte    = pcf_write_byte;
    b->base.write_nibble  = pcf_write_nibble;
    b->base.set_backlight = pcf_set_backlight;
    b->base.destroy       = pcf_destroy;
    b->base.data_width    = 4;
    s_i2c_bus_refcount[i2c_num]++;
    return &b->base;
}


i2c_master_bus_handle_t lcd_bus_pcf8574_get_i2c_bus(i2c_port_t i2c_num) //FUNCION CREADA PARA OBTENER EL HANDLE DE I2C
{
    if (i2c_num < I2C_NUM_0 || i2c_num >= I2C_NUM_MAX) return NULL;
    return s_i2c_buses[i2c_num];
}



/* ------------------------------------------------------------------------------------------------------------------------------------------------------------------ */
/*                          Bus backend: direct GPIO (4-bit and 8-bit)                                                                                                */
/* ------------------------------------------------------------------------------------------------------------------------------------------------------------------ */

static const char TAG_GPIO[] = "HD44780/GPIO";

typedef struct
{
    lcd_bus_hd44780_t base; /* must be first */
    gpio_num_t        rs;
    gpio_num_t        en;
    /* Active data pins: 4 entries for 4-bit (D4..D7), 8 entries for
     * 8-bit (D0..D7). Width is in base.data_width. */
    gpio_num_t data[8];
} gpio_bus_t;

/* Configure a pin as a push-pull output and drive it low. */
static esp_err_t gpio_setup_out(gpio_num_t pin)
{
    if (!GPIO_IS_VALID_OUTPUT_GPIO(pin)) {
        ESP_LOGE(TAG_GPIO, "invalid output GPIO %d", pin);
        return ESP_ERR_INVALID_ARG;
    }

    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    esp_err_t rc = gpio_config(&cfg);
    if (rc == ESP_OK) {
        gpio_set_level(pin, 0);
    }
    return rc;
}

/* Drive E high, hold, drive E low, then wait the standard >37 us
 * instruction-execute time (we use 80 us for safety, matching the I2C
 * backend). */
static void gpio_pulse_en(gpio_bus_t *b)
{
    gpio_set_level(b->en, 1);
    esp_rom_delay_us(1);
    gpio_set_level(b->en, 0);
    esp_rom_delay_us(80);
}

/* Place the high nibble of `nibble` (bits 7..4) onto the four high
 * data pins. In 8-bit mode those are indices 4..7 (D4..D7); in 4-bit
 * mode they are the only data pins (indices 0..3 == panel D4..D7). */
static void gpio_drive_high_nibble(gpio_bus_t *b, uint8_t nibble)
{
    const uint8_t w        = b->base.data_width;
    const uint8_t base_idx = (w == 8) ? 4 : 0;
    for (uint8_t i = 0; i < 4; ++i) {
        gpio_set_level(b->data[base_idx + i], (nibble >> (4 + i)) & 1);
    }
}

static esp_err_t gpio4_write_nibble(lcd_bus_hd44780_t *bus, uint8_t nibble, hd44780_mode_t mode)
{
    gpio_bus_t *b = (gpio_bus_t *)bus;
    gpio_set_level(b->rs, mode == HD44780_MODE_DATA ? 1 : 0);
    gpio_drive_high_nibble(b, nibble);
    gpio_pulse_en(b);
    return ESP_OK;
}

static esp_err_t gpio4_write_byte(lcd_bus_hd44780_t *bus, uint8_t data, hd44780_mode_t mode)
{
    esp_err_t rc = gpio4_write_nibble(bus, data & 0xF0, mode);
    if (rc != ESP_OK) {
        return rc;
    }
    return gpio4_write_nibble(bus, (data << 4) & 0xF0, mode);
}

static esp_err_t gpio8_write_byte(lcd_bus_hd44780_t *bus, uint8_t data, hd44780_mode_t mode)
{
    gpio_bus_t *b = (gpio_bus_t *)bus;
    gpio_set_level(b->rs, mode == HD44780_MODE_DATA ? 1 : 0);
    for (uint8_t i = 0; i < 8; ++i) {
        gpio_set_level(b->data[i], (data >> i) & 1);
    }
    gpio_pulse_en(b);
    return ESP_OK;
}

/* During the 8-bit reset sequence the controller looks at the upper four
 * data lines only, but we still drive the lower four to a known state so
 * the bus has well-defined levels at all times. */
static esp_err_t gpio8_write_nibble(lcd_bus_hd44780_t *bus, uint8_t nibble, hd44780_mode_t mode)
{
    gpio_bus_t *b = (gpio_bus_t *)bus;
    gpio_set_level(b->rs, mode == HD44780_MODE_DATA ? 1 : 0);
    for (uint8_t i = 0; i < 4; ++i) {
        gpio_set_level(b->data[i], 0);
    }
    gpio_drive_high_nibble(b, nibble);
    gpio_pulse_en(b);
    return ESP_OK;
}

static void gpio_destroy(lcd_bus_hd44780_t **bus)
{
    if (!bus || !*bus) {
        return;
    }
    free(*bus);
    *bus = NULL;
}

static gpio_bus_t *gpio_bus_new(gpio_num_t rs, gpio_num_t en, const gpio_num_t *data, uint8_t width)
{
    if (gpio_setup_out(rs) != ESP_OK || gpio_setup_out(en) != ESP_OK) {
        ESP_LOGE(TAG_GPIO, "RS/EN pin config failed");
        return NULL;
    }
    for (uint8_t i = 0; i < width; ++i) {
        if (gpio_setup_out(data[i]) != ESP_OK) {
            ESP_LOGE(TAG_GPIO, "data pin %u config failed", i);
            return NULL;
        }
    }

    gpio_bus_t *b = calloc(1, sizeof(*b));
    if (!b) {
        return NULL;
    }
    b->rs = rs;
    b->en = en;
    for (uint8_t i = 0; i < width; ++i) {
        b->data[i] = data[i];
    }
    b->base.destroy    = gpio_destroy;
    b->base.data_width = width;
    return b;
}

lcd_bus_hd44780_t *lcd_bus_gpio4_create( //
    gpio_num_t rs,                       //
    gpio_num_t en,                       //
    gpio_num_t d4,                       //
    gpio_num_t d5,                       //
    gpio_num_t d6,                       //
    gpio_num_t d7                        //
)

{
    const gpio_num_t data[4] = {d4, d5, d6, d7};
    gpio_bus_t      *b       = gpio_bus_new(rs, en, data, 4);
    if (!b) {
        return NULL;
    }
    b->base.write_byte   = gpio4_write_byte;
    b->base.write_nibble = gpio4_write_nibble;
    return &b->base;
}

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
)

{
    const gpio_num_t data[8] = {d0, d1, d2, d3, d4, d5, d6, d7};
    gpio_bus_t      *b       = gpio_bus_new(rs, en, data, 8);
    if (!b) {
        return NULL;
    }
    b->base.write_byte   = gpio8_write_byte;
    b->base.write_nibble = gpio8_write_nibble;
    return &b->base;
}





