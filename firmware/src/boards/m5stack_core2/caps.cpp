#include "../../hal/board_caps.h"
#include "board.h"

static const BoardCaps caps = {
    .name = BOARD_NAME,
    .width = LCD_WIDTH,
    .height = LCD_HEIGHT,
    .round_panel = false,
    // Touch-strip zones A (primary) + C (secondary). The middle zone B is the
    // PWR-role button, handled in power.cpp and not counted here.
    .button_count = (uint8_t)(1 + BOARD_HAS_SECONDARY_BUTTON),
    .has_rotation = (bool)BOARD_HAS_ROTATION,
    .has_battery  = (bool)BOARD_HAS_BATTERY,
    .has_imu      = (bool)BOARD_HAS_IMU,
    .minimal_ui   = false,   // full animated UI (8 MB PSRAM)
    .has_pwr_button = true,  // touch-strip zone B, PWR role: see power.cpp
    .has_touch = true,
    // The side key is the PMU's power key and turns the Core2 off after a
    // few seconds' hold, so the hint must name the B mark instead.
    .pwr_hint = "hold the B button",
};

const BoardCaps& board_caps(void) { return caps; }
