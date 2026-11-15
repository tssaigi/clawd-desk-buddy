#include "../../hal/input_hal.h"
#include "touch_strip.h"

// The Core2 has no front push-buttons: A and C are the left and right thirds
// of the touch strip under the display (touch.cpp). B, the middle third, is
// the PWR-role button and lives in power.cpp.

void input_hal_init(void) {}

bool input_hal_is_held(InputButton btn) {
    switch (btn) {
    case INPUT_BTN_PRIMARY:   return core2_zone_held(CORE2_ZONE_A);
    case INPUT_BTN_SECONDARY: return core2_zone_held(CORE2_ZONE_C);
    }
    return false;
}
