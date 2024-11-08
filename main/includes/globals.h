#pragma once

#include <stdint.h>
#include <math.h>
#include "geometry.h"

// Shared global variables

const float farPlane = 100.0f;

const float COLLISION_DISTANCE = 1.0f;

const uint32_t MAX_FRAME_DURATION = 99; // In ms to guide chunk choice on frame rate

const Vec3f IncidentLight = { 0.548821f, -0.329293f, 0.768350f }; // The direction by which light reaches the world, must be magnitude 1.0f
