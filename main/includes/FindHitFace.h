#pragma once
#include <stdint.h>

#include "globals.h"
#include "geometry.h"
#include "structures.h"

float CheckHitFace(const TriToRaster & tri, const uint32_t test_x, const uint32_t test_y);

float CheckHitTile(const TriToRaster & tri, const uint32_t test_x, const uint32_t test_y);

bool TestBoBox(const Rect2D box, const uint32_t test_x, const uint32_t test_y);