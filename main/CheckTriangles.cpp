#include <stdint.h>
#include <algorithm>

#include "geometry.h"
#include "globals.h"
#include "structures.h"

#include "esp_log.h" 

#include "CameraWork.h"
#include "TriangleQueues.h"
#include "ClipBound.h"
#include "CheckTriangles.h"
#include "ChunkChooser.h"

// A global pingpong flag
extern bool flipped;

// Transform a given vertex in clip-space [-w,w] to raster-space [0, {w|h}]
constexpr float half_width = g_scWidth/2;
constexpr float half_height = g_scHeight/2;

#define TO_RASTER(v) Vec4f((half_width * (v.x + v.w)), (half_height * (v.w - v.y)), v.z, v.w)

#define FOV 60.0f // field of view in degrees, will be compiled into radians

// If the triangle crosses the view frustrum it will be tiled before rasterisation and
// clearly there's a trade-off as smaller allows more elimination but extended to 1px square it's
// literally back to square one. Needs optimising on a platform and may depend on the world model too. 
const unsigned int g_xTile = 8; // Tile size needs to be optimised for screen size
const unsigned int g_yTile = 8; // 8x8 for 128x128 a maybe 32 x 24 for 320x240

Rect2D TriBoundBox;
//float farPlane = 100.0f;
const float nearPlane = 0.1f;

Matrix44f view = {
    1.0f,0.0f,0.0f,0.0f,
    0.0f,1.0f,0.0f,0.0f,
    0.0f,0.0f,1.0f,0.0f,
    0.0f,0.0f,0.0f,1.0f };

Matrix44f proj = {
    0.0f,0.0f,0.0f,0.0f,
    0.0f,0.0f,0.0f,0.0f,
    0.0f,0.0f,0.0f,0.0f,
    0.0f,0.0f,0.0f,0.0f };

// This is named after Vertex Shader which doesn't feel helpful
// Originally took matrices of position, view and model but in this
// implementation we aren't placing individual models into the world
//return (P * V * Vec4f(pos, 1.0f));
Vec4f VS(const Vec3f& pos, const Matrix44f& V)
{
    Vec4f result;
    V.multVecMatrix(pos, result); // On ESP32 the dspm_mult_f32_aes32() function gives no clear advantage
    return result;
} // End of VS

void ProjectionMatrix()
{
    // Build projection matrix (right-handed sysem)
    make_perspective((2.0f * M_PI) * (FOV / 360.0f), ((float)g_scWidth / (float)g_scHeight), nearPlane, farPlane, proj);
}

float edge_function(const Vec3f a, const Vec3f b, const Vec3f p)
{
    // Vec3f are passed even though Vec2f sufficient to save copying the input
    // From https://www.scratchapixel.com/lessons/3d-basic-rendering/rasterization-practical-implementation/rasterization-stage.html
    // Simpler than matrix used in main routines, note X and Z are horizontal axes
    return (p.x - a.x) * (b.z - a.z) - (p.z - a.z) * (b.x - a.x);
}; // end of edge_function

float BaseTriangles(const Vec3f eye, const Vec3f direction, const uint32_t this_chunk, const WorldLayout* layo_ptr)
{
    static const char *TAG = "BaseTriangles";
    // Static as if a match is not found the old result is returned
    static float this_height = 0.0f; // The ultimate result
    float found_height = 0.0f; // The height of the face being tested
    float best_height  = 0.0f; // The best result so far in the list of faces
    
    // Various reasons to quit without doing anything more
    // Return default height of 0.0f if nothing to check
    if (this_chunk == INVALID_CHUNK) return (0.0f); // Quit as it's invalid chunk beyond border, but we did need depth buffer reset perhaps

    if (layo_ptr->TheChunks[this_chunk].face_count == 0) return (0.0f); // Quit as this chunk is empty with zero faces, which is valid?

    // Projection calculation removed as a simple othographic view will be used
    const uint16_t* this_list = layo_ptr->TheChunks[this_chunk].faces_ptr; // fetch the list of faces applicable to this chunk

    // Currently there is no knowledge of whether there is more than one triangle beneath eye
    // so the height returned simply reflects the last that was hit from the list
    for (uint32_t get_face = 0; get_face < layo_ptr->TheChunks[this_chunk].face_count; get_face++)
    {
        // The chunk list is a subset of all triangles so pull the global index for rendering
        uint32_t idx = this_list[get_face]; // Keep a plain idx so code is easier to read in rest of this function

        // Fetch object-space vertices from the vertex buffer indexed by the values in index buffer
        const Vec3f v0 = layo_ptr->vertices[layo_ptr->nvertices[idx * 3]];
        const Vec3f v1 = layo_ptr->vertices[layo_ptr->nvertices[idx * 3 + 1]];
        const Vec3f v2 = layo_ptr->vertices[layo_ptr->nvertices[idx * 3 + 2]];

        // Simple non-homogenous edge functions are used as there is no need to consider issues such as clip space
        // One could do a bounding box calculation first but only a benefit if many triangles to check per chunk
        float w0 = edge_function(v1, v2, eye); // Signed area of the triangle v1v2p multiplied by 2
        float w1 = edge_function(v2, v0, eye); // Signed area of the triangle v2v0p multiplied by 2
        float w2 = edge_function(v0, v1, eye); // Signed area of the triangle v0v1p multiplied by 2

        // If point p is inside triangles defined by vertices v0, v1, v2
        if (w0 >= 0 && w1 >= 0 && w2 >= 0)
            {
            // Eye is over/inside the triangle

            // Do top left rule to avoid being over 2 abutting triangles
            // As above, it's a Vec2f operation but we haven't copied the vertices
            Vec3f edge0 = v2 - v1;
            Vec3f edge1 = v0 - v2;
            Vec3f edge2 = v1 - v0;

            bool overlaps = true;

            // If the point is on the edge, test if it is a top or left edge, 
            // otherwise test if the edge function is positive
            overlaps &= (w0 == 0 ? ((edge0.y == 0 && edge0.x > 0) || edge0.y > 0) : (w0 > 0));
            overlaps &= (w1 == 0 ? ((edge1.y == 0 && edge1.x > 0) || edge1.y > 0) : (w1 > 0));
            overlaps &= (w2 == 0 ? ((edge2.y == 0 && edge2.x > 0) || edge2.y > 0) : (w2 > 0));

            if (overlaps) // Check has passed
            {
                // The area is only needed if eye is inside the base triangle
                float oneoverarea = 1 / (edge_function(v0, v1, v2)); // Area of the triangle multiplied by 2
                // Barycentric coordinates are the areas of the sub-triangles divided by the area of the main triangle
                w0 *= oneoverarea;
                w1 *= oneoverarea;
                w2 *= oneoverarea;

                // Interpolate a height from the triangle's vertices
                found_height = w0 * v0.y + w1 * v1.y + w2 * v2.y;
                // In a 'bridge' or 'tunnel' there may be more than one face that is projected at the location
                // A chosen one must be lower than eye
                if (found_height < eye.y)
                {
                    // Now keep the highest of the previous best_height and that just found
                    // and update this_height as we have an acceptable match
                    this_height = best_height = std::max(best_height,found_height);    
                }
            } // End of overlap test
        }
        else
        {
        }
    // There is no explicit action if a face is NOT matched, last static value returned
    } // End of loop through triangles
    return(this_height); // Return the spot height from the underlying triangle
} // End of BaseTriangles 

uint32_t CheckTriangles(const Vec3f eye, const Vec3f direction, const uint32_t this_chunk, const WorldLayout* layo_ptr)
{
    static const char *TAG = "CheckTriangles";
    extern Time_tracked time_report;

    // Various reasons to quit without doing anything more
    if (this_chunk == INVALID_CHUNK) return (0); // Quit as it's invalid chunk beyond border, but we did need depth buffer reset perhaps

    if (layo_ptr->TheChunks[this_chunk].face_count == 0) return (0); // Quit as this chunk is empty with zero faces, which is valid?

    uint32_t pixels_done = 0; // Tracks the rasterised pixels to give indication of workload

    Vec3f clip_zs; // This will hold 'z' of the three clipped vertices 

    TriToRaster this_tri; // A structure to pass to rasteriser

    make_camera(direction, eye, view); // coded to replace glm::lookat function at lower level

    // Multiply view and projection matrices here as there is no need to do this within the triangle loop
    Matrix44f ViewProj = view * proj;

    const uint16_t* this_list = layo_ptr->TheChunks[this_chunk].faces_ptr; // fetch the list of faces applicable to this chunk

    for (uint32_t get_face = 0; get_face < layo_ptr->TheChunks[this_chunk].face_count; get_face++)
    {
        time_report.triangles++; // Keep count of primitives processed
        // The chunk list is a subset of all triangles so pull the global index for rendering
        uint32_t idx = this_list[get_face]; // Keep a plain idx so code is easier to read in rest of this function

        this_tri.idx = this_list[get_face]; // For passing to rasteriser stuct
        this_tri.layout = layo_ptr; // Pass the layout as ptr to the queue on a per triangle basis

        // Fetch object-space vertices from the vertex buffer indexed by the values in index buffer
        // and pass them directly to each VS invocation
        const Vec3f v0 = layo_ptr->vertices[layo_ptr->nvertices[idx * 3]];
        const Vec3f v1 = layo_ptr->vertices[layo_ptr->nvertices[idx * 3 + 1]];
        const Vec3f v2 = layo_ptr->vertices[layo_ptr->nvertices[idx * 3 + 2]];

        // Invoke function for each vertex of the triangle to transform them from object-space to clip-space (-w, w)
        Vec4f v0Clip = VS(v0, ViewProj);
        Vec4f v1Clip = VS(v1, ViewProj);
        Vec4f v2Clip = VS(v2, ViewProj);

        this_tri.clip_zs = { v0Clip.z, v1Clip.z, v2Clip.z }; // For passing to rasteriser struct

        // Apply viewport transformation
        // Notice that we haven't applied homogeneous division and are still utilizing homogeneous coordinates
        Vec4f v0Homogen = TO_RASTER(v0Clip);
        Vec4f v1Homogen = TO_RASTER(v1Clip);
        Vec4f v2Homogen = TO_RASTER(v2Clip);

        // Base vertex matrix
        Matrix33f M =
        {
             v0Homogen.x, v1Homogen.x, v2Homogen.x,
             v0Homogen.y, v1Homogen.y, v2Homogen.y,
             v0Homogen.w, v1Homogen.w, v2Homogen.w,
        };

        // Singular vertex matrix (det(M) == 0.0) means that the triangle has zero area,
        // which in turn means that it's a degenerate triangle which should not be rendered anyways,
        // whereas (det(M) > 0) implies a back-facing triangle so we're going to skip such primitives

        // Do backface culling as early as possible so not sent to rasterise
        float det = M.determinant();
        if (det >= 0.0f)
        {
            //tri_backface++; // Track how many are facing away
            continue;
        }

        // Worth doing more if not culled already

        // Work out brightness of the face
        //this_tri.face_brightness = MakeShade(idx, eye, direction, layo_ptr);
        MakeShade(idx, eye, direction, layo_ptr,& this_tri.face_brightness);

        // Compute the inverse of vertex matrix to use it for setting up edge & constant functions
        Matrix33f invM = M.inverse();
        this_tri.invM = invM; // For passing to rasteriser stuct

        // Constants for primitive interpolation could be done here and added to tri structure
        // Calculate constant function to interpolate 1/w
        invM.multVecMatrix(Vec3f(1, 1, 1), this_tri.C);
        // Calculate z interpolation vector
        invM.multVecMatrix(this_tri.clip_zs, this_tri.Z);

        //switch (ExecuteFullTriangleClipping(idx, v0Clip, v1Clip, v2Clip, &TriBoundBox))
        switch (ExecuteFullTriangleClipping(v0Clip, v1Clip, v2Clip, &TriBoundBox))
        {
        case CLIP_TR:
            //tri_not_rendered++;
            continue; // Bounding box is totally off screen
        case CLIP_TA:
            // the simplest function parameter passing approach is illustrated, 
            // but now uses a struct to allow array of primitives and chnges to the parameter list
            // pixels_done += RasteriseBox(idx, v0Clip, v1Clip, v2Clip, TileBox, M);

            this_tri.BoBox = TriBoundBox; // The other parameters have been pre-populated
//            pixels_done += RasteriseBox(this_tri);
            if (flipped) pixels_done += QueueTriangle(this_tri,2);
            else pixels_done += QueueTriangle(this_tri,0);

            break;
        case CLIP_MC:
            // Make the edge cooefficients for this triangle
 
            // Set up edge functions based on the vertex matrix
            Vec3f E0 = { invM[0][0], invM[0][1], invM[0][2] };
            Vec3f E1 = { invM[1][0], invM[1][1], invM[1][2] };
            Vec3f E2 = { invM[2][0], invM[2][1], invM[2][2] };

            // Normalize edge functions
            E0 /= (abs(E0.x) + abs(E0.y));
            E1 /= (abs(E1.x) + abs(E1.y));
            E2 /= (abs(E2.x) + abs(E2.y));

            // Indices of tile corners:
            // LL -> 0  LR -> 1
            // UL -> 2  UR -> 3

            const Vec2f scTileCornerOffsets[] = // this was const
            {
                { 0.f, 0.f},        // LL
                { g_xTile, 0.f },   // LR
                { 0.f, g_yTile },   // UL
                { g_xTile, g_yTile} // UR
            };

            // (x, y) -> sample location | (a, b, c) -> edge equation coefficients
            // E(x, y) = (a * x) + (b * y) + c
            // E(x + s, y + t) = E(x, y) + (a * s) + (b * t)

            // Based on edge normal n=(a, b), set up tile TR corners for each edge
            const uint8_t edge0TRCorner = (E0.y >= 0.f) ? ((E0.x >= 0.f) ? 3u : 2u) : (E0.x >= 0.f) ? 1u : 0u;
            const uint8_t edge1TRCorner = (E1.y >= 0.f) ? ((E1.x >= 0.f) ? 3u : 2u) : (E1.x >= 0.f) ? 1u : 0u;
            const uint8_t edge2TRCorner = (E2.y >= 0.f) ? ((E2.x >= 0.f) ? 3u : 2u) : (E2.x >= 0.f) ? 1u : 0u;

            // TA corner is the one diagonal from TR corner calculated above
            const uint8_t edge0TACorner = 3u - edge0TRCorner;
            const uint8_t edge1TACorner = 3u - edge1TRCorner;
            const uint8_t edge2TACorner = 3u - edge2TRCorner;

            // Evaluate edge equation at first tile origin
            // Surely origin is zero for our first case as we returned a full screen bounding box
            const float edgeFunc0 = E0.z; // +((E0.x * tilePosX) + (E0.y * tilePosY));
            const float edgeFunc1 = E1.z; // +((E1.x * tilePosX) + (E1.y * tilePosY));
            const float edgeFunc2 = E2.z; // +((E2.x * tilePosX) + (E2.y * tilePosY));

            // Break the full screen bounding box into tiles and rasterise them individually
            for (uint32_t ty = 0, tyy = 0; ty < TriBoundBox.m_MaxY / g_yTile; ty++, tyy++)
            {
                for (uint32_t tx = 0, txx = 0; tx < TriBoundBox.m_MaxX / g_xTile; tx++, txx++)
                {
                    // Using EE coefficients calculated in TriangleSetup stage and positive half-space tests, determine one of three cases possible for each tile:
                    // 1) TrivialReject -- tile within tri's bbox does not intersect tri -> move on
                    // 2) TrivialAccept -- tile within tri's bbox is completely within tri -> rasterise with no edge checks
                    // 3) Overlap       -- tile within tri's bbox intersects tri -> rasterization with edge checks on each pixel

                    // (txx, tyy) = how many steps are done per dimension
                    const float txxOffset = static_cast<float>(txx * g_xTile);
                    const float tyyOffset = static_cast<float>(tyy * g_yTile);

                    // Step from edge function computed above for the first tile in bbox
                    float edgeFuncTR0 = edgeFunc0 + ((E0.x * (scTileCornerOffsets[edge0TRCorner].x + txxOffset)) + (E0.y * (scTileCornerOffsets[edge0TRCorner].y + tyyOffset)));
                    float edgeFuncTR1 = edgeFunc1 + ((E1.x * (scTileCornerOffsets[edge1TRCorner].x + txxOffset)) + (E1.y * (scTileCornerOffsets[edge1TRCorner].y + tyyOffset)));
                    float edgeFuncTR2 = edgeFunc2 + ((E2.x * (scTileCornerOffsets[edge2TRCorner].x + txxOffset)) + (E2.y * (scTileCornerOffsets[edge2TRCorner].y + tyyOffset)));

                    // If TR corner of the tile is outside any edge, reject whole tile
                    bool TRForEdge0 = (edgeFuncTR0 < 0.f);
                    bool TRForEdge1 = (edgeFuncTR1 < 0.f);
                    bool TRForEdge2 = (edgeFuncTR2 < 0.f);

                    if (TRForEdge0 || TRForEdge1 || TRForEdge2)
                    {
                        //tile_rejected++;
                        // TrivialReject
                        // Tile is completely outside of one or more edges
                        continue; // Skip to next tile now
                    }

                    // Is the TA testing worthwhile if most are passed to be partial anyway?
                    // Tile is partially or completely inside one or more edges, do TrivialAccept tests first
                    // Compute edge functions at TA corners based on edge function at first tile origin
                    float edgeFuncTA0 = edgeFunc0 + ((E0.x * (scTileCornerOffsets[edge0TACorner].x + txxOffset)) + (E0.y * (scTileCornerOffsets[edge0TACorner].y + tyyOffset)));
                    float edgeFuncTA1 = edgeFunc1 + ((E1.x * (scTileCornerOffsets[edge1TACorner].x + txxOffset)) + (E1.y * (scTileCornerOffsets[edge1TACorner].y + tyyOffset)));
                    float edgeFuncTA2 = edgeFunc2 + ((E2.x * (scTileCornerOffsets[edge2TACorner].x + txxOffset)) + (E2.y * (scTileCornerOffsets[edge2TACorner].y + tyyOffset)));

                    // If TA corner of the tile is outside all edges, accept whole tile
                    bool TAForEdge0 = (edgeFuncTA0 >= 0.f);
                    bool TAForEdge1 = (edgeFuncTA1 >= 0.f);
                    bool TAForEdge2 = (edgeFuncTA2 >= 0.f);

                    if (TAForEdge0 && TAForEdge1 && TAForEdge2)
                    {
                        //tile_accepted++;
                        pixels_done += (g_xTile * g_yTile); // No need to count individual pixerls as it's all rasterised

                        // TrivialAccept
                        // Tile is completely inside of the triangle, so no edge checks needed,
                        // whole tile will be fragment-shaded, with interpolation done in simplified rasteriser

                        // Send tile for rasterisation without edge checking
                        this_tri.BoBox = { (float)(tx * g_xTile),(float)(ty * g_yTile),(float)((tx + 1) * g_xTile),(float)((ty + 1) * g_yTile) };
 //                       NotRasteriseBox(this_tri);
                        if (flipped) QueueTriangle(this_tri, 3); // Push to Not queue
                        else QueueTriangle(this_tri, 1);
                        continue; // Skip to next tile now
                    }
                    // By default the tile must be only partially covered
                    //tile_partial++;
                    // Make a box for each tile within the full screen bound box
                    this_tri.BoBox = { (float)(tx * g_xTile),(float)(ty * g_yTile),(float)((tx + 1) * g_xTile),(float)((ty + 1) * g_yTile) };
                    // and do normal rasterisation with edge checking
//                    pixels_done += RasteriseBox(this_tri);
                    if (flipped) pixels_done += QueueTriangle(this_tri,2);
                    else pixels_done += QueueTriangle(this_tri,0);

                } // End of x tile loop
            } // End of y tile loop
            break;
        } // End of switch on clip outcomes
    } // End of loop through triangles
    return(pixels_done); // Let the caller know how much was displayed
} // End of CheckTriangles 

// ************************************************************************************************
// Basic shader that calculates face normal on the fly (rather than fetching pre-calculated)
// and is called as a triangle is processed so passed to rasteriser via queue
// The model uses embedded roughness to calculate uint32_t multipliers

void MakeShade(uint32_t idx, const Vec3f eye, const Vec3f direction, const WorldLayout* layo_ptr, Shade_params* surface_shade)
{
    static const char *TAG = "MakeShade";

    // Fetch object-space vertices from the vertex buffer indexed by the values in index buffer
    const Vec3f& v0 = layo_ptr->vertices[layo_ptr->nvertices[idx * 3]];
    const Vec3f& v1 = layo_ptr->vertices[layo_ptr->nvertices[idx * 3 + 1]];
    const Vec3f& v2 = layo_ptr->vertices[layo_ptr->nvertices[idx * 3 + 2]];

    // Read the face colour from the palette
    // which has high order byte Ns for block colours and textures from mtl file   
    // Finally scale to 0 to 1.0
    const uint32_t this_Ns = (0xff000000 & (layo_ptr->palette[layo_ptr->attributes[idx]].rgb888)) >> 24;
    float Ns = ((float) this_Ns) / 255.0f;

    // Calculate the face normal
    // This should be from original vertices rather than projected
    Vec3f FaceNormal = (v1 - v0).crossProduct(v2 - v0);
    FaceNormal.normalize();
    // The dot product between face normal and the incident light shades the face
    // Negative values are set to zero, in theory will not exceed 1.0f but wise to clamp
    float lambertian = std::clamp(FaceNormal.dotProduct(IncidentLight), 0.f, 1.f);

    // Calculate the Halfway vector simplifed from
    // https://learn.microsoft.com/en-us/windows/uwp/graphics-concepts/specular-lighting
    // done on a face basis rather than vertex
    Vec3f face_centre = (v0 + v1 + v2);
    face_centre /= 3;
    const Vec3f H = ((eye - face_centre).normalize() - IncidentLight).normalize(); // Sign of incident light has been reversed

    // The dot product between the face normal and eye shades according to whether it faces viewer
    // As above, clamp away negative values
    float shine = std::clamp(FaceNormal.dotProduct(H), 0.f, 1.f);
    // A power of shine is needed, traditionaly ^4 is a good starting point to focus specular
    shine = shine * shine * shine * shine * shine * shine; // Or as pow(), but that is double
    //ESP_LOGI(TAG,"shine %f", shine );

    // Diffuse is base lighting with a Lambertian proportion
    // whereas a shiney surface is most affected by angle to viewer
    const float Ns_power = (Ns * 0.5f); // How much of the Ns is fed into specular
    float diffuse = (1.0f - Ns_power) * 0.5f + lambertian * 0.5f;
    float specular = Ns_power * (0.1f + shine * 0.9f);

    // The degree of roughness is indicated by Ns and so the two elements above are mixed
    // Large Ns is more specular
    //float shade = (specular * (Ns)) + (diffuse * (1 - Ns));
    //surface_shade->x  = diffuse;
    //surface_shade->y  = specular;

    surface_shade->lamb = static_cast<uint32_t>(std::clamp(256.f * diffuse, 0.f, 255.f));
    surface_shade->spec = static_cast<uint32_t>(std::clamp(256.f * specular, 0.f, 255.f));
} // End of MakeShade()
