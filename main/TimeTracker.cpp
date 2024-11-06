#include <stdint.h>
#include "structures.h"
#include "globals.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "freertos/event_groups.h"

#include "esp_log.h"

#include "events_global.h"
#include "EventManager.h"

#include "TimeTracker.h"

void TimeTrack (TimerHandle_t tracked_handle)
{
    static const char *TAG = "TimeTrack";
    extern QueueHandle_t game_event_queue; // A FreeRTOS queue to pass game play events from world to manager

    extern Time_tracked time_report;

    // Send an update event every tick to subtract -1 (ie 0xff) from energy
    Near_pix temp_event;
    temp_event.event = EVNT_ENERGY | EE_CHANGE | (0xff <<EVNT_NN_SHIFT) | EE_DISPLAY; 
    if( xQueueSend( game_event_queue, ( void * ) &temp_event, ( TickType_t ) 10 ) != pdPASS )
          {
            // Failed to post the message, even after 10 ticks.
            ESP_LOGI(TAG,"Failed to post item in event queue");
          }
    // Do a brief performance report
    ESP_LOGI(TAG, "Framerate is %d fps and %d triangles",time_report.frames,(int)time_report.triangles);
    time_report.frames = 0;
    time_report.triangles = 0;
} // End of TimeTrack