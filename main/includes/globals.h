#pragma once

#include <stdint.h>
#include <math.h>
#include "geometry.h"

// Shared global variables

// Size of the game view port 
const uint32_t g_scWidth = 128; // This means that the constant is included at compile time multiple times
const uint32_t g_scHeight = 128;

const float farPlane = 100.0f;

const float COLLISION_DISTANCE = 1.0f;

const uint32_t MAX_FRAME_DURATION = 99;

const Vec3f IncidentLight = { 0.548821f, -0.329293f, 0.768350f }; // The direction by which light reaches the world, must be magnitude 1.0f
