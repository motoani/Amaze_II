#pragma once

#include <stdio.h>

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"

// Other congigurations constants

// Supported alignment: 16, 32, 64. A higher alignment can enables higher burst transfer size, thus a higher i80 bus throughput.
#define EXAMPLE_PSRAM_DATA_ALIGNMENT   64 // This must be a power of two, and at least 32 for the cache clearance

extern bool flipped;
extern uint16_t * frame_buffer_A;
extern uint16_t * frame_buffer_B;

// Prototypes
#ifdef __cplusplus
extern "C" {
#endif

void init_lcd_bus(esp_lcd_panel_io_handle_t *io_handle);
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



