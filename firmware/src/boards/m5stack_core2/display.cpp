#include "../../hal/display_hal.h"
#include "board.h"
#include "pmu.h"
#include <Arduino.h>
#include <Arduino_GFX_Library.h>

// ILI9342 over 4-wire SPI, the same panel family and orientation as the
// M5Stack FIRE (rotation 2 = upright 320x240 with the touch strip below).
// The differences from the FIRE: the reset line and the backlight are PMU
// outputs (pmu.cpp), not GPIOs, and the panel needs inversion on.
//
// Later units carry an ILI9342E. With CORE2_PANEL_ILI9342E=1 its own
// gamma/VCOM list (from M5GFX's Panel_ILI9342E) is sent after the standard
// init. Brightness is the PMU's backlight rail.

static Arduino_DataBus* bus = nullptr;
static Arduino_ILI9342* gfx = nullptr;

#if CORE2_PANEL_ILI9342E
static void send_ili9342e_init(void) {
    static const uint8_t list[] = {
        // cmd, n, data...
        0xDD, 1, 0x01,                  // external command set on
        0xD5, 1, 0x00,
        0xB1, 1, 0x22,
        0xC8, 1, 0x38,                  // VCOM
        0xCB, 1, 0x1C,
        0xC9, 1, 0x1A,
        0xCA, 1, 0x1A,
        0xB7, 4, 0x5A, 0x41, 0x11, 0x19,
        0xE4, 15, 0x04, 0x08, 0x11, 0x06, 0x12, 0x07, 0x3A, 0x76, 0x47, 0x07, 0x0F, 0x0A, 0x11, 0x19, 0x05,
        0xE5, 15, 0x02, 0x03, 0x07, 0x06, 0x12, 0x07, 0x36, 0x5F, 0x48, 0x06, 0x10, 0x0C, 0x16, 0x14, 0x09,
    };
    bus->beginWrite();
    for (size_t i = 0; i < sizeof(list);) {
        uint8_t cmd = list[i++];
        uint8_t n   = list[i++];
        bus->writeCommand(cmd);
        for (uint8_t k = 0; k < n; k++) bus->write(list[i++]);
    }
    bus->endWrite();
}
#endif

void display_hal_init(void) {
    bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCLK, LCD_MOSI, LCD_MISO);
    // No reset GPIO (the PMU holds it); ips=true — the Core2 panel is inverted.
    gfx = new Arduino_ILI9342(bus, GFX_NOT_DEFINED, 2 /* rotation */, true /* ips */);
}

void display_hal_begin(void) {
    core2_pmu_lcd_reset();
    gfx->begin(40000000);   // 40 MHz — M5GFX's write clock for this panel
#if CORE2_PANEL_ILI9342E
    send_ili9342e_init();
#endif
    gfx->fillScreen(0x0000);
    core2_pmu_set_backlight(200);
}

void display_hal_set_brightness(uint8_t level) {
    core2_pmu_set_backlight(level);
}

void display_hal_draw_bitmap(int32_t x, int32_t y, int32_t w, int32_t h,
                             const uint16_t* pixels) {
    if (gfx) gfx->draw16bitRGBBitmap(x, y, (uint16_t*)pixels, w, h);
}

void display_hal_tick(void) {
    // No rotation cycle on this board.
}

// ILI9342 over SPI has no flush-region alignment requirement.
void display_hal_round_area(int32_t* x1, int32_t* y1, int32_t* x2, int32_t* y2) {
    (void)x1; (void)y1; (void)x2; (void)y2;
}
