#include <stdint.h>
#include <algorithm>
#include "globals.h"
#include "geometry.h"
#include "structures.h"
#include "CheckTriangles.h"

#include "ClipBound.h"


// Although the homogenous coordinate sytem renders triangles off screen well it wastes time
// on triangles crossing frustrum as it returns a full bounding box
// Tiling will deal with that!
// This routine is based on source from https://tayfunkayhan.wordpress.com/2019/07/26/chasing-triangles-in-a-tile-based-rasterizer/

unsigned int ExecuteFullTriangleClipping(const Vec4f& v0Clip, const Vec4f& v1Clip, const Vec4f& v2Clip, Rect2D* pBbox)
{
    
        // Clip-space positions are to be bounded by:
        // -w < x < w   -> LEFT/RIGHT
        // -w < y < w   -> TOP/BOTTOM
        //  0 < z < w   -> NEAR/FAR
        // However, we will only clip primitives that are *completely* outside of any of clipping planes.
        // This means that, triangles intersecting view frustum  are passed as-is, to be rasterized as usual.
        // Because we're utilizing homogeneous rasterization, we don't need to do explcit line-clipping here.

        // Clip against w+x=0 left plane
    bool allOutsideLeftPlane =
        (v0Clip.x < -v0Clip.w) &&
        (v1Clip.x < -v1Clip.w) &&
        (v2Clip.x < -v2Clip.w);

    bool allInsideLeftPlane =
        (v0Clip.x >= -v0Clip.w) &&
        (v1Clip.x >= -v1Clip.w) &&
        (v2Clip.x >= -v2Clip.w);

    // Clip against w-x=0 right plane
    bool allOutsideRightPlane =
        (v0Clip.x > v0Clip.w) &&
        (v1Clip.x > v1Clip.w) &&
        (v2Clip.x > v2Clip.w);

    bool allInsideRightPlane =
        (v0Clip.x <= v0Clip.w) &&
        (v1Clip.x <= v1Clip.w) &&
        (v2Clip.x <= v2Clip.w);

    // Clip against w+y top plane
    bool allOutsideBottomPlane =
        (v0Clip.y < -v0Clip.w) &&
        (v1Clip.y < -v1Clip.w) &&
        (v2Clip.y < -v2Clip.w);

    bool allInsideBottomPlane =
        (v0Clip.y >= -v0Clip.w) &&
        (v1Clip.y >= -v1Clip.w) &&
        (v2Clip.y >= -v2Clip.w);

    // Clip against w-y bottom plane
    bool allOutsideTopPlane =
        (v0Clip.y > v0Clip.w) &&
        (v1Clip.y > v1Clip.w) &&
        (v2Clip.y > v2Clip.w);

    bool allInsideTopPlane =
        (v0Clip.y <= v0Clip.w) &&
        (v1Clip.y <= v1Clip.w) &&
        (v2Clip.y <= v2Clip.w);

    // Clip against 0<z near plane
    bool allOutsideNearPlane =
        (v0Clip.z < 0.f) &&
        (v1Clip.z < 0.f) &&
        (v2Clip.z < 0.f);

    bool allInsideNearPlane =
        (v0Clip.z >= 0.f) &&
        (v1Clip.z >= 0.f) &&
        (v2Clip.z >= 0.f);

    // Clip against z>w far plane
    bool allOutsideFarPlane =
        (v0Clip.z > v0Clip.w) &&
        (v1Clip.z > v1Clip.w) &&
        (v2Clip.z > v2Clip.w);

    bool allInsideFarPlane =
        (v0Clip.z <= v0Clip.w) &&
        (v1Clip.z <= v1Clip.w) &&
        (v2Clip.z <= v2Clip.w);

    float width = g_scWidth; // Not ideal but quick and easy
    float height = g_scHeight;

    if (allOutsideLeftPlane ||
        allOutsideRightPlane ||
        allOutsideBottomPlane ||
        allOutsideTopPlane ||
        allOutsideNearPlane ||
        allOutsideFarPlane)
    {
        // TRIVIALREJECT case

        // Primitive completely outside of one of the clip planes, discard it
        return CLIP_TR;
    }
    else if (allInsideLeftPlane &&
        allInsideRightPlane &&
        allInsideBottomPlane &&
        allInsideTopPlane &&
        allInsideNearPlane &&
        allInsideFarPlane)
    {
        // TRIVIALACCEPT
        //tri_trivial_accept++;

        //LOG("Prim %d TA'd in FT-clipper by thread %d", primIdx, m_ThreadIdx);

        // Primitive is completely inside view frustum

        // Compute bounding box
        Rect2D bbox = ComputeBoundingBox(v0Clip, v1Clip, v2Clip, width, height);

        // Clamp bbox to screen extents
        bbox.m_MinX = std::max(0.0f, bbox.m_MinX);
        bbox.m_MaxX = std::min(width - 1, bbox.m_MaxX);
        bbox.m_MinY = std::max(0.0f, bbox.m_MinY);
        bbox.m_MaxY = std::min(height - 1, bbox.m_MaxY);

        *pBbox = bbox;

        return CLIP_TA;
    }
    else
    {
        // MUSTCLIP

        // Primitive is partially inside view frustum, but we don't clip for this
        // so we must be conservative and return the whole range to rasterize further.
        // Note that is *overly* conservative in practice; we could do better by implementing
        // Blinn's method of screen coverage calculation properly but it's an overkill here.
        // Note that simple use of Bounding Box doesn't work correctly even with homogenous coordinates

        //tri_mustclip++;

        Rect2D bbox =
        {
            0.f,
            0.f,
            width,
            height
        };

        *pBbox = bbox;

        return CLIP_MC;
    }
}

Rect2D ComputeBoundingBox(const Vec4f& v0Clip, const Vec4f& v1Clip, const Vec4f& v2Clip, float width, float height)
{
    Vec2f v0Raster = Vec2f((g_scWidth * (v0Clip.x + v0Clip.w) / (2 * v0Clip.w)), (g_scHeight * (v0Clip.w - v0Clip.y) / (2 * v0Clip.w)));
    Vec2f v1Raster = Vec2f((g_scWidth * (v1Clip.x + v1Clip.w) / (2 * v1Clip.w)), (g_scHeight * (v1Clip.w - v1Clip.y) / (2 * v1Clip.w)));
    Vec2f v2Raster = Vec2f((g_scWidth * (v2Clip.x + v2Clip.w) / (2 * v2Clip.w)), (g_scHeight * (v2Clip.w - v2Clip.y) / (2 * v2Clip.w)));


    // Find min/max in X & Y

    float xmin = std::min(v0Raster.x, std::min(v1Raster.x, v2Raster.x));
    float xmax = std::max(v0Raster.x, std::max(v1Raster.x, v2Raster.x));
    float ymin = std::min(v0Raster.y, std::min(v1Raster.y, v2Raster.y));
    float ymax = std::max(v0Raster.y, std::max(v1Raster.y, v2Raster.y));

    Rect2D bbox;
    bbox.m_MinX = xmin;
    bbox.m_MinY = ymin;
    bbox.m_MaxX = xmax;
    bbox.m_MaxY = ymax;

    //box_count++; // track how many boxes are made
    //box_length += (long unsigned int)(xmax - xmin + ymax - ymin); // calculate  w + h for this box

    return (bbox);
}

