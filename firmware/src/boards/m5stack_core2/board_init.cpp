#include "board.h"
#include "pmu.h"
#include <Arduino.h>
#include <Wire.h>

// Bring up the internal I2C bus, then the PMU: on the Core2 the LCD power,
// LCD reset and backlight are PMU outputs, so they must be up before
// display_hal_init() (firmware/CLAUDE.md, gotcha 4). The TF card shares the
// display's SPI bus; hold its CS high so it never answers display traffic.
extern "C" void board_init(void) {
    pinMode(4, OUTPUT);          // TF card CS
    digitalWrite(4, HIGH);
    Wire.begin(IIC_SDA, IIC_SCL, 400000);
    core2_pmu_init();
}
