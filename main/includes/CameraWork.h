#pragma once

#include "geometry.h"

void make_camera(const Vec3f direction, const Vec3f cameraPosition, Matrix44f& worldToCamera);

void make_perspective(
    float const fovy, // must be in radians
    float const aspect,
    float const zNear,
    float const zFar,
    Matrix44f& Result
);

