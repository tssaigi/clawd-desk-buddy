#include "../../hal/imu_hal.h"

// An MPU6886 is populated on the Core2 (I2C 0x68), but this port runs at a
// fixed desk orientation, so rotation is off and the IMU stays uninitialized
// (same posture as the M5Stack FIRE).

void    imu_hal_init(void) {}
void    imu_hal_tick(void) {}
uint8_t imu_hal_rotation_quadrant(void) { return 0; }
