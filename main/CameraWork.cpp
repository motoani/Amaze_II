#include <math.h>

#include "geometry.h"
#include "CameraWork.h"


void make_camera(const Vec3f direction, const Vec3f cameraPosition, Matrix44f& worldToCamera)
{
    // Build the view matrix as per https://medium.com/@carmencincotti/lets-look-at-magic-lookat-matrices-c77e53ebdf78
    // This has worked with basic geometry functions but converted to glm to ensure that it works with the rest of 3D code

    Vec3f forwardvector = (-direction).normalize();

    const Vec3f tempup = { 0,1,0 };
    Vec3f rightvector = (tempup.crossProduct(forwardvector)).normalize();

    Vec3f upvector = (forwardvector.crossProduct(rightvector)).normalize();

    Vec3f translate;
    translate.x = rightvector.dotProduct(cameraPosition);
    translate.y = upvector.dotProduct(cameraPosition);
    translate.z = forwardvector.dotProduct(cameraPosition);

    Matrix44f LookAt = {
            rightvector.x,rightvector.y,rightvector.z,-translate.x,
            upvector.x,upvector.y,upvector.z,-translate.y,
            forwardvector.x,forwardvector.y,forwardvector.z,-translate.z,
            0,0,0,1 };

    worldToCamera = LookAt.transposed();
} // End of make_camera()

// Make a perspective projection matrix, simplified from glm opensource library
// so that can be used without glm library
void make_perspective(
    float const fovy, // must be in radians
    float const aspect,
    float const zNear,
    float const zFar,
    Matrix44f& Result
)
{
    float tanHalfFovy = tan(fovy / 2.0f);

    Result[0][0] = 1.0f / (aspect * tanHalfFovy);
    Result[1][1] = 1.0f / (tanHalfFovy);
    Result[2][2] = -(zFar + zNear) / (zFar - zNear);
    Result[2][3] = -1.0f;
    Result[3][2] = -(2.0f * zFar * zNear) / (zFar - zNear);
} // End of make_perspective

