#include <stdint.h>
#include <math.h>
#include <algorithm>
#include <vector>

#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_cache.h"

//#include "esp_lcd_panel_io.h"
//#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"

#include <freertos/FreeRTOS.h>
//#include <freertos/task.h>
#include "freertos/event_groups.h"

#include "geometry.h"
#include "structures.h"
#include "globals.h"
#include "buttons.h"
#include "events_global.h"

#include "TriangleQueues.h" // For EmptyQueue
#include "ChunkChooser.h"
#include "CheckTriangles.h"
#include "RasteriseBox.h"
#include "EventManager.h"

#include "ShowWorld.h"

extern std::vector<EachLayout> world; // An unsized vector of layouts each of which can contain multiple frames

// Position and movement are global
extern Vec3f eye;
extern float eye_level;
extern Vec3f direction;

extern Time_tracked time_report; // Health and fps etc for reporting
extern QueueHandle_t game_event_queue; // A FreeRTOS queue to pass game play events from world to manager

// This module maintains the 2D overlay description but not the actual buffer
TwoD_overlay score_overlay;

bool OverlayFlag = false;

void ShowWorld(void * parameter)
{
static const char *TAG = "ShowWorld";
static uint32_t max_pixel_count = 5 * g_scHeight * g_scWidth ; // Pick a start value to initialise
static int64_t elapsed_time = esp_timer_get_time(); // Internal microsecond clock
static uint32_t frame_time;


static  Vec3f scaled_direction;
float spot_height = 0.0f; // The result of mapping height will be put here
static float old_spot_height = 0.0f; // The previous spot height found
float delta_height = 0.0f; // change in height since last frame;
float fallen = 0.0f; // Accumulate fallen distance at a rate

static bool control_not_pressed = true; // Track whether player has a control down, if not we can render more!
  
WorldLayout * this_world_ptr;// ptr to const world

xEventGroupSetBits( raster_event_group, GAME_RUNNING);

ESP_LOGI(TAG, "ShowWorld task about to enter infinite loop");

while(1)
{ // Loop forever as a task
//ESP_LOGI(TAG, "ShowWorld task looping");

// Manage pingpong
flipped = ! flipped;

// Needs depth buffer cleared before sending, depth could be adjusted to limit rendering
ClearDepthBuffer(200.0f); // Just the one to clear before rasterise

// Oddly the rasterising is started at the start of the loop which seems unexpected but sets
// the two threads working nicely
xEventGroupSetBits( raster_event_group, START_RASTER);

SendFlippedFrame();

        // Start a new screen render
        uint32_t this_frame; // Track frame index
        const uint32_t frame_period = (1000 * 100); // A constant period per animation frame

        // Find the eye / viewer's height based on current position
        // We assume that the base plan is in world[0] and single frame
        // set the pointer to the layout in use
        this_world_ptr = & (world[0].frame_layouts[0]);

        // Which chunk is the eye in? We don't need to check deeper
        // The moved flag is set true to force check of [0,0] offset
        uint32_t base_chunk = ChunkChooser(eye, direction, true, this_world_ptr);

        // Is the location in the world? If not, assume same as before !zero
        if (base_chunk == INVALID_CHUNK) spot_height = old_spot_height; // 0.0f;
        else
        {
          // Current location is in the world so find height
          spot_height = BaseTriangles(eye, direction, base_chunk, this_world_ptr);
          delta_height = spot_height - old_spot_height; // This is an absolute change and will be scaled later
          old_spot_height = spot_height;
        }
        //ESP_LOGI(TAG,"BASE %x SPOT %f",(int)base_chunk,spot_height);

        //ESP_LOGI(TAG, "Spot height is %f",spot_height);
        // Adjust eye height based on what has been found
        eye.y = spot_height + eye_level;

    // Now the rendering can take place
    // reduce the search but don't let it get too small or needlessly big
    // 99ms is an arbitrary maximum
    // The MCU clock can't be used to limit this as this thread is not rate limiting 
    max_pixel_count = std::clamp(max_pixel_count+(MAX_FRAME_DURATION-frame_time)*100,(g_scWidth * g_scHeight * 2),(g_scWidth * g_scHeight * 5));


    // Start a new screen render
    uint32_t count=0; // use as a workload indicator

    uint32_t chunk_index_count = 0;
    do
    {
        // Loop through all of the world layouts to draw the world(s)
        for (uint32_t worlds=0 ; worlds < world.size() ; worlds++)
          {
        // See if the layout has mutiple frames
        if (world[worlds].frames > 1 )
        {
          // This uses two divides but is conceptually very simple
          this_frame = (elapsed_time / frame_period) %   world[worlds].frames;
        }
        else this_frame = 0; // Defaults to the sole first frame

          // set the pointer to the layout in use
          this_world_ptr = & (world[worlds].frame_layouts[this_frame]);

          uint32_t my_chunk = IndexChunkChooser(eye, direction,chunk_index_count,this_world_ptr);
          if (my_chunk == LAST_CHUNK) goto ChunksDone; // There is nothing more to be found so move on
            {
              // CheckTriangles returns itself if the chunk is invalid
              count += CheckTriangles(eye, direction, my_chunk,this_world_ptr); // Which pushes onto rasteriser queues
            }
          } // End of world loop
          chunk_index_count++; // Go onto the next chunk in the sequence
        
    }
    // Adjust how many chunks are included by pixel estimate
    // But keep going if no control pressed
    // This will also affect render depth when a key is pressed as complexity is still being estimated
      while ((count < max_pixel_count) || control_not_pressed); 
    
ChunksDone: // A goto is used to reach here to exit from a depth of two loops
              // Wait for rasterisation of queue to be finished, that's is the bigger job

    // It is possible that Core 0 does not idle if rasteriser is quick so fore a wdt reset
    // so Core 0 WDT is disabled in SDK configuration editor
    // as adding a 1 tck vTaskDelay didn't help

              xEventGroupWaitBits(
                       raster_event_group,               // event group handle
                       RASTER_DONE | CLEAR_READY | GAME_RUNNING,          // bits to wait for
                       pdFALSE,                            // don't clear the bit once we've started
                       pdTRUE,                           //  AND for any of the defined bits
                       portMAX_DELAY );                   //  block forever
              xEventGroupClearBits(raster_event_group, RASTER_DONE | CLEAR_READY); // Clear the bits we want to allow game play

      // Collision is checked by sampling depth buffer in a field of view
      // A struct is sent and updated
      // Later, if an impact is detected the triangle buffers are run again
      // to see which triangle has caused the impact, whilst this has an overhead it is only invoked
      // when player is stationary whereas checking and recording the object per pixel
      // on every frame would have quite an overhead, although I've not benchmarked it
      Near_pix test_pix;
      CheckCollide( &test_pix); // check proximity, returns depth 
      const float nearest = test_pix.depth; // For later use in movement
// Do basic player movement.
// Speed is adjusted by the PREVIOUS frame's duration which isn't perfect but
// we can't predict the NEXT frame build time!

// Simple direction control adjusted to move at a standardised angular speed
constexpr float rot_factor = M_PI * 0.0003f;
const float rotSpeed =  rot_factor * (float)frame_time;

// At this point scaled_direction is the size it was on the previous frame which
// resulted in delta_height, so we can work out a gradient
float rate_of_ascent = delta_height/scaled_direction.length();
if (rate_of_ascent < -0.8f)
{
  fallen += delta_height;
  //ESP_LOGI(TAG, "Fallen %f",fallen);
} 
else 
{
  // Stopped falling now, that's the dangerous part
  if (fallen < -1.0f)
  {
    Near_pix temp_event;
    const int8_t fall = (uint8_t)round(fallen);
    //ESP_LOGI(TAG,"Damage factor %f, %x",fallen,fall);
    temp_event.event = EVNT_ENERGY | EE_CHANGE | ((0xff & fall) <<EVNT_NN_SHIFT); 
    if( xQueueSend( game_event_queue, ( void * ) &temp_event, ( TickType_t ) 10 ) != pdPASS )
      {
      // Failed to post the message, even after 10 ticks.
      ESP_LOGI(TAG,"Failed to post item in event queue");
      }
  }
  fallen = 0.0f; // reset the drop when it slows down
}

// Scale the direction vector to maintain speed with changing frame duration
// Direction should be normalised anyway but do it just to be sure
// The intention was 1 metre per second which might be realistic but is SLOW
direction.normalize();
scaled_direction = direction * (float)frame_time * 0.005f;

control_not_pressed = true; // Assume this to be the case

static uint32_t last_event = 0;
uint32_t this_event=0;

if (!gpio_get_level(CONTROL_UP))
  {
    bool found = false;
    control_not_pressed = false;
    // Going forwards check that the eye won't get too close to the world after next move
    // Steps become larger as they get closer as refresh time increases so there is a
    // chance of overshooting
    if (nearest < (COLLISION_DISTANCE + scaled_direction.length()))
    {
      test_pix.depth = farPlane; // depth large to begin seeking in triangle queues

      // Viewer is about to be closer than the COLLISION_DISTANCE and find the nearest face
      // test_pix structure will be updated to the nearest point as it's found
      // Checking of which face is impacted is only done on forward motion
      // Note that projected triangle/tile queues are re-used by SendImpactQueue and not reset here
      // As before, chose buffers based on flipped status of pingpong buffers
      if (flipped)
      {
        found = SendImpactQueue(2, & test_pix) || SendImpactQueue(3, & test_pix); 
      }
      else
      {
        found = SendImpactQueue(0, & test_pix) || SendImpactQueue(1, & test_pix); 
      }
      //ESP_LOGI(TAG,"Nearest %f %d %d %d",(float)nearest, (int)test_pix.x, (int)test_pix.y, (int)test_pix.idx);

      if (found) // A face SHOULD be found if an impact, to minimise errors
      {
        // Use test_pix structure to derive event code via palette attributes
        this_event = test_pix.layout->palette[test_pix.layout->attributes[test_pix.idx]].event;

        // Check a valid event and also don't allow repeated messages for the same impact  
        if (this_event && (this_event != last_event))
        {
          last_event = this_event; // Note what happened previously
          test_pix.event = this_event; // Pass the event code since we've found it already
          // Send Near_pix event messsage if present
          // Wait for 10 ticks for space to become available if necessary
          // This might need to be longer than frame duration?
          if( xQueueSend( game_event_queue, ( void * ) &test_pix, ( TickType_t ) 10 ) != pdPASS )
          //if( xQueueSend( game_event_queue, ( void * ) &this_event, ( TickType_t ) 10 ) != pdPASS )
          {
            // Failed to post the message, even after 10 ticks.
            ESP_LOGI(TAG,"Failed to post item in event queue");
          }
        } // End of this_event detected
      } // end of found check
      else ESP_LOGI(TAG,"Impacted face not found"); // Should be rare occurance

      // Adjust this step so as not to go too far
      // The steps are reduced once we're in that close zone
      scaled_direction = (nearest - COLLISION_DISTANCE) * 0.2f * scaled_direction; 
    } // End of nearest

    eye.x += scaled_direction.x;
    eye.z += scaled_direction.z; // Don't add on the y element of ther vector
  }
  else last_event = 0; // reset event record when button lifted
/*
// Reverse is inhibited at the moment as it really needs code to 'look back'
if (!gpio_get_level(CONTROL_DOWN))
  {
    eye.x -= scaled_direction.x;
    eye.z -= scaled_direction.z; // Don't add on the y element of ther vector
  }
*/
if (!gpio_get_level(CONTROL_RIGHT))
  {
    control_not_pressed = false;
    if (nearest < COLLISION_DISTANCE)
    {
      // For turning, push back if too close
      eye = eye - (COLLISION_DISTANCE - nearest) * direction;
    }
    
      const float oldDirX = direction.x;
      direction.x = direction.x * cos(rotSpeed) - direction.z * sin(rotSpeed);
      direction.z = oldDirX * sin(rotSpeed) + direction.z * cos(rotSpeed);
      //ESP_LOGI(TAG,"RIGHT");
  }

  
if (!gpio_get_level(CONTROL_LEFT))
  {
    control_not_pressed = false;
    if (nearest < COLLISION_DISTANCE)
    {
      // For turning, push back if too close
      eye = eye - (COLLISION_DISTANCE - nearest) * direction;
    }
      const float oldDirX = direction.x;
      direction.x = direction.x * cos(-rotSpeed) - direction.z * sin(-rotSpeed);
      direction.z = oldDirX * sin(-rotSpeed) + direction.z * cos(-rotSpeed);
      //ESP_LOGI(TAG,"LEFT");
  }

// Find frame refresh duration and update the time
  frame_time=(esp_timer_get_time()-elapsed_time)>>10; // Divide by 1024 is near enough and faster
  elapsed_time=esp_timer_get_time();
  //ESP_LOGI(TAG, "Frame time is %d ",(int)frame_time); 
  // Record the frame in the reporting structure
  time_report.frames++;

  // Frame buffer is written now and various movements and impacts so this 
  // is the time to do 2D information overlays if needed

  if (OverlayFlag) OverlayTwoD(score_overlay);
  //taskYIELD();
} // end of loop forever

} // end of ShowWorld function

void SendFlippedFrame(void)
{
  // Signal that DMA is use
xEventGroupClearBits( raster_event_group, CLEAR_READY);

// Display the previously-prepared image from the indicated buffer
// On completion the call back will clear the pushed buffer
// Also empty the triangle queues that will be used by CheckTriangles
// The queues may be needed in this module so removed reset in SendQueue
if (flipped)
  {
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, g_scWidth, g_scWidth, frame_buffer_B));
    EmptyQueue(2);
    EmptyQueue(3);
  }
else
  {
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, g_scWidth, g_scWidth, frame_buffer_A));
    EmptyQueue(0);
    EmptyQueue(1);
  }
} // End of SendFlippedFrame