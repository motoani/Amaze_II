#pragma once

// World parameters and structures
#include <stdint.h>
#include "geometry.h"
#include "globals.h"
#include <vector>

struct Font_numbers // Describes a bitmapped font
{
    uint16_t width;             //Dimensions
    uint16_t height;
    uint16_t locations[11];     // A list of start and end pixel columns
    const uint16_t * buffer;          // Pointer to the actual bitmap
};

struct TwoD_overlay // Describes a buffer to overlay on the 3D viewport
{
    uint16_t width = 0;     // Dimensions of the buffer in use
    uint16_t height = 0;
    uint16_t * buffer;  // Pointer to the memory area
};

struct Time_tracked // Various parameters that are tracked in time and displayed periodically
{
    uint16_t frames;    // Frame since last call, used to indicate fps
    uint16_t health;    // Player's wellbeing 
    uint32_t triangles; // Primitives processed
};

struct Shade_params // Defines the amount of lambertian diffuse and specular relection from a surface
{
    uint32_t lamb;
    uint32_t spec;
};

struct world_partition_header // Defines how the worlds are placed in the partition
{
    const Vec3f eye;          // The observer
    const Vec3f direction;
                        // These values are followed by descriptors and offsets
    const uint32_t world_start; // Use this as a starting memory location
};

struct world_header // The header of a single world in the partition, constants mostly
{
    //Vec3f eye;
    //Vec3f direction;
    //const uint32_t world_descriptor; // This will be used to describe for animation etc
    const uint32_t vertices; // Vec3f*
    const uint32_t nvertices; // uint16_t*
    const uint32_t vts; // Vec2f*
    const uint32_t texels;// uint16_t*
    const uint32_t attributes; // uint16_t*
    const uint32_t thin_palette; // uint32_t*
    const uint32_t chunks; // int16_t*
    //const uint32_t next_world; // world_header*
    //const uint32_t next_frame; // link to the next frame of an animated world
};

struct faceMaterials // To describe live palette as it's built in RAM
{
    uint32_t rgb888; // A shade for block colours and textures, but texture calculates its own and places it here
    uint16_t width; // Not constant as they are set from partition
    uint16_t height;
    const uint32_t * image; // Pointer to a (constant) image array
    uint32_t event; // A word to identify and describe events associated with this palette attribute
};

struct part_faceMaterials // To describe palette in the partition
{
    const uint32_t type; // What type of material is here so how next word will be used
    const uint32_t parameter; // Offset start in the partition or rgb888 or ? in future
    const uint32_t event_code;// An event word to be added here, for all cases or optional?
};

struct ChunkArr // Describes the layout of chunks in an a given layer
{
  int16_t xmin;
  int16_t zmin;
  int16_t xcount;
  int16_t zcount;
  int16_t size; // Of each tile
};

struct ChunkFaces
{
    uint16_t  * faces_ptr; // Pointer to the array of faces for that chunk
    uint32_t face_count; // The length of the array
};

// In theory this box can be integer but I've tried to work this through the algorithm and lost pixels each time
struct Rect2D
{
    float   m_MinX;
    float   m_MinY;
    float   m_MaxX;
    float   m_MaxY;
};

struct WorldLayout
{
  // The constants of number of triangles etc aren't needed as they are fixed via chunks
  // Pointers were constant but now they have to be updated to match partition mapping
  // Animation has been moved out of here
  Vec3f * vertices;         // Pointer to an array of Vec3f vertices - these are all const ptrs to const values
  uint16_t *  nvertices;     // Pointer to an array of indices to the vertex coordinates
  uint16_t * texel_verts;   // Pointer to an array of indices to texture UV coordinates
  Vec2f * vts;              // Pointer to an array of UV coordinates
  faceMaterials  * palette;  // Pointer to an array of faceMaterials that's a palette
  uint16_t * attributes;    // Pointer to an array that selects from the palette per face 
  ChunkFaces * TheChunks;   // Pointer to the array of lists of faces per chunk
  ChunkArr ChAr;            // The arrangement of chunks used in this layout
};

// A structure that stores the pointers to layouts and is set up as the world is parsed from partition
// It will be a ragged array when built as the included vector will vary in length depending on layouts/frames
struct EachLayout
{
    uint16_t frames; // The number of frames in this layout
    //uint16_t current_frame; // Store which frame is to be used for display
    std::vector<WorldLayout> frame_layouts; // A list of world layouts
};

struct Near_pix // To find and report on an impacted face at a pixel
{
    float depth; // z depth found on CheckCollide but also returns from SendImpactQueue
    uint32_t x; // The pixel to be tested
    uint32_t y;
    const WorldLayout* layout; // The layout in which a found face is contained
    uint32_t idx; // Face index
    uint32_t event; // Event code detected
};

// A struct to define how a triangle is rasterised, it's big but no obvious reductions
// to be made as invM used to derive a fair few other parameters but maybe more efficient
// for those to be calculated outside rasteriser in future?
// Especially for tiles there is a degree of inefficiency of passing and re-calculating
// a range of parameters
struct TriToRaster
{
    const WorldLayout* layout;  // A ptr to structure of the world so the queue can have multiple layouts queued for rasterising
                            // Can't pass the struct as the constants aren't defined when this struct is declared
                            // and doing it by pointer probably faster and more transparent anyway.
    uint32_t idx; // Triangle index
    Vec3f clip_zs; // just z value of the 3 clipped vertices passed in this Vec3f
    Rect2D BoBox; // Bounding box
    Matrix33f invM; // Matrix to solve edge equations etc
    Vec3f C; // Constant function (derived from invM) paased to reduce Rasteriser load
    Vec3f Z; // Z interpolation 
    Shade_params face_brightness; // Based on the face and half normals to determine shading
 };

// A struct to keep track of the TriToRaster queues, at least two are needed, one per rasteriser
// and a duplicate of those is needed for multicore implementation, so 4 in total
struct TriQueue
{
    TriToRaster* itemptr;
    uint32_t size;
    uint32_t count;
};
