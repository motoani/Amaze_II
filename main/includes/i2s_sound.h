#pragma once

#include "sdkconfig.h"

#include "amaze_II_main.h"
//#ifdef __cplusplus
//extern "C" {
//#endif

// The pins in use for I2S audio Tx
// Avoid Gamer buttons and defaults on each board
#if defined T_DISPLAY_S3_GAMER || defined T_DISPLAY_S3
    #define I2S_BCLK_IO1        GPIO_NUM_2      // I2S bit clock io number - 33 to 37 are N/C on T-Display-S3
    #define I2S_WS_IO1          GPIO_NUM_1      // I2S word select io number - free and not used by GAMER buttons
    #define I2S_DOUT_IO1        GPIO_NUM_3      // I2S data out io number
#endif
#ifdef T_QT_PRO
    #define I2S_BCLK_IO1        GPIO_NUM_34      // I2S bit clock io number - 33 to 37 are N/C on T-Display-S3
    #define I2S_WS_IO1          GPIO_NUM_33      // I2S word select io number - free and not used by GAMER buttons
    #define I2S_DOUT_IO1        GPIO_NUM_39      // I2S data out io number
#endif


//#define I2S_PDM                            // To trial PDM output with no I2S codec

//#ifdef __cplusplus
//}
//#endif


//#ifdef __cplusplus
//extern "C" {
//#endif
void i2s_init(void);
//#ifdef __cplusplus
//}
//#endif

//#ifdef __cplusplus
//extern "C" {
//#endif
void i2s_write_task(void *args);
//#ifdef __cplusplus
//}
//#endif


