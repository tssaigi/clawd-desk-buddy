#include "../../hal/power_hal.h"
#include "board.h"
#include "pmu.h"
#include "touch_strip.h"
#include <Arduino.h>

// Battery, charging and USB state come from the PMU (pmu.cpp: AXP192 on
// v1.0, AXP2101 on v1.1). Hardware power on/off stays with the PMU's own side
// key, so this port never synthesizes a software power-off.
//
// The PWR-role button is touch-strip zone B (the middle printed mark). Same
// software edge synthesis as the M5Stack FIRE so the hold-to-pair gesture in
// main.cpp stays board-agnostic:
//   short    — fired on release if the hold was shorter than PWR_LONG_MS
//   long     — fired once when a hold crosses PWR_LONG_MS
//   release  — fired on every release edge

#define BATTERY_POLL_MS  2000
#define CHARGING_POLL_MS 500
#define PWR_POLL_MS      50
#define PWR_LONG_MS      1500

static int      cached_pct        = -1;
static bool     cached_charging   = false;
static bool     cached_vbus       = false;
static bool     pwr_pressed_flag  = false;
static bool     pwr_long_flag     = false;
static bool     pwr_released_flag = false;
static bool     last_pwr_state    = false;
static uint32_t pwr_press_started_ms = 0;
static bool     pwr_long_fired    = false;
static uint32_t last_battery_ms   = 0;
static uint32_t last_charging_ms  = 0;
static uint32_t last_pwr_ms       = 0;

void power_hal_init(void) {
    // core2_pmu_init() already ran in board_init(): the LCD rails depend on it.
    cached_pct      = core2_pmu_battery_pct();
    cached_charging = core2_pmu_is_charging();
    cached_vbus     = core2_pmu_is_vbus_in();
}

void power_hal_tick(void) {
    uint32_t now = millis();

    if (now - last_battery_ms >= BATTERY_POLL_MS) {
        last_battery_ms = now;
        cached_pct = core2_pmu_battery_pct();
    }
    if (now - last_charging_ms >= CHARGING_POLL_MS) {
        last_charging_ms = now;
        cached_charging = core2_pmu_is_charging();
        cached_vbus     = core2_pmu_is_vbus_in();
    }
    if (now - last_pwr_ms >= PWR_POLL_MS) {
        last_pwr_ms = now;
        bool pwr_now = core2_zone_held(CORE2_ZONE_B);
        if (pwr_now && !last_pwr_state) {            // press edge — hold begins
            pwr_press_started_ms = now;
            pwr_long_fired = false;
        } else if (pwr_now && last_pwr_state) {      // held
            if (!pwr_long_fired && (now - pwr_press_started_ms >= PWR_LONG_MS)) {
                pwr_long_flag  = true;
                pwr_long_fired = true;
            }
        } else if (!pwr_now && last_pwr_state) {     // release edge
            pwr_released_flag = true;
            if (!pwr_long_fired) pwr_pressed_flag = true;  // short press
        }
        last_pwr_state = pwr_now;
    }
}

int  power_hal_battery_pct(void) { return cached_pct; }
bool power_hal_is_charging(void) { return cached_charging; }
bool power_hal_is_vbus_in(void)  { return cached_vbus; }

bool power_hal_pwr_pressed(void) {
    if (pwr_pressed_flag) { pwr_pressed_flag = false; return true; }
    return false;
}

bool power_hal_pwr_long_pressed(void) {
    if (pwr_long_flag) { pwr_long_flag = false; return true; }
    return false;
}

bool power_hal_pwr_released(void) {
    if (pwr_released_flag) { pwr_released_flag = false; return true; }
    return false;
}
