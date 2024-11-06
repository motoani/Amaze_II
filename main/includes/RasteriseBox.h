#pragma once
#include <stdint.h>

#include "globals.h"
#include "geometry.h"
#include "structures.h"

void MakeDepthBuffer();

void ClearDepthBuffer(float farPlane);

void CheckCollide(Near_pix * near);

bool CheckEdgeFunction(const Vec3f& E, const float result);

void RasteriseBox(const TriToRaster & tri);

void NotRasteriseBox(const TriToRaster & tri);

uint32_t spec_shade_pixel (const uint32_t rgb888, const Shade_params surface_shade);

void WritePixel2Fog888(const uint32_t frame_index, const uint32_t rgb888, const float depth);

float FogFunction(const float depth); // end and start are set in the function

uint32_t intmix(const uint32_t x, const uint32_t y, const uint32_t a);

void ClearWorldFrame(uint16_t * frame_buffer);








