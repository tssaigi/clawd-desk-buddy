#pragma once
#include <stdint.h>

// Board-private driver for the Core2's power IC. v1.0 boards carry an
// AXP192, v1.1 boards an AXP2101; core2_pmu_init() detects which and every
// other call dispatches on it. Register values follow M5GFX / M5Unified's
// board_M5StackCore2 setup.

enum Core2Pmu { CORE2_PMU_NONE = 0, CORE2_PMU_AXP192, CORE2_PMU_AXP2101 };

// Detect the PMU, set up charging and battery measurement, and power the
// LCD/touch rails. Must run after Wire.begin() and before the display.
Core2Pmu core2_pmu_init(void);
Core2Pmu core2_pmu_type(void);

// Pulse the LCD (and on v1.1 the touch) reset line held by the PMU.
void core2_pmu_lcd_reset(void);

// Backlight, 0 = off, 255 = brightest.
void core2_pmu_set_backlight(uint8_t level);

int  core2_pmu_battery_pct(void);   // 0..100, -1 if unknown
bool core2_pmu_is_charging(void);
bool core2_pmu_is_vbus_in(void);
