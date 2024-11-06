#include <stdint.h>

#include "esp_log.h" 

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "freertos/event_groups.h"

#include "globals.h"
#include "geometry.h"
#include "structures.h"
#include "events_global.h"

#include "FindHitFace.h"
#include "ShowError.h"
#include "RasteriseBox.h"

#include "TriangleQueues.h"

// A global pingpong flag
extern bool flipped;
extern EventGroupHandle_t raster_event_group;
extern uint16_t * frame_buffer_A;
extern uint16_t * frame_buffer_B;

uint16_t *frame_buffer_this;

unsigned int max_raster_buf[4];

// Reserve space for a minimum of two queues of primitives using struct of TriToRaster
// One for Rasterise triangles and one for NotRasterise tiles
TriQueue BlockA[4];

static const char *TAG = "TriangleQueues";


// Manage queues via cores and tasks
void rasteriseTask(void * parameter)
{
    ESP_LOGI(TAG, "Entered rasterise task and waiting");

  while(1)
  {
    xEventGroupWaitBits(
                       raster_event_group,               // event group handle
                       START_RASTER,                        // bits to wait for
                       pdTRUE,                            // clear the bit once we've started
                       pdTRUE,                           //  AND for any of the defined bits
                       portMAX_DELAY );                   //  block forever
    //xEventGroupClearBits(raster_event_group, START_RASTER); // retain GAME_RUNNING BIT

    if (flipped)
    {
        frame_buffer_this=frame_buffer_A; // Set the target frame buffer
        ClearWorldFrame(frame_buffer_this); // Perhaps not ideal to do this on rasteriser core?

        SendQueue(0); // Send both queues to the rasteriser
        SendQueue(1);
    }
    else
    {
        frame_buffer_this=frame_buffer_B; // Set the target frame buffer
        ClearWorldFrame(frame_buffer_this); // Perhaps not ideal to do this on rasteriser core?
        SendQueue(2); // Send both queues to the rasteriser
        SendQueue(3);
    }
    xEventGroupSetBits(
      raster_event_group,
      RASTER_DONE);
    
    //taskYIELD();
  }
}


// Set up a queue with the various parameters
void MakeQueue(const uint32_t tri_count, const uint32_t block)
{
    BlockA[block].size = 0; // Record being of zero size
    // Get a pointer to the start of the allocated memory area
    TriToRaster* this_ptr;

    this_ptr = (TriToRaster*)malloc(sizeof(TriToRaster) * tri_count);
    BlockA[block].itemptr = this_ptr;

    if (!this_ptr) // manage failure to allocate
    {
        show_error("Failed to allocate triangle queue");
    }
    BlockA[block].size = tri_count; // Record size
    BlockA[block].count = 0; // and it's now empty
}

// Empty Queues
void EmptyQueues()
{
  // Counters (which is used to fill and read) are set to empty rather than anything being deleted
  BlockA[0].count=0;
  BlockA[1].count=0;
  BlockA[2].count=0;
  BlockA[3].count=0;
}

void EmptyQueue(const uint32_t block)
{
  // Counter (which is used to fill and read) are set to empty rather than anything being deleted
  BlockA[block].count=0;
};

// Put a triangle on the queue
uint32_t QueueTriangle(const TriToRaster triangle, const uint32_t block)
{
    // sizeof(triangle) is >70  bytes with multiple elements of the struct
    // Put the passed triangle into the memory space as if an array
    // It's inefficient as tiles pass a matrix that's the same many times...
    BlockA[block].itemptr[BlockA[block].count] = triangle;

    // Increment the counter
    BlockA[block].count++;

        if (BlockA[block].count >= BlockA[block].size)
        {
            ESP_LOGI(TAG,"Block %d queue overflow",(int)block);
            show_error("Triangle queue overflow"); // Manage buffer overflow
        }
        // Use the bounding box to (over)estimate how many pixels will be placed although
        // it will be correct for tiles in odd block
        uint32_t pixel_estimate = (uint32_t)((triangle.BoBox.m_MaxX - triangle.BoBox.m_MinX) * (triangle.BoBox.m_MaxY - triangle.BoBox.m_MinY));

        return(pixel_estimate);
}

// Send all of the queued triangles or tiles to the rasteriser
// Use block to choose RasteriseBox or NotRasteriseBox such that
// an odd block goes to NotRasteriseBox
void SendQueue(const uint32_t block)
{
    // Loop through the queue items, the order doesn't matter as pixels
    // placed based on z depth
    if (BlockA[block].count == 0) return; // Quit immediately if an empty queue
    for (uint32_t cnt = 0; cnt < BlockA[block].count; cnt++)
    {
        TriToRaster this_tri = BlockA[block].itemptr[cnt];
        if (block & 0x01) // test bit zero for oddness
        {
            NotRasteriseBox(this_tri);
        }
        else
        {
            RasteriseBox(this_tri);
        }
    }
    //std::cout << "Triangle queue size in " << block << " is " << BlockA[block].count << "\n";
    
    if (BlockA[block].count > max_raster_buf[block]) max_raster_buf[block] = BlockA[block].count; // track buffer usage

    // Reset of counter is done before use in ShowWorld now
    //BlockA[block].count = 0; //reset queue counter at the end
}

// Send all of the queued triangles or tiles to be checked for an impact
// Use block to choose whether to edge check or not such that
// an odd block goes to simply checking the bounding box
bool SendImpactQueue(const uint32_t block, Near_pix * to_test)
{
    bool found = false; // Failsafe for not findign an impact
    // Loop through the queue items
    if (BlockA[block].count==0) return(false); // Quit immediately if an empty queue

    // Loop through and find the nearest point
    for (uint32_t cnt = 0; cnt < BlockA[block].count; cnt++)
    {
        TriToRaster this_tri = BlockA[block].itemptr[cnt];
        if (block & 0x01) // test bit zero for oddness
        {
            const float z = CheckHitTile(this_tri, to_test->x, to_test->y);
            if (z < to_test->depth)
            {
                found = true;
                to_test->depth = z;
                to_test->idx = this_tri.idx;
                to_test->layout = this_tri.layout;
            }    
        }
        else
        {
            const float z = CheckHitFace(this_tri, to_test->x, to_test->y);
            if (z < to_test->depth)
            {
                found = true;
                to_test->depth = z;
                to_test->idx = this_tri.idx;
                to_test->layout = this_tri.layout;
            }    
        }
    }
return (found);
} // End of SendImpactQueue