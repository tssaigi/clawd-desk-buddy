# M5Stack Core2 — `m5stack_core2`

Hardware notes for this board. Shared firmware rules are in `firmware/CLAUDE.md`.

M5Stack Core2: classic ESP32 (16 MB flash, 8 MB PSRAM), ILI9342 320×240 over 4-wire SPI, FT6336U touch, NS4168 speaker, MPU6886, 390 or 500 mAh cell. Build env: `m5stack_core2`. Like the FIRE, USB is a UART bridge (CP2104 or CH9102), so the env must **not** set `ARDUINO_USB_CDC_ON_BOOT`.

- **Two revisions, one build.** v1.0 has an AXP192, v1.1 an AXP2101, both at I2C 0x34; `pmu.cpp` reads register 0x03 (0x03 vs 0x4A) and dispatches. `info` reports `v1.0_axp192` or `v1.1_axp2101`.
- **The PMU owns the panel**: LCD power (AXP192 LDO2 / AXP2101 ALDO4), LCD reset (AXP192 GPIO4 / AXP2101 ALDO2, which also resets touch) and backlight (AXP192 DCDC3 / AXP2101 BLDO1). `board_init()` powers them before the display; `display_hal_begin()` pulses reset through the PMU.
- **Panel variant**: some units have an ILI9342E. `-DCORE2_PANEL_ILI9342E=1` sends M5GFX's E gamma/VCOM list after the C init. Not auto-detected yet.
- **Buttons are touch zones.** The touch panel runs on to y = 279 under the glass; thirds of that strip are A (Space), B (PWR role: brightness, hold to pair) and C (Shift+Tab). `touch.cpp` is the only reader of the controller and hides strip touches from LVGL; `touch_strip.h` hands the zone state to `input.cpp` and `power.cpp`.
- The physical side key is the PMU's power key (on, and about 4 s hold for off). The firmware does not read it.
- The TF card shares the display SPI bus; `board_init()` holds its CS (GPIO 4) high.
- No sound yet (NS4168 is plain I2S, not the ES8311 chime path). IMU unused.
- **Pages and gestures** (shared code, touch boards): `gestures.cpp` (tap, swipe up = face, swipes = pages), `sessions_view.cpp`, `colony_view.cpp`, `visitor.cpp` (Clawd roaming a side page), fed over serial by `core2/bridge/clawd_status_bridge.py` (`status`, `sess ...`, `col ...`). Their tables live in PSRAM: this chip's static DRAM is nearly full.
- **Startup QR** (`boot_qr.cpp`): on only in this env (`-DLV_USE_QRCODE=1`, `-DCLAWD_REPO_URL`). `bootqr on|off|show` over serial; the setting is in NVS.
