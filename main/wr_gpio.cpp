// Generic GPIO functions to serve the 3d world rasteriser

#include <stdint.h>
#include "driver/gpio.h"
#include "buttons.h"

// Makes a numbered pin input with an internal pullup
// This sort of thing is hidden in Arduino
// Are ALL four parts really needed?
void set_input_pin (gpio_num_t pin)
{
    gpio_set_direction(pin,GPIO_MODE_INPUT);
    //gpio_pulldown_dis(pin);
    //gpio_pullup_en(pin);
    gpio_set_pull_mode(pin, GPIO_PULLUP_ONLY);
}
