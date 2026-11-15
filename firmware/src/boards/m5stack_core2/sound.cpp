#include "../../hal/sound_hal.h"

// The Core2's speaker is an NS4168 I2S amplifier (BCK 12, WS 0, DATA 2,
// enabled by the PMU: AXP192 GPIO2 / AXP2101 ALDO3). The shared chime engine
// (../../chime.cpp) streams through an ES8311 codec, so sound is a no-op for
// now and pmu.cpp keeps the amp switched off. A later revision can stream the
// ding straight to I2S here.

void sound_hal_init(void) {}
void sound_hal_tick(void) {}
void sound_hal_chime(void) {}
void sound_hal_tune(float gain, int volume) { (void)gain; (void)volume; }
