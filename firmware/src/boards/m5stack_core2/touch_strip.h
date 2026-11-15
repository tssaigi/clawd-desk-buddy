#pragma once

// Board-private: the three touch "buttons" in the strip under the display.
// touch.cpp owns the FT6336U and is the only place that reads it; input.cpp
// and power.cpp ask it for the strip state here.

enum Core2Zone { CORE2_ZONE_A = 0, CORE2_ZONE_B = 1, CORE2_ZONE_C = 2 };

// True while a finger rests on that zone. Polls the controller if the
// cached sample is stale, so it works even when LVGL is not reading touch.
bool core2_zone_held(Core2Zone zone);
