#include <stdint.h>
#include <algorithm>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "freertos/event_groups.h"

#include "esp_timer.h"

#include "esp_log.h" 

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"

#include "structures.h"
#include "events_global.h"

#include "ShowWorld.h"

#include "GradientBar.h"
#include "numberfont.h" // 10 digits as a bitmap for use as a 'font' and 'game over'
#include "EventManager.h"

extern QueueHandle_t game_event_queue; // A FreeRTOS queue to pass game play events from world to manager
extern std::vector<EachLayout> world; // An unsized vector of layouts each of which can contain multiple frames
extern TimerHandle_t track_handle_p; // Handle for popup removal timer
extern bool OverlayFlag; // Causes the 2D overlay to be added 
extern uint16_t * overlay_buffer; // 2D buffer allocated in start-up

// Track parameters and report occasionally
Time_tracked time_report;

void GetGameEvent(void * parameter)
{
    static const char *TAG = "GetGameEvent";
    extern TwoD_overlay score_overlay;
    extern EventGroupHandle_t raster_event_group;
    extern TimerHandle_t track_handle_s;

    static int64_t game_start_time = esp_timer_get_time(); // The time game play starts is found dynamically on first run

    Near_pix this_event_pix;

    ESP_LOGI(TAG,"Entering GetGameEvent task to loop");

    // A task that waits for a game play event to appear on the queue
    while (1)
    {
        // Block until a queue item is available
        xQueueReceive( game_event_queue, & this_event_pix, portMAX_DELAY);
        uint32_t event_code = this_event_pix.event;
        //ESP_LOGI(TAG,"Event is %04x",(int)event_code);
        // Nested if allows actions to be accumulated rather than isolated with switch
        if (event_code & EVNT_ENERGY) // Actions related to player's energy level
        {
            if (event_code & EE_SET)
                {
                    // Set its value, generally as game is reset
                    time_report.health = (event_code & EVNT_NN_MASK) >> EVNT_NN_SHIFT;
                    time_report.health = std::clamp(time_report.health, (uint16_t)0 , (uint16_t)128);
                    // ESP_LOGI(TAG,"Energy set to %d",(int)time_report.health);
                }
            if (event_code & EE_CHANGE)
                {
                    //ESP_LOGI(TAG,"Energy incoming %d",(int)time_report.health);

                    // Adjust its value, done with time, incidents and awards
                    // Working with 16bit ints until it's been clamped as it can overflow on a temporary basis
                    uint16_t temp_health = time_report.health + (int8_t)((event_code & EVNT_NN_MASK) >> EVNT_NN_SHIFT);
                    //ESP_LOGI(TAG,"Temp health %d", (int)temp_health);
                    time_report.health = std::clamp(temp_health, (uint16_t)0 , (uint16_t)128);
                    // ESP_LOGI(TAG,"Energy changed to %d",(int)time_report.health);
                    if (time_report.health <= 0)
                        // End of the game
                        {
                            // Halt key tasks rather than deleting as it's cleaner and allows restarting if required
                            xEventGroupClearBits(raster_event_group, GAME_RUNNING); // Hold the ShowWorld task which will in turn halt raster queues
                            xTimerStop(track_handle_s,5); // Halt the one second update timer
                            const uint16_t game_play_time = (uint16_t)((esp_timer_get_time() - game_start_time)/1000000);
                            // Display the score on stationary game screen when last frame has been done
                            MakeNumber(0,game_play_time, score_overlay);  // make the score buffer
                            xEventGroupWaitBits(
                                raster_event_group,               // event group handle
                                RASTER_DONE,                        // bits to wait for
                                pdTRUE,                            // clear the bit once we've started
                                pdTRUE,                           //  AND for any of the defined bits
                                100 );                   //  block for a bit

                            // Send the 'game over' screen
                            TwoD_overlay gameover_screen = {GAMEOVER_W, GAMEOVER_H, (uint16_t *) gameover};
                            OverlayTwoD(gameover_screen);
                            OverlayTwoD(score_overlay);                 // overlay the buffer
                            flipped = !flipped;                         // align writing and display buffers 
                            SendFlippedFrame();                         // draw frame buffer with its overlay onto LCD
                        } // End of end of game
                }
            if (event_code & EE_DISPLAY)
                {
                    // Show the energy bar
                    // ESP_LOGI(TAG,"To display energy");
                    EvntHealthBar(time_report.health);
                }
            if (event_code & EE_SCORE)
                {
                    // Make a buffer that has digits as raster
                    MakeNumber(1,((event_code & EVNT_NN_MASK) >> EVNT_NN_SHIFT), score_overlay);
                    // Show the overlay
                    // ESP_LOGI(TAG,"Show top-ups of energy");
                    // Start the popup removal timer now
                    if( xTimerStart( track_handle_p, 0 ) != pdPASS )  assert("Start of popup xTimer failed");
                    OverlayFlag = true;
                }
        } // End of EVNT_ENERGY

        if (event_code & EVNT_FACES) // Actions related to faces in the worlds
        {
            if (event_code & EF_DELETE)
            {
            // Delete faces matching event code
            //ESP_LOGI(TAG,"To delete faces");
            EvntDeleteFaces(this_event_pix); // Delete the item impacted
            }
        } // End of EVNT_FACES
    //taskYIELD();
    } // End of infinite while loop
 } // End of GetGameEvent

// Delete all of the faces from chunk lists that have the same event code throughout this layout
// Works through ALL frames of ALL world layouts
void EvntDeleteFaces(Near_pix this_event_pix)
{
    static const char *TAG = "EvntDeleteFaces";
    WorldLayout * this_world_ptr; // ptr to const world

    // Loop through all of the world layouts to draw the world(s)
    for (uint32_t worlds=0 ; worlds < world.size() ; worlds++)
    {
        // See if the layout has mutiple frames and loop accordingly
        // Note that single frame layouts have a count of '1'
        for (uint16_t this_frame = 0; this_frame < world[worlds].frames; this_frame++)
        {
            //ESP_LOGI(TAG,"World % d and frame %d", (int)worlds, (int)this_frame);

        // set the pointer to the layout in use
        this_world_ptr = & (world[worlds].frame_layouts[this_frame]);

        // We could check here if the event code is in the palette in this frame 
        // to save checking all faces in irrelevant layouts/frames
        // faceMaterials * this_palette = this_world_ptr->palette;
        // Note that we don't know how many entries are in the palette...
        // It seems fast enough without the extra checking
        
        // Work through chunks finding which faces share the event code value
        // Fetch the number of chunks in the recetangle from ChAr x and z

        uint32_t chunk_number = this_world_ptr->ChAr.xcount * this_world_ptr->ChAr.zcount;
        // Loop through all chunks
        for (uint32_t chnk = 0; chnk < chunk_number; chnk ++)
            {
            // Fetch the base address of the list of faces
            uint16_t * face_ptr = this_world_ptr->TheChunks[chnk].faces_ptr;
            
            uint32_t i = 0; // A counter to step into chunk array

            // Will quit when no faces in a chunk or i shows it's at the end   
            while (this_world_ptr->TheChunks[chnk].face_count && i < this_world_ptr->TheChunks[chnk].face_count)
            {
                // It is an option to use the face idx to retrieve the attribute but that isn't
                // reprodcibile across layouts, whereas checking for the specific event code is more indorection
                // but actually does what is wanted without making assumptions about the event code and attribute ampping
                while (this_world_ptr->palette[this_world_ptr->attributes[face_ptr[i]]].event == this_event_pix.event)
                {
                    // Yes it is, so copy last face into the space and decrement face count in chunk table
                    //ESP_LOGI(TAG, "Deleting face %d at %i",(int)face_ptr[i],(int)i);
                    // Deletion done by putting last face into a matched slot
                    face_ptr[i] = face_ptr[this_world_ptr->TheChunks[chnk].face_count - 1];
                    this_world_ptr->TheChunks[chnk].face_count --;
                    if (this_world_ptr->TheChunks[chnk].face_count == 0) break; // Quit if all deleted
                }
            i++; // Move on to the next spot in the face list now matches removed at a given spot
        } // End of face delete while
    } // End of for through chunks
        } // End of frame loop
    } // End of world loop
} // End of EvntDeleteFaces

void EvntHealthBar(const uint16_t health)
{
    extern esp_lcd_panel_handle_t panel_handle;
    extern EventGroupHandle_t raster_event_group;

    const uint32_t horiz_space = 4; // A space between world window and the bar
    static uint16_t bar_buffer[GRADBAR_H * GRADBAR_W]; // Make an empty graphic bar

    // A bar is built in RAM with black top part and coloured gradient lower
    // +1 so that initial 127 shows as the full health bar of 128 lines
    const auto transition_index = (GRADBAR_W * (GRADBAR_H - health)); 

    // Copy the lower part of gradient-coloured bar as required
    for (int i = transition_index; i < (GRADBAR_H * GRADBAR_W); i++)
    {
        bar_buffer[i] = gradient_bar[i];
    }

    // Blank out the top part as required, for loop is good as it allows zero executions
    for (int i = 0; i < transition_index; i++)
    {
        bar_buffer[i] = 0x0000;
    }

    // Check that LCD is free to use
    xEventGroupWaitBits(
        raster_event_group,               // event group handle
        CLEAR_READY,                    // bits to wait for
        pdTRUE,                            // clear the bit once we've started
        pdTRUE,                           //  AND for any of the defined bits
        portMAX_DELAY );                   //  block forever

    // Signal that DMA is in use
    //xEventGroupClearBits( raster_event_group, CLEAR_READY);

    // A single DMA process is initiated rather than sending top and bottom separately
    // No check done that other DMA ongoing, is this needed?
    // MUTEX or use the event  flag?
    // Perhaps this should go inside the world screen for the minature devices?
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle,
        g_scWidth + horiz_space + 0, 0, // Start and end of the complete buffer
        g_scWidth + horiz_space + GRADBAR_W, GRADBAR_H,
        bar_buffer ));
} // End of HealthBar

void OverlayTwoD(TwoD_overlay & this_overlay)
{
static const char *TAG = "OverlayTwoD";

extern bool flipped; // A global pingpong flag
extern uint16_t * frame_buffer_A;
extern uint16_t * frame_buffer_B;

uint16_t *frame_buffer_this;

if (flipped) frame_buffer_this = frame_buffer_A;
else frame_buffer_this = frame_buffer_B;

//ESP_LOGI(TAG," width %d and height %d", (int)this_overlay.width, (int)this_overlay.height);
// Calculate the first corner to copy to so as to centralise the overlay every frame
const auto start_index = ((g_scWidth * (g_scHeight - this_overlay.height) / 2) + (g_scWidth - this_overlay.width)/2); 
//ESP_LOGI(TAG,"Start index %d",(int)start_index);
// Do the copying based on parameters for source
for (auto h = 0; h < this_overlay.height; h++)
{
    for (auto w = 0; w < this_overlay.width; w++)
    {
        const uint16_t pix_write = this_overlay.buffer[h*this_overlay.width + w];
        // Only write non-zero pixels from source image
        if (pix_write) frame_buffer_this[h * g_scWidth + w + start_index] = pix_write;
    }
}
} // End of Overlay2D

void StopOverlayTwoD(TimerHandle_t tracked_handle)
{
    static const char *TAG = "StopOverlayTwoD";

    //ESP_LOGI(TAG,"Removing popup");
    OverlayFlag = false;

} // End of StopOverlayTwoD

// Input a score and make a bitmap for display
// Maybe needs a structure for the bitmap and its dimensions
void MakeNumber(uint16_t font_index, uint16_t score, TwoD_overlay & this_overlay)
{
    static const char *TAG = "MakeNumber";

    std::vector<uint16_t>digits; // Somewhere to keep the digits
    this_overlay.buffer = overlay_buffer; // Define where the build buffer is

    // Start by splitting score into digits, this will give them in reverse order
    // such that lowest is deepest, actually this may well be optimum
    while(score)
    {
        digits.push_back(score % 10);
        score = score / 10;
    }

    // Run through  the vector to find the full width of buffer required, don't pull the items though
    uint16_t this_buffer_width = 0;
    for (auto this_digit : digits )
    {
        this_buffer_width += nesto[font_index].locations[this_digit + 1] - nesto[font_index].locations[this_digit];
    }
    //ESP_LOGI(TAG,"Buffer width set to %d",(int)this_buffer_width);

    // Knowing the destination layout copy raster from the char sources one by one
    // Starting with most significant digit which is popped first 
    uint16_t buffer_x_pos = 0; // Current placement position in buffer, starting at the beginning
    while (!digits.empty())
    {
        uint16_t this_digit = digits.back(); digits.pop_back();// Fetch a digit, highest order first
        // Find where the digit resides in the source array that holds the numbers
        const uint16_t font_start_column = nesto[font_index].locations[this_digit];
        const uint16_t font_end_column = nesto[font_index].locations[this_digit + 1];
        const uint16_t font_char_width = font_end_column - font_start_column;

    //ESP_LOGI(TAG, "Digit %d, start %d and end %d",(int)this_digit, (int)font_start_column, (int)font_end_column); 
       
        // Copy the font into the buffer area, background removed by the overlayer later
        for (auto font_row = 0; font_row < nesto[font_index].height; font_row ++) // Loop through each row of font in the copy
        {
            for (auto font_pos = 0; font_pos < font_char_width; font_pos++)
            {
                this_overlay.buffer[buffer_x_pos + font_pos + (font_row * this_buffer_width)] = nesto[font_index].buffer[font_start_column + font_pos + (font_row * nesto[font_index].width)];
            //ESP_LOGI(TAG, "At row %d, pos %d", (int)font_row, (int)font_pos);
            } // End of copying along a row
        } // End of font column copy for loop
        buffer_x_pos += font_char_width; // Move along in destination buffer
    } // End of while to pull every digit from buffer

    // Update the parameters of the written buffer for the consumer
    this_overlay.width = this_buffer_width;
    this_overlay.height = nesto[font_index].height;
    //ESP_LOGI(TAG,"Height %d and width %d",(int)this_overlay.height,(int)this_overlay.width);
} // End of MakeNumber