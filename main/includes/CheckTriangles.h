#pragma once

#include <stdint.h>
#include "geometry.h"
#include "structures.h"

#define CLIP_TR 0 // reject a triangle that is outside frustrum
#define CLIP_TA 1 // totally accept and applied a bounding box as it's inside frustrum
#define CLIP_MC 2 // triangle exits frustrum and so a bounding box of the whole screen is given which is then rasterised in tiles

void ProjectionMatrix();

// Checks every face and transforms prior to putting into rasteriser queue
uint32_t CheckTriangles(const Vec3f eye, const Vec3f direction, const uint32_t this_chunk, const WorldLayout* layo_ptr);

// Basic edge function for BaseTriangles, no awareness of frustrum
float edge_function(const Vec3f a, const Vec3f b, const Vec3f p);

// Derived from the above to simply find the current spot height of the viewer
float BaseTriangles(const Vec3f eye, const Vec3f direction, const uint32_t this_chunk, const WorldLayout* layo_ptr);

// Calculated face shading based on normal and lighting
void MakeShade(uint32_t idx, const Vec3f eye, const Vec3f direction, const WorldLayout* layo_ptr, Shade_params* surface_shade);

