#include "pmu.h"
#include "board.h"
#include <Arduino.h>
#include <Wire.h>

static Core2Pmu pmu = CORE2_PMU_NONE;

static bool rd(uint8_t reg, uint8_t* out, uint8_t n = 1) {
    Wire.beginTransmission(PMU_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint8_t)PMU_ADDR, n) != n) return false;
    for (uint8_t i = 0; i < n; i++) out[i] = Wire.read();
    return true;
}

static void wr(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(PMU_ADDR);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}

// new = (old & keep) | set, the same form as M5GFX's register tables.
static void upd(uint8_t reg, uint8_t set, uint8_t keep) {
    uint8_t v = 0;
    if (!rd(reg, &v)) return;
    wr(reg, (uint8_t)((v & keep) | set));
}
static void bit_on(uint8_t reg, uint8_t bits)  { upd(reg, bits, 0xFF); }
static void bit_off(uint8_t reg, uint8_t bits) { upd(reg, 0, (uint8_t)~bits); }

// ---- AXP192 (Core2 v1.0) ----
static void axp192_init(void) {
    static const uint8_t regs[][2] = {
        { 0x26, 0x6A },   // DCDC1 3.35 V (ESP32 VDD)
        { 0x30, 0x02 },   // VBUS-IPSOUT path, no VHOLD limit
        { 0x31, 0x04 },   // VOFF 3.0 V
        { 0x32, 0x42 },   // battery detection on
        { 0x33, 0xC3 },   // charge on, 4.2 V, 390 mA (the Core2's 390 mAh cell)
        { 0x35, 0xA2 },   // RTC backup battery charge
        { 0x36, 0x0C },   // power key: 128 ms on, 4 s off
        { 0x40, 0x00 }, { 0x41, 0x00 }, { 0x42, 0x03 }, { 0x43, 0x00 }, { 0x44, 0x00 },
        { 0x82, 0xFF },   // all ADCs on (battery voltage)
        { 0x83, 0x80 },
        { 0x84, 0x32 },   // ADC 25 Hz
        { 0x90, 0x07 },   // GPIO0 floating
        { 0x91, 0xA0 },
        { 0x92, 0x02 },   // GPIO1 = PWM (green LED)
        { 0x98, 0x00 }, { 0x99, 0xFF }, { 0x9A, 0xFF },   // LED off
        { 0x93, 0x05 },   // GPIO2 low: speaker amp off
    };
    for (auto& r : regs) wr(r[0], r[1]);
    upd(0x28, 0xF0, 0x0F);   // LDO2 3.3 V: LCD + TF power
    bit_on(0x12, 0x04);      // LDO2 on
    bit_off(0x12, 0x08);     // LDO3 off: vibration motor
    upd(0x95, 0x84, 0x72);   // GPIO4 as output (LCD reset)
    bit_on(0x96, 0x02);      // release LCD reset
}

static void axp192_lcd_reset(void) {
    bit_off(0x96, 0x02);
    delay(2);
    bit_on(0x96, 0x02);
}

static void axp192_backlight(uint8_t level) {
    if (level) {
        bit_on(0x12, 0x02);                              // DCDC3 on
        upd(0x27, (uint8_t)((level >> 3) + 72), 0x80);   // about 2.5 .. 3.3 V
    } else {
        bit_off(0x12, 0x02);
    }
}

// ---- AXP2101 (Core2 v1.1) ----
static void axp2101_init(void) {
    upd(0x23, 0x00, 0xE0);   // no PMIC power-off on DCDC undervoltage (brown-out resets instead)
    upd(0x90, 0x08, 0x7B);   // ALDO4 on (TF, touch, LCD power); ALDO3 (speaker) and DLDO1 (motor) off
    bit_on(0x80, 0x05);      // DCDC1 + DCDC3 on
    wr(0x82, 0x12);          // DCDC1 3.3 V
    wr(0x84, 0x6A);          // DCDC3 3.3 V
    wr(0x27, 0x00);          // power key: 1 s on, 4 s off
    wr(0x10, 0x30);          // internal off-discharge
    wr(0x12, 0x00);          // BATFET control default
    wr(0x68, 0x01);          // battery detection on
    wr(0x69, 0x13);          // charge LED
    wr(0x99, 0x00);          // DLDO1 0.5 V (motor idle)
    wr(0x30, 0x0F);          // ADCs on
    wr(0x64, 0x03);          // charge to 4.2 V
    bit_on(0x50, 0x10);      // no thermistor on TS: ignore it for charging
    bit_on(0x18, 0x08);      // fuel gauge on
    bit_on(0x90, 0x02);      // ALDO2 on: release LCD + touch reset
}

static void axp2101_lcd_reset(void) {
    bit_off(0x90, 0x02);
    delay(2);
    bit_on(0x90, 0x02);
}

static void axp2101_backlight(uint8_t level) {
    if (level) {
        wr(0x96, (uint8_t)((level + 641) >> 5));   // BLDO1 about 2.5 .. 3.3 V
        bit_on(0x90, 0x10);
    } else {
        bit_off(0x90, 0x10);
    }
}

// ---- shared ----

// Rough LiPo open-circuit curve, used when the PMU has no fuel gauge (AXP192)
// or the gauge has not settled (AXP2101 reports > 100 right after boot).
static int pct_from_mv(int mv) {
    static const int16_t pts[][2] = {
        { 3300, 0 }, { 3500, 5 }, { 3600, 10 }, { 3700, 30 }, { 3750, 45 },
        { 3800, 55 }, { 3850, 65 }, { 3900, 72 }, { 4000, 85 }, { 4100, 95 }, { 4150, 100 },
    };
    const int n = sizeof(pts) / sizeof(pts[0]);
    if (mv <= pts[0][0]) return 0;
    if (mv >= pts[n - 1][0]) return 100;
    for (int i = 1; i < n; i++) {
        if (mv < pts[i][0]) {
            int dv = pts[i][0] - pts[i - 1][0];
            int dp = pts[i][1] - pts[i - 1][1];
            return pts[i - 1][1] + (mv - pts[i - 1][0]) * dp / dv;
        }
    }
    return 100;
}

Core2Pmu core2_pmu_init(void) {
    uint8_t id = 0;
    if (!rd(0x03, &id)) {
        Serial.println("Core2: no PMU at 0x34");
        pmu = CORE2_PMU_NONE;
        return pmu;
    }
    if (id == 0x03) {
        pmu = CORE2_PMU_AXP192;
        axp192_init();
        Serial.println("Core2: AXP192 (v1.0)");
    } else if (id == 0x4A) {
        pmu = CORE2_PMU_AXP2101;
        axp2101_init();
        Serial.println("Core2: AXP2101 (v1.1)");
    } else {
        Serial.printf("Core2: unknown PMU id 0x%02X\n", id);
        pmu = CORE2_PMU_NONE;
    }
    delay(5);   // LCD rail settles; the panel answers about 2 ms after power
    return pmu;
}

Core2Pmu core2_pmu_type(void) { return pmu; }

void core2_pmu_lcd_reset(void) {
    if (pmu == CORE2_PMU_AXP192)       axp192_lcd_reset();
    else if (pmu == CORE2_PMU_AXP2101) axp2101_lcd_reset();
    delay(120);   // ILI9342 reloads its registers after a reset (datasheet: up to 120 ms)
}

void core2_pmu_set_backlight(uint8_t level) {
    if (pmu == CORE2_PMU_AXP192)       axp192_backlight(level);
    else if (pmu == CORE2_PMU_AXP2101) axp2101_backlight(level);
}

int core2_pmu_battery_pct(void) {
    uint8_t b[2];
    if (pmu == CORE2_PMU_AXP192) {
        uint8_t st = 0;
        if (rd(0x01, &st) && !(st & 0x20)) return -1;      // no battery
        if (!rd(0x78, b, 2)) return -1;
        int raw = (b[0] << 4) | (b[1] & 0x0F);              // 12 bit, 1.1 mV/LSB
        return pct_from_mv(raw * 11 / 10);
    }
    if (pmu == CORE2_PMU_AXP2101) {
        uint8_t st = 0;
        if (rd(0x00, &st) && !(st & 0x08)) return -1;      // no battery
        uint8_t g = 0;
        if (rd(0xA4, &g) && g <= 100) return g;             // fuel gauge
        if (!rd(0x34, b, 2)) return -1;
        return pct_from_mv(((b[0] & 0x3F) << 8) | b[1]);    // 14 bit, mV
    }
    return -1;
}

bool core2_pmu_is_charging(void) {
    uint8_t v = 0;
    if (pmu == CORE2_PMU_AXP192)  return rd(0x00, &v) && (v & 0x04);
    if (pmu == CORE2_PMU_AXP2101) return rd(0x01, &v) && ((v & 0x60) == 0x20);
    return false;
}

bool core2_pmu_is_vbus_in(void) {
    uint8_t v = 0;
    if (pmu == CORE2_PMU_AXP192)  return rd(0x00, &v) && (v & 0xA0);   // ACIN or VBUS present
    if (pmu == CORE2_PMU_AXP2101) return rd(0x00, &v) && (v & 0x20);   // VBUS good
    return false;
}
