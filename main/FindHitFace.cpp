#include <stdint.h>
#include <algorithm>

#include "esp_log.h" 

#include "globals.h"
#include "geometry.h"
#include "structures.h"

#include "ShowError.h"

#include "FindHitFace.h"
#include "RasteriseBox.h" // For CheckEdgeFunction

// ************************************************************************************************
// Checks if a triangle is intersected by the viewer pixel test_x and test_y
// using rasteriser layout, returns LARGE_FLOAT if not contained or a smaller z value if found
float CheckHitFace(const TriToRaster & tri, const uint32_t test_x, const uint32_t test_y)
{    
    //static const char *TAG = "CheckHitFace";
    const Matrix33f invM = tri.invM;

    // Check if text_x and test_y lie within the bounding box as a first step
    if (! TestBoBox(tri.BoBox, test_x, test_y)) return (farPlane); 

    float z = farPlane;

    // Homogenous appraoch to edge function used as triangles may extend beyond view frustrum
    // Set up edge functions based on the vertex matrix
    const Vec3f E0 = { invM[0][0], invM[0][1], invM[0][2] };
    const Vec3f E1 = { invM[1][0], invM[1][1], invM[1][2] };
    const Vec3f E2 = { invM[2][0], invM[2][1], invM[2][2] };

    // Calculate constant function to interpolate 1/w
    const Vec3f C = tri.C;

    // Calculate z interpolation vector
    const Vec3f Z = tri.Z;

    Vec3f Sample = { test_x + 0.5f, test_y + 0.5f, 1.0f };

    // Set up the EvaluateEdgeFunction
    float Edge0 = (E0.x * Sample.x) + (E0.y * Sample.y) + E0.z;
    float Edge1 = (E1.x * Sample.x) + (E1.y * Sample.y) + E1.z;
    float Edge2 = (E2.x * Sample.x) + (E2.y * Sample.y) + E2.z;

   
    // Check if the text pixel is within the projected triangle
    if (CheckEdgeFunction(E0, Edge0) && CheckEdgeFunction(E1, Edge1) && CheckEdgeFunction(E2, Edge2))
        {
        // It is inside so do further work
        // Find z that will be used for depth test
        const float w = 1/((C.x * Sample.x) + (C.y * Sample.y) + C.z);
        z = w * ((Z.x * Sample.x) + (Z.y * Sample.y) + Z.z);
        }
    return (z); // LARGE_FLOAT if not contained
} // End of CheckFace

// Tests the pixel z depth against a tiled primitive, so no edge checking needed
float CheckHitTile(const TriToRaster & tri, const uint32_t test_x, const uint32_t test_y)
{
    //static const char *TAG = "CheckHitTile";
    
    // Check if text_x and test_y lie within the bounding box as a first step and exit if not
    if (! TestBoBox(tri.BoBox, test_x, test_y)) return (farPlane); 

    // Calculate constant function to interpolate 1/w
    const Vec3f C = tri.C;

    // Calculate z interpolation vector
    const Vec3f Z = tri.Z;

    // The triangle does need to be checked
    Vec3f Sample = { test_x + 0.5f, test_y + 0.5f, 1.0f };
    // Find z that will be used for depth test
    const float w = 1/((C.x * Sample.x) + (C.y * Sample.y) + C.z);
    const float z = w * ((Z.x * Sample.x) + (Z.y * Sample.y) + Z.z);
return (z);
} // End of CheckHitTile

// For hit checking, basic test if the test pixel is within the bounding box for rendering
inline bool TestBoBox(const Rect2D box, const uint32_t test_x, const uint32_t test_y)
{
    if ((test_x >= (uint32_t)box.m_MinX) && (test_x <= (uint32_t)box.m_MaxX) &&
        (test_y >= (uint32_t)box.m_MinY) && (test_y <= (uint32_t)box.m_MaxY))
        return (true);
    else return (false);    
} // End of TestBoBox

