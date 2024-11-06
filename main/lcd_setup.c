#include <stdint.h>

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"

#include "driver/gpio.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_dma_utils.h"

#include <freertos/FreeRTOS.h>
//#include <freertos/task.h>
#include "freertos/event_groups.h"
#include "events_global.h"

#include "esp_cache.h"

#include "ST7789_def.h"
#include "lcd_setup.h"




static const char *TAG = "lcd_setup";

extern EventGroupHandle_t raster_event_group;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////       Parameters from Lilygo and Bodmer's resources                  //////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// PCLK frequency can't go too high as the limitation of PSRAM bandwidth 2MHz
// Datasheet suggests 66ns write cycle time which equates to 15Mhz
// In my case I've set to use internal RAM - 10MHz fine, 20 MHZ write error
#define EXAMPLE_LCD_PIXEL_CLOCK_HZ     (14 * 1000 * 1000)

#define EXAMPLE_LCD_BK_LIGHT_ON_LEVEL  1
#define EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL !EXAMPLE_LCD_BK_LIGHT_ON_LEVEL
#define EXAMPLE_PIN_NUM_DATA0          39
#define EXAMPLE_PIN_NUM_DATA1          40
#define EXAMPLE_PIN_NUM_DATA2          41
#define EXAMPLE_PIN_NUM_DATA3          42
#define EXAMPLE_PIN_NUM_DATA4          45
#define EXAMPLE_PIN_NUM_DATA5          46
#define EXAMPLE_PIN_NUM_DATA6          47
#define EXAMPLE_PIN_NUM_DATA7          48

#define EXAMPLE_PIN_NUM_PCLK           8 // Write
#define EXAMPLE_PIN_NUM_CS             6
#define EXAMPLE_PIN_NUM_DC             7
#define EXAMPLE_PIN_NUM_RST            5
#define EXAMPLE_PIN_NUM_BK_LIGHT       38

#define EXAMPLE_PIN_NUM_RD              9

// Bit number used to represent command and parameter
#define EXAMPLE_LCD_CMD_BITS           8
#define EXAMPLE_LCD_PARAM_BITS         8


#ifdef __cplusplus
extern "C" {
#endif
static bool lcd_callback(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    /*
    extern const uint16_t BackgroundColour; // = ((fog >> 8) & 0b1111100000000000) | ((fog >> 5) & 0b0000011111100000) | ((fog >> 3) & 0b0000000000011111);

   // Once pushed to TFT the buffer can be cleared
   // This is called when DMA is complete, in this case we can clear the buffer
     if (flipped)
    {
        // Clear screen, could make a function, but is it worth it?
        // memset() is char-based, could make this uint32_t perhaps? 
        for (int i=0;i < 128 * 128;i++)
        {
            frame_buffer_B[i] = BackgroundColour; // Background colour
        }
    }
    else
        {
        for (int i=0;i < 128 * 128;i++)
        {
            frame_buffer_A[i] = BackgroundColour; // Background colour
        }
    }
    */
    // A flag set to show that screen is cleared so DMA or re-use of frame buffer can occur
    // DMA transfer can be slower than a fast frame which gives write errors hance need for flag
    // 10Mhz clock makes this less of a limiting rate
    xEventGroupSetBits(
        raster_event_group,
        CLEAR_READY);

    return (false);
} // End of lcd_callback
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif
void init_lcd_i80_bus(esp_lcd_panel_io_handle_t *io_handle)
{
    ESP_LOGI(TAG, "Initialize Intel 8080 bus");

    esp_lcd_i80_bus_handle_t i80_bus = NULL;
    esp_lcd_i80_bus_config_t bus_config = {
        .dc_gpio_num = EXAMPLE_PIN_NUM_DC,
        .wr_gpio_num = EXAMPLE_PIN_NUM_PCLK,
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .data_gpio_nums = {
            EXAMPLE_PIN_NUM_DATA0,
            EXAMPLE_PIN_NUM_DATA1,
            EXAMPLE_PIN_NUM_DATA2,
            EXAMPLE_PIN_NUM_DATA3,
            EXAMPLE_PIN_NUM_DATA4,
            EXAMPLE_PIN_NUM_DATA5,
            EXAMPLE_PIN_NUM_DATA6,
            EXAMPLE_PIN_NUM_DATA7,
        },
        .bus_width = CONFIG_EXAMPLE_LCD_I80_BUS_WIDTH,
        .max_transfer_bytes = EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES * sizeof(uint16_t),
        .psram_trans_align = EXAMPLE_PSRAM_DATA_ALIGNMENT,
        .sram_trans_align = 4,
    };
    ESP_ERROR_CHECK(esp_lcd_new_i80_bus(&bus_config, &i80_bus));

    esp_lcd_panel_io_i80_config_t io_config = {
        .cs_gpio_num = EXAMPLE_PIN_NUM_CS,
        .pclk_hz = EXAMPLE_LCD_PIXEL_CLOCK_HZ,
        .trans_queue_depth = 10,
        .on_color_trans_done = lcd_callback,
        .lcd_cmd_bits = EXAMPLE_LCD_CMD_BITS,
        .lcd_param_bits = EXAMPLE_LCD_PARAM_BITS,
        .dc_levels = {
            .dc_idle_level = 0,
            .dc_cmd_level = 0,
            .dc_dummy_level = 0,
            .dc_data_level = 1,
        },
        .flags = {
            .swap_color_bytes = true, // Swap to be done by DMA hardware
        },
        //.user_ctx = user_ctx,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i80(i80_bus, &io_config, io_handle));

    ESP_LOGI(TAG, "Set RD pin high"); // This is CRITICAL but isn't in driver information
    gpio_config_t rd_gpio_config = {
        .pin_bit_mask = 1ULL << EXAMPLE_PIN_NUM_RD,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&rd_gpio_config));
    gpio_set_level((gpio_num_t) EXAMPLE_PIN_NUM_RD, 1);
} // End of init_lcd_i80_bus
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif
void init_lcd_panel(esp_lcd_panel_io_handle_t io_handle, esp_lcd_panel_handle_t *panel)
{
#if EXAMPLE_PIN_NUM_BK_LIGHT >= 0
    ESP_LOGI(TAG, "Turn off LCD backlight");
    gpio_config_t bk_gpio_config = {
        .pin_bit_mask = 1ULL << EXAMPLE_PIN_NUM_BK_LIGHT,
        .mode = GPIO_MODE_OUTPUT
    };
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
    gpio_set_level((gpio_num_t) EXAMPLE_PIN_NUM_BK_LIGHT, EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL);
#endif // EXAMPLE_PIN_NUM_BK_LIGHT >= 0

    ESP_LOGI(TAG, "Install LCD driver of st7789");
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = EXAMPLE_PIN_NUM_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, panel));

    esp_lcd_panel_reset(* panel);
    esp_lcd_panel_init(* panel);
    // Set inversion, x/y coordinate order, x/y mirror according to your LCD module spec
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(* panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(* panel, true,false));
 
    // the gap is LCD panel specific, even panels with the same driver IC, can have different gap value
    esp_lcd_panel_invert_color(* panel, true);
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(* panel,0,35)); // Trial and error

    // These set up parameters from Bodmer's TFT library ST7789_Init.h
    //------------------------------display and color format setting--------------------------------//
//writecommand(ST7789_MADCTL);
//writedata(TFT_MAD_COLOR_ORDER);

//writecommand(0x3A);
//writedata(0x05);
//--------------------------------ST7789V Frame rate setting----------------------------------//
ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io_handle,ST7789_PORCTRL,(uint8_t[]){0x0b, 0x0b, 0x00, 0x33, 0x33},5));

ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io_handle,ST7789_GCTRL,(uint8_t[]){0x75},1));

//---------------------------------ST7789V Power setting--------------------------------------//
ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io_handle,ST7789_VCOMS,(uint8_t[]){0x28},1));

ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io_handle,ST7789_LCMCTRL,(uint8_t[]){0x2c},1));

ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io_handle,ST7789_VDVVRHEN,(uint8_t[]){0x01},1));

ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io_handle,ST7789_VRHS,(uint8_t[]){0x1f},1));

ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io_handle,ST7789_FRCTR2,(uint8_t[]){0x13},1));

ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io_handle,ST7789_PWCTRL1,(uint8_t[]){0xa7},1));

ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io_handle,ST7789_PWCTRL1,(uint8_t[]){0xa4, 0xa1},2));

ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io_handle,0xd6,(uint8_t[]){0xa1},1));

//--------------------------------ST7789V gamma setting---------------------------------------//
ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io_handle,ST7789_PVGAMCTRL,(uint8_t[]){0xf0, 0x05, 0x0a, 0x06, 0x06, 0x03, 0x2b, 0x32, 0x43, 0x36, 0x11, 0x10, 0x2b, 0x32},14));

ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io_handle,ST7789_NVGAMCTRL,(uint8_t[]){0xf0, 0x08, 0x0c, 0x0b, 0x09, 0x24, 0x2b, 0x22, 0x43, 0x38, 0x15, 0x16, 0x2f, 0x37},14));

// Now we are set up activate the display
ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(*panel, true));

#if EXAMPLE_PIN_NUM_BK_LIGHT >= 0
    ESP_LOGI(TAG, "Turn on LCD backlight");
    gpio_set_level((gpio_num_t) EXAMPLE_PIN_NUM_BK_LIGHT, EXAMPLE_LCD_BK_LIGHT_ON_LEVEL);
#endif // EXAMPLE_PIN_NUM_BK_LIGHT >= 0
} // End of init_lcd_panel
#ifdef __cplusplus
}
#endif