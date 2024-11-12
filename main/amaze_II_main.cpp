
// Note that SDK config has 128k reserved for DMA etc in internal memory
// 2 x 32k is needed for frame_buffer_A and _B, depth buffer is ok via cache though
#include "amaze_II_main.h"

#include <stdint.h>
#include <vector>
#include "driver/gpio.h"

#include "esp_partition.h"
#include "esp_err.h"
#include "esp_log.h" 
#include "esp_dma_utils.h"

#include "esp_cache.h"
#include "esp_heap_caps_init.h"
#include "esp_timer.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"


#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
//#include <freertos/timers.h> // Doesn't seem to be essential, perhaps embedded in another?

#include "ST7789_def.h"
#include "lcd_setup.h"

#include "buttons.h"
#include "title.h" // A 128 square title screen ideally
#include "GradientBar.h"

#include "events_global.h"
#include "wr_gpio.h"
#include "TriangleQueues.h"
#include "RasteriseBox.h"
#include "CheckTriangles.h"
#include "ShowWorld.h"
#include "ParseWorld.h"
#include "TimeTracker.h"
#include "EventManager.h"
#include "i2s_sound.h"

#define LO_PLAIN 0 // A static world 
#define LO_FLIP 1  // Flip book with some sets of vertices 

extern const uint32_t display_physical_height;
extern const uint32_t display_physical_width;

// The background colour for clearing screen which is made from fog
extern constexpr uint32_t fog = 0x00303030;
extern const uint16_t BackgroundColour = SwapBytes(((fog >> 8) & 0b1111100000000000) | ((fog >> 5) & 0b0000011111100000) | ((fog >> 3) & 0b0000000000011111));

#if defined T_DISPLAY_S3_GAMER || defined T_DISPLAY_S3
    const auto queue_size = 4500; // Size of each of 4 tri queues to rasterise
#endif
#ifdef T_QT_PRO
    // There's only 2MB PSRAM on the small unit
    const auto queue_size = 1800; // Size of each of 4 tri queues to rasterise
#endif

// Size of the game view port must be multiples of 8 for MMU caching perhaps?
// Basic aim is for 128 x 128 but will be reduced on smaller units that are only 128 at best
// There are also limits on memory allocation for large screens which don't always post errors
// This isn't robust for small screens either!
extern const auto g_scWidth = std::min( (uint32_t)128, 8 * ((display_physical_width - (gradbar_space + gradbar_w)) / 8) );
extern const auto g_scHeight = std::min( (uint32_t)128, display_physical_height);

    uint32_t * world_dummy;

std::vector<WorldLayout> the_layouts; // Global presently but not good idea

std::vector<EachLayout> world; // An unsized vector of layouts which can contain multiple frames

// Declare a global variable to hold the created event group
// which will synchronise the Core 0 and Core 1 tasks
// Bit definitions in event_globals.h
EventGroupHandle_t raster_event_group;

QueueHandle_t game_event_queue;
QueueHandle_t sound_event_queue;

TimerHandle_t track_handle_s;
TimerHandle_t track_handle_p;


// When the frame buffers are created their memory will be pointed to by
uint16_t * frame_buffer_A;
uint16_t * frame_buffer_B;

uint16_t * overlay_buffer; // For 2D operations

esp_lcd_panel_handle_t panel_handle = NULL;

bool flipped = true;

Vec3f direction;
Vec3f eye;
float eye_level; // Height of the viewer will be found from the world eye and spot_height

extern "C" void app_main(void)
{
    static const char *TAG = "main";
    // A time check will be done after setup and a delay made to retain title screen if required
    const int64_t startup_time = esp_timer_get_time(); // Internal microsecond clock

    ESP_LOGI(TAG, "Allocating memory for frame buffers");
    // allocate a screen buffer but must be on 64 byte boundary it seems for cache flush to work
    void *mem = malloc(sizeof(uint16_t) * display_physical_height * display_physical_width + EXAMPLE_PSRAM_DATA_ALIGNMENT - 1);
    uint16_t *pix = (uint16_t *)(((uintptr_t)mem + EXAMPLE_PSRAM_DATA_ALIGNMENT - 1) & ~ (uintptr_t)(EXAMPLE_PSRAM_DATA_ALIGNMENT - 1));
    // The below should work better without cache etc but it leaves screen gaps
    //uint16_t * pix = (uint16_t *)heap_caps_aligned_alloc(0x04 , sizeof(uint16_t) * EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES , MALLOC_CAP_DMA);
    if ( mem == NULL) assert("malloc for full screen buffer failed");

    // Make frame buffers
    frame_buffer_A = (uint16_t *)heap_caps_malloc(sizeof(uint16_t) * g_scWidth * g_scHeight , MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_32BIT);
    if ( frame_buffer_A == NULL) assert("malloc failed for frame_buffer_A");
    ClearWorldFrame(frame_buffer_A); // Barely needed, but it's neater!

    frame_buffer_B = (uint16_t *)heap_caps_malloc(sizeof(uint16_t) * g_scWidth * g_scHeight , MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_32BIT);
    if ( frame_buffer_B == NULL) assert("malloc failed for frame_buffer_B");
    ClearWorldFrame(frame_buffer_B);

    overlay_buffer = (uint16_t *)heap_caps_malloc(sizeof(uint16_t) * g_scWidth * g_scHeight , MALLOC_CAP_INTERNAL | MALLOC_CAP_32BIT);
    if ( overlay_buffer == NULL) assert("malloc failed for overlay_buffer");

    // Attempt to create the event group  - before DMA callback is invoked
    raster_event_group = xEventGroupCreate();
    // Was the event group created successfully?
    if( raster_event_group == NULL ) assert("Create event group failed");

    // make sure all bits are cleared so tasks don't start unexpectedly
    xEventGroupClearBits(raster_event_group,0xff);

     
    // Build the structures that make lcd handles
    esp_lcd_panel_io_handle_t io_handle = NULL;
    
    init_lcd_bus(&io_handle);
    init_lcd_panel(io_handle, &panel_handle);
    
    // Clear the WHOLE screen, not just out display area
    for (auto i=0; i<display_physical_width * display_physical_height; i++)
    {
        pix[i] = 0x0000; // Black screen rather than background
    }

    // Copy the title screen into top left area, cropping if physical is smaller than title
    for (auto w = 0; w < std::min(display_physical_width, (uint32_t)TITLE_W); w++)
    {
        for (auto h = 0; h < std::min(display_physical_height,(uint32_t)TITLE_H); h++)
        {
            pix[ (h*display_physical_width) + w ] = SwapBytes(amaze_2_title [h * TITLE_W + w]);
        }
    }

    // Flush the cache to move data to RAM where the DMA can pick it up
    ESP_ERROR_CHECK(esp_cache_msync((void *)pix, (size_t) sizeof(uint16_t) * display_physical_width * display_physical_height, ESP_CACHE_MSYNC_FLAG_DIR_C2M));
        
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, display_physical_width, display_physical_height, pix));
    ESP_LOGI(TAG, "Clear screen and title pixels sent");
    
    ESP_LOGI(TAG, "Set up buttons on GPIO");
    // Set up the game board for play
    //set_input_pin(CONTROL_A);
    //set_input_pin(CONTROL_B);
    set_input_pin(CONTROL_LEFT);
    set_input_pin(CONTROL_RIGHT);
    
    #ifdef CONTROL_UP
        set_input_pin(CONTROL_UP);
    #endif
    //set_input_pin(CONTROL_DOWN);

    ESP_LOGI(TAG,"Making depth buffers and queues");
    MakeDepthBuffer();

    // The queues need to allow tiles to be sent to both queues so they have 
    // more spaces than even though chunks are sent.
    // Queue 1 / 3 could be especially large as it's definitely all tiles!
    // MakeQueue() also sets queues to be empty so first rasterise will be rapidly returned 
    MakeQueue(queue_size, 0); // Set up storage space for triangle buffering
    MakeQueue(queue_size, 1); 

    MakeQueue(queue_size, 2); // To permit pingpong in dual core
    MakeQueue(queue_size, 3);

    ProjectionMatrix(); // Make the projection/perspective matrix for triangle rendering

    ESP_LOGI(TAG,"Finding partition for textures and mapping memmory");
    // Find the partition map in the partition table for textures
    const esp_partition_t *partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "textures");
    assert(partition != NULL);

    const void *texture_map_ptr;
    //uint8_t * tex_ptr; // Use a byte wise at present
    esp_partition_mmap_handle_t map_handle;

    // Map the textures partition to data memory
    ESP_ERROR_CHECK(esp_partition_mmap(partition, 0, partition->size, ESP_PARTITION_MMAP_DATA, &texture_map_ptr, &map_handle));
    ESP_LOGI(TAG, "Mapped textures partition to data memory address %p", texture_map_ptr);
    unsigned int size = partition->size;
    ESP_LOGI(TAG, "Texture partition is of size %x", size);

    ESP_LOGI(TAG,"Finding partition for the 3D world and mapping memmory");
    // Find the partition map in the partition table for the world itself
    const esp_partition_t * w_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "world");
    assert(w_partition != NULL);

    //uint8_t *w_map_ptr;
    const void * w_ptr; // This is the start of the partition
    esp_partition_mmap_handle_t w_map_handle;

    // Map the textures partition to data memory
    ESP_ERROR_CHECK(esp_partition_mmap(w_partition, 0, w_partition->size, ESP_PARTITION_MMAP_DATA, &w_ptr, &w_map_handle));
    ESP_LOGI(TAG, "Mapped world partition to data memory address and getting offsets %p", w_ptr);
    // w_ptr is a pointer to the start of the world partition
    // It is cast to uint8 * in w_map_ptr so that byte offsets can be done
    //w_map_ptr = (uint8_t *)w_ptr;

    // Use a structure into partition header to save pointer messing
    // to extract the over-arching parameters
    world_partition_header * world_partition_header_ptr = (world_partition_header *) w_ptr;

    eye = world_partition_header_ptr->eye;
    eye_level = eye.y; // Copy eye level from the world and save for later
    direction = world_partition_header_ptr->direction;
/*
    // Fetch the offset of the first world from the opening header
    // world_start will be a descriptor that is followed by an offset
    const uint32_t w_map_offset = (const uint32_t) * (1 + &(world_partition_header_ptr->world_start));

        ESP_LOGI(TAG, "First offset to world is %p", (void *)w_map_offset);
    
    w_map_ptr += w_map_offset; // Skip past the world header and offset to the world's own list of pointers

    // Each set of offsets is relative to its own header start
    // So there is a need to know where each header begins
*/
/*
    world_header * world_header_ptr = (world_header *) (w_ptr + w_map_offset);

    ESP_LOGI(TAG, "header %p",world_header_ptr);
    ESP_LOGI(TAG, "w_map %p",w_map_ptr);
    ESP_LOGI(TAG, "map %p",texture_map_ptr);
*/
    // Uses a ragged array to give offsets of world layouts in the partition
    ParseWorld ( w_ptr , texture_map_ptr );

    // Use the partition pointers to read the ROM world descriptors into a
    // structure of pointers, calculating the values from offsets for each case    
    //ReadWorld(w_map_ptr , world_header_ptr , texture_map_ptr);
    //ReadWorld(w_ptr , texture_map_ptr);

    // Initialise the I2S Tx system
    i2s_init_std_simplex();

    // Make a queue which will take event words generated during play and apss to a manager
    // Create a queue capable of containing 10 Near_pix values which say a lot about the impact
    game_event_queue = xQueueCreate( 10, sizeof( Near_pix ) );
    if( game_event_queue == 0 ) assert("Creation of game event queue failed");

    // Make a queue to send sound effect initiators to I2S module
    // Create a queue capable of containing 5 uint16_t values which
    // will number different sound effects
    sound_event_queue = xQueueCreate( 5, sizeof( uint16_t ) );
    if( sound_event_queue == 0 ) assert("Creation of sound event queue failed");

     // Set the initial health bar via the event queue system
    Near_pix first_event;
    first_event.event = EVNT_ENERGY | EE_SET | EVNT_SOUND | (0x0 << ES_SHIFT) | ( 0x79 << EVNT_NN_SHIFT) | EE_DISPLAY; // Start with 120  - actually 121 - energy so a life of 2 minutes
    if( xQueueSend( game_event_queue, ( void * ) &first_event, ( TickType_t ) 10 ) != pdPASS )
          {
            // Failed to post the message, even after 10 ticks.
            ESP_LOGI(TAG,"Failed to post item in event queue");
          }

    // Nearly everything is done, so see if the title screen can be removed yet
    while (esp_timer_get_time() < startup_time + 1000000);

// A reloading second timer for user updates
    track_handle_s = xTimerCreate("SecondTimer",       // Just a text name, not used by the kernel.
        (1000 / portTICK_PERIOD_MS),   // Activate every 1000ms
        pdTRUE,        // The timer will auto-reload on expiration
        ( void * ) 0,  // No id field really used once events up and running
//        ( void * ) &time_report,  // Pass the data structure via the id field
        TimeTrack // The one per second callback
        );
    if (track_handle_s == NULL) assert("Create tracker xTimerCreate failed");
    if( xTimerStart( track_handle_s, 0 ) != pdPASS )  assert("Start of tracker xTimer failed");

    // A oneshot timer that will be used to remove 2D popup overlays
    track_handle_p = xTimerCreate("PopupTimer",       // Just a text name, not used by the kernel
        (800 / portTICK_PERIOD_MS),   // Delay to remove overlay goes here
        pdFALSE,        // The timer is one-shot
        ( void * ) 0,  // No id field really used once events up and running
        StopOverlayTwoD // The overlay callback
        );
    if (track_handle_p == NULL) assert("Create popup xTimerCreate failed");

    // Clear the screen of the title etc just before gameplay begins
    // Check previous DMA completed
    xEventGroupWaitBits(
        raster_event_group,               // event group handle
        CLEAR_READY,                        // bits to wait for
        pdTRUE,                            // clear the bit once we've started
        pdTRUE,                           //  AND for any of the defined bits
        portMAX_DELAY );                   //  block forever
    
    for (auto i=0; i<display_physical_width * display_physical_height; i++)
    {
        pix[i] = 0x0000; // Black screen rather than background
    }
    
    // Flush the cache to move data to RAM where the DMA can pick it up
    ESP_ERROR_CHECK(esp_cache_msync((void *)pix, (size_t) sizeof(uint16_t) * display_physical_width * display_physical_height, ESP_CACHE_MSYNC_FLAG_DIR_C2M));
    
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, display_physical_width, display_physical_height, pix));
    ESP_LOGI(TAG, "Final clear screen sent");

    // Check previous DMA completed before allowing 1st world drawing, which may interfere
    // as it's empty, and so frame sent rapidly
    xEventGroupWaitBits(
        raster_event_group,               // event group handle
        CLEAR_READY,                        // bits to wait for
        pdTRUE,                            // clear the bit once we've started
        pdTRUE,                           //  AND for any of the defined bits
        portMAX_DELAY );                   //  block forever
    
    // Note that if the stacks here are too big the tasks will not run BUT
    // the create error will not be shown!!
    
    // On Arduino this would run as loop()
    ESP_LOGI(TAG,"Start ShowWorld task now everything else ready");
    if (xTaskCreatePinnedToCore (
                    ShowWorld,        // Task function
                    "ShowWorldTask",      // String with name of task
                    30000,            // Stack size in bytes
                    NULL,// Passing nothing at the moment
                    4,                // Priority of the task
                    NULL,             // Task handle
                    0)            // Core
        != pdPASS) assert("Failed to create ShowWorld task");

    ESP_LOGI(TAG,"Start rasterise task");
    if (xTaskCreatePinnedToCore (
                    rasteriseTask,        // Task function
                    "RasteriseTask",      // String with name of task
                    30000,            // Stack size in bytes
                    NULL,// Passing nothing at the moment
                    6,                // Priority of the task - highest for this core
                    NULL,             // Task handle
                    1)            // Core - some RTOS tasks on zero so put this where less work
        != pdPASS) assert("Failed to create Rasterisation task");

    ESP_LOGI(TAG,"Start game event manager task");
    if (xTaskCreatePinnedToCore (
                    GetGameEvent,        // Task function
                    "GameEventTask",      // String with name of task
                    10000,            // Stack size in bytes
                    NULL,// Passing nothing at the moment
                    3,                // Priority of the task
                    NULL,              // Task handle
                    tskNO_AFFINITY)
        != pdPASS) assert("Failed to create GameEvent task");

    // Make task for I2S sound effects
    if(xTaskCreate(i2s_write_task, "i2s_write_task", 4096, NULL, 5, NULL)
        != pdPASS) assert ("Failed to create I2S Tx task");

} // End of main now that work passed to three tasks
