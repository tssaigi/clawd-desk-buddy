#pragma once

// M5Stack Core2 — classic ESP32 (D0WDQ6-V3, 16 MB flash, 8 MB PSRAM) with a
// 320x240 ILI9342 TFT over 4-wire SPI, an FT6336U capacitive touch panel that
// extends 40 px below the display (the three printed A/B/C "buttons"), an
// AXP power-management IC that owns the LCD power, reset and backlight, an
// NS4168 I2S speaker amp, an MPU6886 IMU and a 390 mAh (500 mAh since 2023) cell.
//
// Two hardware revisions exist and this port handles both at run time:
//   v1.0: AXP192 PMU   (LCD reset = AXP GPIO4, backlight = DCDC3)
//   v1.1: AXP2101 PMU  (LCD + touch reset = ALDO2, backlight = BLDO1)
// Both answer at I2C 0x34; register 0x03 tells them apart (0x03 vs 0x4A).
// Pin map, rail sequence and register values follow M5GFX's / M5Unified's
// board_M5StackCore2 bring-up. USB is a UART bridge (CP2104 or CH9102), so
// the env must not set ARDUINO_USB_CDC_ON_BOOT.

#define BOARD_NAME           "M5Stack Core2"

// ---- Display geometry (ILI9342 is natively 320x240 landscape) ----
#define LCD_WIDTH            320
#define LCD_HEIGHT           240

// ---- SPI display pins (shared with the TF slot, whose CS is GPIO 4) ----
#define LCD_CS               5
#define LCD_SCLK             18
#define LCD_MOSI             23
#define LCD_MISO             38
#define LCD_DC               15
// No LCD reset or backlight GPIO: both live on the PMU (pmu.cpp).

// Later Core2 units ship an ILI9342E instead of the ILI9342C. The C init
// that Arduino_GFX sends lights both, but the E needs its own gamma/VCOM
// list for correct colours. Build with -DCORE2_PANEL_ILI9342E=1 if the
// colours look washed out or wrong on your unit.
#ifndef CORE2_PANEL_ILI9342E
#define CORE2_PANEL_ILI9342E 0
#endif

// ---- Internal I2C bus (PMU, touch, IMU, RTC) ----
#define IIC_SDA              21
#define IIC_SCL              22
#define PMU_ADDR             0x34   // AXP192 (v1.0) or AXP2101 (v1.1)
#define TOUCH_ADDR           0x38   // FT6336U
#define TOUCH_INT_GPIO       39     // low while a finger is down

// ---- Touch-strip buttons ----
// The touch panel reports y up to 279; rows 240..279 sit under the printed
// A/B/C marks below the display. They are split in thirds:
//   A (left)   → primary,   HID Space (voice-mode PTT)
//   B (middle) → PWR role,  brightness / hold-to-pair (power.cpp)
//   C (right)  → secondary, HID Shift+Tab (mode toggle)
// The physical side button stays the PMU's hardware power key (press to
// turn on, hold about 4 s to turn off); the firmware does not read it.
#define TOUCH_STRIP_Y        240
#define TOUCH_STRIP_Y_MAX    279

// ---- Capability flags ----
#define BOARD_HAS_SECONDARY_BUTTON 1
#define BOARD_HAS_ROTATION         0    // fixed desk orientation
#define BOARD_HAS_IMU              0    // MPU6886 populated but unused
#define BOARD_HAS_BATTERY          1    // via the PMU
#define BOARD_HAS_IO_EXPANDER      0    // the PMU gates LCD power/reset instead (board_init)
#define BOARD_HAS_SOUND            0    // NS4168 I2S amp, not the ES8311 chime engine
