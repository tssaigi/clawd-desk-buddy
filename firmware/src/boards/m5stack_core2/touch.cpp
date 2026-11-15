#include "../../hal/touch_hal.h"
#include "board.h"
#include "touch_strip.h"
#include <Arduino.h>
#include <Wire.h>

// FT6336U over the shared internal I2C bus. Its coordinates already match the
// upright 320x240 display (M5GFX uses an identity affine at this rotation),
// with y running on to 279 across the printed A/B/C strip below the glass.
// Touches on the display go to LVGL; touches on the strip become buttons
// (touch_strip.h) and are never reported to LVGL.
//
// This is the only reader of the controller (firmware/CLAUDE.md, gotcha 5).
// A sample is a single 5-byte burst, about 0.2 ms at 400 kHz, and is cached
// for SAMPLE_MS so LVGL, input.cpp and power.cpp can all ask without extra
// bus traffic.

#define SAMPLE_MS 10

static bool     down      = false;
static uint16_t raw_x     = 0;
static uint16_t raw_y     = 0;
static uint32_t last_ms   = 0;
static bool     sampled   = false;

static void sample(void) {
    uint32_t now = millis();
    if (sampled && now - last_ms < SAMPLE_MS) return;
    sampled = true;
    last_ms = now;

    // INT idles high and stays low while a finger is down: skip the bus
    // entirely when nothing touches the panel.
    if (digitalRead(TOUCH_INT_GPIO) == HIGH) {
        down = false;
        return;
    }

    Wire.beginTransmission(TOUCH_ADDR);
    Wire.write(0x02);                         // TD_STATUS, then P1 XH XL YH YL
    if (Wire.endTransmission(false) != 0 ||
        Wire.requestFrom((uint8_t)TOUCH_ADDR, (uint8_t)5) != 5) {
        down = false;
        return;
    }
    uint8_t b[5];
    for (int i = 0; i < 5; i++) b[i] = Wire.read();

    uint8_t points = b[0] & 0x0F;
    if (points == 0 || points > 2) {
        down = false;
        return;
    }
    raw_x = (uint16_t)(((b[1] & 0x0F) << 8) | b[2]);
    raw_y = (uint16_t)(((b[3] & 0x0F) << 8) | b[4]);
    if (raw_x >= LCD_WIDTH) raw_x = LCD_WIDTH - 1;
    if (raw_y > TOUCH_STRIP_Y_MAX) raw_y = TOUCH_STRIP_Y_MAX;
    down = true;
}

void touch_hal_init(void) {
    pinMode(TOUCH_INT_GPIO, INPUT);   // input-only GPIO, pulled up on the board
    // Interrupt mode "trigger" (0xA4 = 0) holds INT low for as long as a finger
    // is down, which sample() relies on.
    Wire.beginTransmission(TOUCH_ADDR);
    Wire.write(0xA4);
    Wire.write(0x00);
    Wire.endTransmission();
}

void touch_hal_read(uint16_t* x, uint16_t* y, bool* pressed) {
    sample();
    if (down && raw_y < TOUCH_STRIP_Y) {
        *x = raw_x;
        *y = raw_y;
        *pressed = true;
    } else {
        *x = 0;
        *y = 0;
        *pressed = false;
    }
}

bool core2_zone_held(Core2Zone zone) {
    sample();
    if (!down || raw_y < TOUCH_STRIP_Y) return false;
    int third = raw_x * 3 / LCD_WIDTH;        // 0, 1, 2
    return third == (int)zone;
}
