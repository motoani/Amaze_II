#pragma once

#define START_RASTER (1 << 0) // start the rasterisation 
#define RASTER_DONE (1 << 2)  // rasterisation is finished
#define CLEAR_READY (1 << 3)  // lcd callback says send is done so OK to clear the DMA'd buffer
#define GAME_RUNNING (1 << 4)  // the game is allowed to run, cleared at the end of play

