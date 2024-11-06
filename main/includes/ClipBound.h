#pragma once

#include <stdint.h>
#include "geometry.h"
#include "structures.h"

unsigned int ExecuteFullTriangleClipping(const Vec4f& v0Clip, const Vec4f& v1Clip, const Vec4f& v2Clip, Rect2D* pBbox);

Rect2D ComputeBoundingBox(const Vec4f& v0Clip, const Vec4f& v1Clip, const Vec4f& v2Clip, float width, float height);


