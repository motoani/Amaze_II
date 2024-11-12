#pragma once
#include "amaze_II_main.h"

// The Arduino/ESP32 pin identifiers from datasheets
// For the 2023 challenge we only used two but others might have debug functions

#ifdef T_DISPLAY_S3
    // Lilygo T Dsiplay S3 with no additional button
    #define CONTROL_LEFT GPIO_NUM_0 // Boot button
    #define CONTROL_RIGHT GPIO_NUM_14
#endif

#ifdef T_DISPLAY_S3_GAMER
    // Digital pin definitions for Volo's Game Board for ESP32 T-Display S3
    // Note that none of these seem to be be RTC wake-up pins

    #define CONTROL_LEFT GPIO_NUM_43
    #define CONTROL_UP GPIO_NUM_44
    //#define CONTROL_DOWN GPIO_NUM_18
    #define CONTROL_RIGHT GPIO_NUM_17
    //#define CONTROL_A GPIO_NUM_21
    //#define CONTROL_B GPIO_NUM_16
#endif

#ifdef T_QT_PRO
    #define CONTROL_LEFT GPIO_NUM_0 
    #define CONTROL_RIGHT GPIO_NUM_47
#endif