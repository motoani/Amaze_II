#pragma once

#include <stdio.h>

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"

// The pixel number in horizontal and vertical - note that X Y swap is active
#define EXAMPLE_LCD_H_RES              320
#define EXAMPLE_LCD_V_RES              170

// Other congigurations constants
#define CONFIG_EXAMPLE_LCD_I80_BUS_WIDTH 8
// Supported alignment: 16, 32, 64. A higher alignment can enables higher burst transfer size, thus a higher i80 bus throughput.
#define EXAMPLE_PSRAM_DATA_ALIGNMENT   64 // This must be a power of two, and at least 32 for the cache clearance

extern bool flipped;
extern uint16_t * frame_buffer_A;
extern uint16_t * frame_buffer_B;

// Prototypes
#ifdef __cplusplus
extern "C" {
#endif
void init_lcd_i80_bus(esp_lcd_panel_io_handle_t *io_handle);
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif
void init_lcd_panel(esp_lcd_panel_io_handle_t io_handle, esp_lcd_panel_handle_t *panel);
#ifdef __cplusplus
}
#endif



