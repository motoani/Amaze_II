// All catch messages will be sent to here for showing and then hold, no recovery options
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void show_error(const char* error_string)
{
	printf("ERROR: %s",error_string);

	vTaskDelay(10*1000/portTICK_PERIOD_MS);
	esp_restart();
}