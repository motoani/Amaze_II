#include <stdint.h>
#include <algorithm>

#include "esp_log.h" 
#include "esp_random.h"
//#include "esp_dsp.h"

#include "globals.h"
#include "geometry.h"
#include "structures.h"

#include "ShowError.h"

#include "RasteriseBox.h"

float* depthBuffer; // depthBuffer restricted in scope to this unit, albeit globally
extern uint16_t * frame_buffer_this;

#define TEXTURE_DEPTH_THRESHOLD 22.f // Depth at which textures are disabled and base colour sent

// ************************************************************************************************
// Start with various support functions for rasteriser

void MakeDepthBuffer()
{
    depthBuffer = (float*)malloc(sizeof(float) * g_scWidth * g_scHeight);
    if (!depthBuffer)
    {
        show_error("Failed to allocate depth buffer");
    }
}

void ClearDepthBuffer(float farPlane)
{
    // Passing farPlane as we may use it dynamically to affect redraw speed
    // Clear the depth buffer to a high z now we are using this rather than 1/w
    for (int pixel = 0; pixel < g_scWidth * g_scHeight; ++pixel)
    {
        depthBuffer[pixel] = farPlane;
    }
}

void CheckCollide(Near_pix * near)
{
    // Just scan part of the area as an 'eyeline'
    
    //uint32_t collide_colour;
    
    // Aim to do calculation at compile time, divides invovled!
    constexpr uint32_t y_start = g_scHeight / 5;
    constexpr uint32_t y_end = 3 * g_scHeight / 5;
    constexpr uint32_t x_start = g_scWidth / 4;
    constexpr uint32_t x_end = 3 * g_scWidth / 4;

    near->depth = farPlane; // Initial value for nearest is farClip of view

    for (uint32_t y = y_start; y < y_end; y = y + 2)
    {
        for (uint32_t x = x_start + (y%4); x < x_end; x = x + 4) 
        {
            if (depthBuffer[y * g_scWidth + x] < near->depth)
            {
                near->depth = depthBuffer[y * g_scWidth + x];
                near->x = x; // Store the point that is the nearest
                near->y = y; 
            }
            /*
            { // Just for illustration, colour each test point
                collide_colour = 0x0000ff00;
                if (depthBuffer[y * g_scWidth + x] < COLLISION_DISTANCE * 1.5f) collide_colour = 0x000000ff;
                if (depthBuffer[y * g_scWidth + x] < COLLISION_DISTANCE) collide_colour = 0x00ff0000;
            }
            WritePixel888(y * g_scWidth + x, collide_colour, 1.0f);
            */
        }
    }
    //return(nearest); // return the distance of the nearest scanned point
}


// Here we only check the edge function and will increment it elsewhere
// has been inline
bool CheckEdgeFunction(const Vec3f& E, const float result)
{
    // Apply tie-breaking rules on shared vertices in order to avoid double-shading fragments
    if (result > 0.0f) return true;
    else if (result < 0.0f) return false;

    // These tests only done if result is zero which seems highly improbable
    // A breakpoint here is never used in the benchmark view
    if (E.x > 0.f) return true;
    else if (E.x < 0.0f) return false;

    if ((E.x == 0.0f) && (E.y < 0.0f)) return false;
    else return true;
}

// ************************************************************************************************
// Rasterises a primitive triangle using passed struct with edge checking and
// interpolating z and UV mapping
//uint32_t RasteriseBox(const TriToRaster& tri)
void RasteriseBox(const TriToRaster & tri)
{    
    //static const char *TAG = "RasteriseBox";
     const uint32_t idx = tri.idx; // idx is used so many times it makes sense to have this stage, compiler might delete it?
    const Rect2D TriBoundBox = tri.BoBox;
    const Matrix33f invM = tri.invM;
    uint32_t this_colour = 0; // This will be filled with face or texture colour

    //WorldLayout layo = *tri.layout; // Find the layout structure from the ptr passed to here
    const WorldLayout* layo_ptr = tri.layout;

    //uint32_t pixels_done = 0; // Track the load

    // M determinant and inverse used to be calculated here but better to pass invM in and backface cull in CheckTriangles

    // Set up edge functions based on the vertex matrix
    Vec3f E0 = { invM[0][0], invM[0][1], invM[0][2] };
    Vec3f E1 = { invM[1][0], invM[1][1], invM[1][2] };
    Vec3f E2 = { invM[2][0], invM[2][1], invM[2][2] };


   // Calculate constant function to interpolate 1/w
    //Vec3f C;
    //invM.multVecMatrix(Vec3f(1, 1, 1), C);
    const Vec3f C = tri.C;

    // Calculate z interpolation vector
    // tri.clip_zs is equivalent of sample Vec3f shown
    // invM.multVecMatrix(Vec3f(v0Clip.z, v1Clip.z, v2Clip.z), Z);

    // Calculate z interpolation vector
    //Vec3f Z;
    //invM.multVecMatrix(tri.clip_zs, Z);
    const Vec3f Z = tri.Z;

    // Fetch the shading overview for the triangle
    const Shade_params surface = tri.face_brightness;
    
    // This has been pulled out of the pixel loop as it is sufficient to do once per box (or actually per triangle) 
    Vec3f PUVS, PUVT; // They have to be declared is the later IF doesn't know if they are needed!
    bool Texturise = false; // Is the box valid for a texture, a size test was trialled but at 128x128px some triangles are only 1 px!
                            // If it is small then just use base colour

    if (layo_ptr->palette[layo_ptr->attributes[idx]].width && (TriBoundBox.m_MaxX-TriBoundBox.m_MinX) > 6 && (TriBoundBox.m_MaxY-TriBoundBox.m_MinY) > 6)
    {
        Texturise = true;
        // Calculate UV interpolation vector
        invM.multVecMatrix(Vec3f(layo_ptr->vts[layo_ptr->texel_verts[idx * 3 + 0]].x, layo_ptr->vts[layo_ptr->texel_verts[idx * 3 + 1]].x, layo_ptr->vts[layo_ptr->texel_verts[idx * 3 + 2]].x), PUVS);
        invM.multVecMatrix(Vec3f(layo_ptr->vts[layo_ptr->texel_verts[idx * 3 + 0]].y, layo_ptr->vts[layo_ptr->texel_verts[idx * 3 + 1]].y, layo_ptr->vts[layo_ptr->texel_verts[idx * 3 + 2]].y), PUVT);
    }
    else
    {   
        // Read the face colour from the palette which will be used for untextured primitives
        this_colour = layo_ptr->palette[layo_ptr->attributes[idx]].rgb888;
        // Adjust primitive colour based on surface diffuse and specular components

        this_colour = spec_shade_pixel (this_colour, surface);
    }
    //if ( &(layo_ptr->attributes[idx]) & 0x01 ) ESP_LOGI(TAG, "Odd attribute address ****");
    // sample for the edge function at the first pixel for this rectangle
    Vec3f StartSample = { (unsigned int)TriBoundBox.m_MinX + 0.5f, (unsigned int)TriBoundBox.m_MinY + 0.5f, 1.0f };

    // Do the first EvaluateEdgeFunction
    float EdgeFirst0 = (E0.x * StartSample.x) + (E0.y * StartSample.y) + E0.z;
    float EdgeFirst1 = (E1.x * StartSample.x) + (E1.y * StartSample.y) + E1.z;
    float EdgeFirst2 = (E2.x * StartSample.x) + (E2.y * StartSample.y) + E2.z;

    // w and z can be estimated incrementally too which saves multiplication
    float oneOverWFirst = (C.x * StartSample.x) + (C.y * StartSample.y) + C.z;

    // Interpolate z that will be used for depth test
    float zOverWFirst = (Z.x * StartSample.x) + (Z.y * StartSample.y) + Z.z;
   
    // Start rasterizing by looping over pixels to output a per-pixel color
    for (unsigned int y = (unsigned int)TriBoundBox.m_MinY; y < TriBoundBox.m_MaxY; y++)
    {
        float EdgeRes0 = EdgeFirst0; // EdgeFirst will be incremented by y values
        float EdgeRes1 = EdgeFirst1;
        float EdgeRes2 = EdgeFirst2;

        float oneOverW = oneOverWFirst;
        float zOverW = zOverWFirst;

        // Once the edge function shows that the scan has exited the triangles then the line can be quit
        bool x_inside = 0;
        for (unsigned int x = (unsigned int)TriBoundBox.m_MinX; x < TriBoundBox.m_MaxX; x++)
        {
            //pixels_scanned++; // How many pixels in all of the bounding boxes
            // sample for the edge function at every pixel
            Vec3f sample = { x + 0.5f, y + 0.5f, 1.0f };

            // In simplest implementation the edge functions would be evaluated here for every pixel
            // but as they are linear functions it is more efficient to increment by pre-calculated
            // values for each x and y step
            // The result is slightly different which isn't surprising with floating point math


            // Edge function incremented at end of both x and y pixel loops
            // These checks in function is shown as a major impact in the profiler
            // So give opportunities to not take the test, reduces load from 12% to 8 %
            // The progressive 'if' is faster than doing all 3 everytime

            bool inside0 = 0, inside1 = 0, inside2 = 0;

            if ( (inside0 = CheckEdgeFunction(E0, EdgeRes0)) )
            {
                if ( (inside1 = CheckEdgeFunction(E1, EdgeRes1)) )
                {
                    inside2 = CheckEdgeFunction(E2, EdgeRes2);
                }
            }

            // If sample is "inside" of all three half-spaces bounded by the three edges of the triangle
            // it's 'on' the triangle
            // This test is redundant after the tests above but there is an 'else' dependent on it
            if (inside0 && inside1 && inside2)
                // Perhaps need to add stuff from https://tayfunkayhan.wordpress.com/2019/07/26/chasing-triangles-in-a-tile-based-rasterizer/
            {
                // We are drawing pixels inside now;
                x_inside = 1;

                // w and z are estimated incrementally too which saves multiplication
                // as per the edge function they will be incremented at the end of each loop

                float w = 1 / oneOverW;
                float z = zOverW * w;

                // Previously 1/w was used as a surrogate for depth but that doesn't allow true
                // prespective mapping so true z interpolation added as per 'GoWild.h' sample
                // as this is crucial for correct texture or normal mapping 
                if (z <= depthBuffer[x + y * g_scWidth])
                {
                    // Sensible to only consider texture if depth test passed

                    // Depth test passed; update depth buffer value
                    depthBuffer[x + y * g_scWidth] = z;// oneOverW previously;
               
                    // If the Texture table has a width then the flag will be set and the material is texture mapped
                    if (Texturise)
                    {
                        if (z < TEXTURE_DEPTH_THRESHOLD) // Don't texturise if too far away
                        {
                        // Interpolate texture coordinates
                        float uOverW = abs((PUVS.x * sample.x) + (PUVS.y * sample.y) + PUVS.z);
                        float vOverW = abs((PUVT.x * sample.x) + (PUVT.y * sample.y) + PUVT.z);

                        Vec2f texCoords = Vec2f(uOverW, vOverW) * w; // {u/w, v/w} * w -> {u, v}
                        // Now fetch from the image

                        uint32_t idxS = static_cast<uint32_t>((texCoords.x - static_cast<uint32_t>(texCoords.x)) * layo_ptr->palette[layo_ptr->attributes[idx]].width - 0.5f);
                        uint32_t idxT = static_cast<uint32_t>((texCoords.y - static_cast<uint32_t>(texCoords.y)) * layo_ptr->palette[layo_ptr->attributes[idx]].height - 0.5f);

                        // Flip y over as world and bitmap have opposite y-axes, it isn't expensive in time, less than 1ms per frame
                        // uint32_t image_idx = ((layo_ptr->palette[layo_ptr->attributes[idx]].height - idxT) * layo_ptr->palette[layo_ptr->attributes[idx]].width + idxS);
                        // The flip is not required actually, no bad thing, one fewer operation per pixel!
                        uint32_t image_idx = (idxT * layo_ptr->palette[layo_ptr->attributes[idx]].width + idxS);

                        // Fetch the pointer to the texture image and then get the relevant pixel
                        const uint32_t* this_colour_ptr = layo_ptr->palette[layo_ptr->attributes[idx]].image;
                        this_colour = this_colour_ptr[image_idx];
                        }
                      else
                        {
                        // Read the face colour
                        this_colour = layo_ptr->palette[layo_ptr->attributes[idx]].rgb888; // read the palette
                        }
                    this_colour = spec_shade_pixel (this_colour, surface);
                    }
                    // Send the pixel and its shading
                WritePixel2Fog888(g_scWidth * y + x, this_colour, z);
                } // end of depth check

            } // end of inside check
            else
            {
                if (x_inside)
                    // This is true if last check was inside and now we're outside, with a convex triangle
                    // it indicates that no more edge tests are required on this x scan so break to next y
                {
                    break; // This gives up to 9% reduction when box is applied previously
                }
            } //end of being outside triangle 
            EdgeRes0 += E0.x; // Incremental increase on x axis
            EdgeRes1 += E1.x;
            EdgeRes2 += E2.x;

            oneOverW += C.x; // Incremental increase of barycentric coordinates on x axis
            zOverW += Z.x;
        } // end of x pixel scan
        EdgeFirst0 += E0.y; // Incremental increase on y axis
        EdgeFirst1 += E1.y;
        EdgeFirst2 += E2.y;

        oneOverWFirst += C.y; // Incremental increase of barycentric coordinates on y axis
        zOverWFirst += Z.y;
    } // end of y pixel scan
//    return(pixels_done); // Return how much was shown
} // End of RasteriseBox

// ************************************************************************************************
// Rasterises a primitive triangle using passed struct WITHOUT edge checking as it's 
// only called for TA (totally accepted) tiles, it does interpolate z and UV mapping
void NotRasteriseBox(const TriToRaster & tri)
{
// Rasterise without edge checking as tile is 'Trivial Accept', but otherwise as RasteriseBox
    //static const char *TAG = "NotRasteriseBox";
    const uint32_t idx = tri.idx;
    const Rect2D TriBoundBox = tri.BoBox;
    const Matrix33f invM = tri.invM;
    bool Texturise = false; // Is the box valid for a texture, a size test was trialled but at 128x128px some triangles are only 1 px!
    uint32_t this_colour = 0; // This will be filled with face or texture colour

    const WorldLayout* layo_ptr = tri.layout; // Find the layout structure from the ptr passed to here

    // M determinant and inverse used to be calculated here but better to pass invM in and backface cull in CheckTriangles
    // Likewise C and Z fetched rather than calcuated
    const Vec3f C = tri.C;    // Constant function to interpolate 1/w
    const Vec3f Z = tri.Z;    // Fetch z interpolation vector

    const Shade_params surface = tri.face_brightness;

    // This has been pulled out of the pixel loop as it is sufficient to do once per box (or actually per triangle) 
    Vec3f PUVS, PUVT; // They have to be declared is the later IF doesn't know if they are needed!
    if (layo_ptr->palette[layo_ptr->attributes[idx]].width && (TriBoundBox.m_MaxX-TriBoundBox.m_MinX)>3 && (TriBoundBox.m_MaxY-TriBoundBox.m_MinY)>3)
    {
        // Calculate UV interpolation vector
        invM.multVecMatrix(Vec3f(layo_ptr->vts[layo_ptr->texel_verts[idx * 3 + 0]].x, layo_ptr->vts[layo_ptr->texel_verts[idx * 3 + 1]].x, layo_ptr->vts[layo_ptr->texel_verts[idx * 3 + 2]].x), PUVS);
        invM.multVecMatrix(Vec3f(layo_ptr->vts[layo_ptr->texel_verts[idx * 3 + 0]].y, layo_ptr->vts[layo_ptr->texel_verts[idx * 3 + 1]].y, layo_ptr->vts[layo_ptr->texel_verts[idx * 3 + 2]].y), PUVT);

        //diffuse = MakeShade(idx,layo_ptr);

        Texturise=true;
    }
    else
    {
        // Read the face colour from the palette which will be used for untextured primitives
        this_colour = layo_ptr->palette[layo_ptr->attributes[idx]].rgb888;
        // Adjust primitve colur based on surface diffuse and psecular components
        this_colour = spec_shade_pixel (this_colour, surface);
    }

    //if (layo_ptr->attributes[idx] > 20) ESP_LOGI(TAG, "Attribute is %x",layo_ptr->attributes[idx]);

    // sample for UV mapping at the first pixel for this rectangle
    Vec3f StartSample = { (unsigned int)TriBoundBox.m_MinX + 0.5f, (unsigned int)TriBoundBox.m_MinY + 0.5f, 1.0f };

    // w and z can be estimated incrementally too which saves multiplication
    float oneOverWFirst = (C.x * StartSample.x) + (C.y * StartSample.y) + C.z;

    // Interpolate z that will be used for depth test
    float zOverWFirst = (Z.x * StartSample.x) + (Z.y * StartSample.y) + Z.z;

    // An incremented x and y was tried but made no speed difference
    // but it was harder to read code so removed

    for (unsigned int y = (unsigned int)TriBoundBox.m_MinY; y < TriBoundBox.m_MaxY; y++)
    {
        float oneOverW = oneOverWFirst;
        float zOverW = zOverWFirst;

        for (unsigned int x = (unsigned int)TriBoundBox.m_MinX; x < TriBoundBox.m_MaxX; x++)
        {
            //pixels_scanned++; // How many pixels in all of the bounding boxes
            // sample for interpolation at every pixel
            Vec3f sample = { x + 0.5f, y + 0.5f, 1.0f };

            // w and z are estimated incrementally too which saves multiplication
            // as per the edge function they will be incremented at the end of each loop

            float w = 1 / oneOverW;
            float z = zOverW * w;

            if (z <= depthBuffer[x + y * g_scWidth])
            {
                // Sensible to only consider texture if depth test passed

                // Depth test passed; update depth buffer value
                depthBuffer[x + y * g_scWidth] = z;

                // Starting on Texture
                // If the Texture table has a width then the material is texture mapped
                if (Texturise)
                {
                  if (z<TEXTURE_DEPTH_THRESHOLD) // Don't texturise if too far away
                  // Not done as an AND so non-textured primitives can be coloured outside the loop
                    {
                    // Interpolate texture coordinates
                    float uOverW = abs((PUVS.x * sample.x) + (PUVS.y * sample.y) + PUVS.z);
                    float vOverW = abs((PUVT.x * sample.x) + (PUVT.y * sample.y) + PUVT.z);

                    Vec2f texCoords = Vec2f(uOverW, vOverW) * w; // {u/w, v/w} * w -> {u, v}
                    // Now fetch from the image
                    // Simplifed from 64 bit, can't see a point in this!
                    // uint32_t idxS = static_cast<uint32_t>((texCoords.x - static_cast<int64_t>(texCoords.x)) * layo_ptr->palette[layo_ptr->attributes[idx]].width - 0.5f);
                    // uint32_t idxT = static_cast<uint32_t>((texCoords.y - static_cast<int64_t>(texCoords.y)) * layo_ptr->palette[layo_ptr->attributes[idx]].height - 0.5f);

                    uint32_t idxS = static_cast<uint32_t>((texCoords.x - static_cast<uint32_t>(texCoords.x)) * layo_ptr->palette[layo_ptr->attributes[idx]].width - 0.5f);
                    uint32_t idxT = static_cast<uint32_t>((texCoords.y - static_cast<uint32_t>(texCoords.y)) * layo_ptr->palette[layo_ptr->attributes[idx]].height - 0.5f);


                    uint32_t image_idx = (idxT * layo_ptr->palette[layo_ptr->attributes[idx]].width + idxS);

                    // Fetch the pointer to the texture image and then get the relevant pixel
                    const uint32_t* this_colour_ptr = layo_ptr->palette[layo_ptr->attributes[idx]].image;
                    this_colour = this_colour_ptr[image_idx];
                    }
                else
                    {
                    // Read the face colour for distant textured primitive
                    this_colour = layo_ptr->palette[layo_ptr->attributes[idx]].rgb888; // read the palette
                    }
                this_colour = spec_shade_pixel (this_colour, surface);
                }
                //Put the pixel into a 16 bit sprite buffer, using previous shading and z for fog
                WritePixel2Fog888(g_scWidth * y + x, this_colour, z);
            } // end of depth check
            oneOverW += C.x; // Incremental increase of barycentric coordinates on x axis
            zOverW += Z.x;
        } // end of x pixel scan
        oneOverWFirst += C.y; // Incremental increase of barycentric coordinates on y axis
        zOverWFirst += Z.y;
    } // end of y pixel scan
} // End of NotRasteriseBox

// ************************************************************************************************
// Function to write pixels to a buffer, mixes the rgb with fog based on depth
// No merit being in IRAM

void WritePixel2Fog888(const uint32_t frame_index, const uint32_t rgb888, const float depth)
// Takes rgb in 888 format, which has already been adjusted by the shade
// Mixes with the fog and finally converts to rgb565

// One may consider that textures and palettes should be in 565 format but
// although it would save space in memory would it be any faster as it would
// still need unpacking for shading and then repacking?

// It was tried with a struct that contained seperate rgb to save the unpacking and packing between
// functions but there was no apparent speed improvement and it's less adaptable 
{
    // The background colour for clearing screen which will also be fog
    extern const uint32_t fog;
    
    // These would be nice as constexpr but can't make it work
    // Static makes it VERY slow!!
    const uint32_t fog_red = (fog & 0x00ff0000) >> 16;
    const uint32_t fog_green = (fog & 0x0000ff00) >> 8;
    const uint32_t fog_blue = (fog & 0x000000ff);

    // There may be vector math optimisation to make here but previous attempts not encouraging
    const uint32_t pix_red = (rgb888 & 0x00ff0000) >> 16;
    const uint32_t pix_green = (rgb888 & 0x0000ff00) >> 8;
    const uint32_t pix_blue = (rgb888 & 0x000000ff) >> 0;

    uint32_t fog_depth = (uint32_t) (255.f * FogFunction(depth));

    // intmix is for integers and has 'a' of 0 to 255, returns with value <<8
    const uint32_t fogged_red = intmix(fog_red , pix_red , fog_depth);
    const uint32_t fogged_green = intmix(fog_green , pix_green , fog_depth);
    const uint32_t fogged_blue = intmix(fog_blue , pix_blue , fog_depth);
    
    // Shift to divide by 'a' in intmix
    uint16_t rgb565 = ((fogged_red) & 0b1111100000000000) | ((fogged_green >> 5) & 0b0000011111100000) | ((fogged_blue >> 11) & 0b0000000000011111);
    
    // Using ESP-IDF the DMA routine will do the byte swap so here can be standard pack to 565
    // We have a pixel in 565 format so send it to the appropriate viewer
    frame_buffer_this[frame_index] = rgb565;
} // end of WritePixel2Fog888

// adjusts input rgb according to surface shade for simple specular and diffuse illumination
// ideally worked on a face level for non-textured primitives
//uint32_t spec_shade_pixel (const uint32_t rgb888, const Vec2f surface_shade)
uint32_t spec_shade_pixel (const uint32_t rgb888, const Shade_params surface_shade)
{
    const uint32_t intShade = surface_shade.lamb;
    const uint32_t intSpecular = surface_shade.spec; // Adds white/grey rather than an incident light colour

    // I'm not sure if these values can overflow??
    // Assumes that the reflected light is white so equal addition to each rgb channel

    /*
    const uint32_t pix_red = std::min(((intShade * (rgb888 & 0x00ff0000)) >> 24) + intSpecular, (uint32_t) 0xff);
    const uint32_t pix_green = std::min(((intShade * (rgb888 & 0x0000ff00)) >> 16) + intSpecular, (uint32_t) 0xff);
    const uint32_t pix_blue = std::min(((intShade * (rgb888 & 0x000000ff)) >> 8) + intSpecular, (uint32_t) 0xff);
    */
    const uint32_t pix_red = ((intShade * (rgb888 & 0x00ff0000)) >> 24) + intSpecular;
    const uint32_t pix_green = ((intShade * (rgb888 & 0x0000ff00)) >> 16) + intSpecular;
    const uint32_t pix_blue = ((intShade * (rgb888 & 0x000000ff)) >> 8) + intSpecular;

    const uint32_t temp_rgb888 = (pix_red << 16) | (pix_green << 8) | (pix_blue);

    return (temp_rgb888);
} // End of spec_shade_pixel

// Although the fpu is great at fp multiple there are issues on casting to and from floats
// as that is complex with many considerations
// 'a' is in the range 0 to 255
// inline is worth a few ms per frame here
inline uint32_t intmix(const uint32_t x, const uint32_t y, const uint32_t a)
{
    return (x * (255-a) + y * a);
}

// Linear fog presently, calculated live
float FogFunction(float const depth)
{
    constexpr float start = 5.0f;
    constexpr float end = 30.0f;
    constexpr float divisor = 1/(end-start); // Known at compile time, a division avoided

    float fog_temp = (end-depth) * divisor;
    return (std::clamp(fog_temp , 0.f , 1.f));
}

// Clear frame buffer to fog background colour
void ClearWorldFrame(uint16_t * frame_buffer)
{
extern const uint16_t BackgroundColour; // = ((fog >> 8) & 0b1111100000000000) | ((fog >> 5) & 0b0000011111100000) | ((fog >> 3) & 0b0000000000011111);

    // memset() is char-based, could make this uint32_t perhaps? 
    for (int i=0; i < g_scWidth * g_scHeight; i++)
    {
        frame_buffer[i] = BackgroundColour; // Background colour
    }
} // End of ClearWorldFrame