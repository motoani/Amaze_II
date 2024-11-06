#pragma once

#include <stdint.h>

// Thin palette format is a uint32_t count of materials followed by
// type word that is coded, each of which is followed by value which could be:
#define PAL_PLAIN 0x01 //RGB888 colour
#define PAL_TEXOFF 0x02 // Offset into texture table

void ParseWorld(const void * w_ptr , const void * texture_map_ptr);

WorldLayout ReadWorld(const void * w_map_ptr , const void * map_ptr);
