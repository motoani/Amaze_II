#pragma once
// Digital pin definitions for Volo's Game Board for ESP32 T-Display S3
// Note that none of these seem to be be RTC wake-up pins
// For the 2023 challenge we only used two but others might have debug functions

#define CONTROL_LEFT GPIO_NUM_43     // The Arduino/ESP32 pin identifiers
#define CONTROL_UP GPIO_NUM_44
#define CONTROL_DOWN GPIO_NUM_18
#define CONTROL_RIGHT GPIO_NUM_17
#define CONTROL_A GPIO_NUM_21
#define CONTROL_B GPIO_NUM_16