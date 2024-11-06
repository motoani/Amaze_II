#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "esp_lcd_panel_vendor.h"

// Access to global variables
extern bool flipped;
extern EventGroupHandle_t raster_event_group;
extern uint16_t * frame_buffer_A;
extern uint16_t * frame_buffer_B;
extern esp_lcd_panel_handle_t panel_handle;

void ShowWorld(void * parameter);

void SendFlippedFrame(void);

