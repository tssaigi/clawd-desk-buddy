#include "../../hal/board_info_hal.h"
#include "pmu.h"

// Reports which Core2 revision booted, for the `info` serial command.
const char* board_hal_rev(void) {
    switch (core2_pmu_type()) {
    case CORE2_PMU_AXP192:  return "v1.0_axp192";
    case CORE2_PMU_AXP2101: return "v1.1_axp2101";
    default:                return "";
    }
}
