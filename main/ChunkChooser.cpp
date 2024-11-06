// It seemed a good idea to use an orthographic projection to build a 2d map of the world
// which could be used for chunking. BUT triangles that are 'edge-on' are invisible in the edge-detection
// and thus not placed in a chunk, so abandoned as an approach.
// 
// This simple routine allows the 360 degree view to be divided into sectors and then a series of chunks tested for rendering
// The search array is readily altered for more or fewer sectors and numbers of chunks to be worked through
//
// Some of the functions are very small so it feels as though there might be an excessive overhead 
// passing parameters, although rasteriser mostly likely the bottleneck

#include <stdint.h>
#include <math.h>
#include "esp_log.h"
//static const char *TAG = "ChunkChooser";


#include "ChunkChooser.h"
#include "globals.h"
//#include "CameraWork.h"
//#include "RasteriseBox.h"
#include "geometry.h"


// Check if the chunk is in range of the model and return false if not in the defined model
bool test_chunk(Vec2i chunk, const ChunkArr& chunk_param)
{
	if (chunk.x < 0 || chunk.x>(chunk_param.xcount - 1)) return false; // range check x
	if (chunk.y < 0 || chunk.y>(chunk_param.zcount - 1)) return false; // range check z

	return (true);
}

// Calculate chunk coordinate from eye position
// this is separated from test so scanning can occur from invalid chunks
// also passing the chunk parameters so can swap world layouts
Vec2i find_chunk(const Vec3f location, const ChunkArr& chunk_param)
{
	Vec2i this_chunk;

	this_chunk.x = (int)floor((location.x - chunk_param.xmin) / chunk_param.size);

	// Don't forget that z is the other axis in the horizontal world plane
	this_chunk.y = (int)floor((location.z - chunk_param.zmin) / chunk_param.size);

	return (this_chunk);
}

// Return the chunk's index in the model's array from coordinates of chunk
uint32_t chunk_index(const Vec2i chunk, const ChunkArr& chunk_param)
	{
	return (chunk.x + chunk_param.xcount * chunk.y);
}

// An array of offsets to use to pick the next chunk for display
const uint32_t test_zones = 32; // The number of chunks listed in decreasing priority
const uint32_t test_sectors = 8; // The sectors into which a 2D horizontal circle is divided

const Vec2i ChunkSequence[test_sectors][test_zones] = {
     { {0,0},{0,1},{-1,1},{-1,0},{1,0},{0,2},{-1,2},{1,1},{-2,1},{-2,0},{1,2},{-2,2},{2,3},{-3,1},{0,3},{1,3},{-1,3},{2,3},{-2,3},{-3,2},{0,4},{-1,4},{1,4},{-2,4},{2,4},{-3,3},{-4,2},{3,4},{-3,4},{-4,3},{0,5},{-1,5} }, // Sector 0
     { {0,0},{-1,0},{-1,1},{0,1},{0,-1},{-2,0},{-2,1},{-1,-1},{-1,2},{0,2},{-2,-1},{-2,2},{-3,-2},{-1,3},{-3,0},{-3,-1},{-3,1},{-3,-2},{-3,2},{-2,3},{-4,0},{-4,1},{-4,-1},{-4,2},{-4,-2},{-3,3},{-2,4},{-4,-3},{-4,3},{-3,4},{-5,0},{-5,1} }, // Sector 1
     { {0,0},{-1,0},{-1,-1},{0,-1},{0,1},{-2,0},{-2,-1},{-1,1},{-1,-2},{0,-2},{-2,1},{-2,-2},{-3,2},{-1,-3},{-3,0},{-3,1},{-3,-1},{-3,2},{-3,-2},{-2,-3},{-4,0},{-4,-1},{-4,1},{-4,-2},{-4,2},{-3,-3},{-2,-4},{-4,3},{-4,-3},{-3,-4},{-5,0},{-5,-1} }, // Sector 2
     { {0,0},{0,-1},{-1,-1},{-1,0},{1,0},{0,-2},{-1,-2},{1,-1},{-2,-1},{-2,0},{1,-2},{-2,-2},{2,-3},{-3,-1},{0,-3},{1,-3},{-1,-3},{2,-3},{-2,-3},{-3,-2},{0,-4},{-1,-4},{1,-4},{-2,-4},{2,-4},{-3,-3},{-4,-2},{3,-4},{-3,-4},{-4,-3},{0,-5},{-1,-5} }, // Sector 3
     { {0,0},{0,-1},{1,-1},{1,0},{-1,0},{0,-2},{1,-2},{-1,-1},{2,-1},{2,0},{-1,-2},{2,-2},{-2,-3},{3,-1},{0,-3},{-1,-3},{1,-3},{-2,-3},{2,-3},{3,-2},{0,-4},{1,-4},{-1,-4},{2,-4},{-2,-4},{3,-3},{4,-2},{-3,-4},{3,-4},{4,-3},{0,-5},{1,-5} }, // Sector 4
     { {0,0},{1,0},{1,-1},{0,-1},{0,1},{2,0},{2,-1},{1,1},{1,-2},{0,-2},{2,1},{2,-2},{3,2},{1,-3},{3,0},{3,1},{3,-1},{3,2},{3,-2},{2,-3},{4,0},{4,-1},{4,1},{4,-2},{4,2},{3,-3},{2,-4},{4,3},{4,-3},{3,-4},{5,0},{5,-1} }, // Sector 5
     { {0,0},{1,0},{1,1},{0,1},{0,-1},{2,0},{2,1},{1,-1},{1,2},{0,2},{2,-1},{2,2},{3,-2},{1,3},{3,0},{3,-1},{3,1},{3,-2},{3,2},{2,3},{4,0},{4,1},{4,-1},{4,2},{4,-2},{3,3},{2,4},{4,-3},{4,3},{3,4},{5,0},{5,1} }, // Sector 6
     { {0,0},{0,1},{1,1},{1,0},{-1,0},{0,2},{1,2},{-1,1},{2,1},{2,0},{-1,2},{2,2},{-2,3},{3,1},{0,3},{-1,3},{1,3},{-2,3},{2,3},{3,2},{0,4},{1,4},{-1,4},{2,4},{-2,4},{3,3},{4,2},{-3,4},{3,4},{4,3},{0,5},{1,5} } // Sector 7
};


// Return a chunk index using eye and direction Vec3f with progressive
// scanning around through the array from the current chunk location
// Sending the layout too as we will be overlaying models
uint32_t ChunkChooser(const Vec3f eye, Vec3f direction, const bool eye_moved, const WorldLayout* layo_ptr)
{
// direction can't be const as we zero y component in this function

	const Vec2f axis = { 0,-1 }; // The reference axis, hopefully calculated in at compile as it's const

	// These variables to persist as the direction vector stays the same between calls until 'moved'
	// The calculations aren't massive but why do them 8 times extra?
	static int32_t search_step = -1; // Keep track of which chunk we will return to
	static Vec2i this_chunk;
	static Vec3f ch_dir;
	static float det, dot,pointing;
	static uint32_t pointing_index;

	// if we have moved then restart the choosing process
	// using static old values gave false movement presumably due to rounding issues
	// so it is now a specific flag
	if (eye_moved)
	{
		search_step = -1;
		// Find the chunk that we sit in with result into passed variables
		// and kept for each call to save time
		this_chunk = find_chunk(eye,layo_ptr->ChAr);

		direction.y = 0; // Ignore 3rd dimension of y and then normalise the  way we're looking
		ch_dir = direction.normalize();

		// As atan2 gives result -PI to +PI when we add PI the sequence around the dial is simply 0 to 2PI!
		dot = axis.x * ch_dir.x + axis.y * ch_dir.z; // Note it's z in Vec3f, OK to be zero
		det = ch_dir.z * axis.x - ch_dir.x * axis.y;

		// angle is atan2 (determinant,dot product) plus PI
        constexpr float pointing_factor = test_sectors / (2 * M_PI);
		pointing = (atan2(det, dot) + M_PI) * pointing_factor; // divide into the sectors
		// A bitwise mask was used in the next line to avoid a divide, but not generalisable and it's only done once per frame
		pointing_index = ((uint32_t)floor(pointing)) % test_sectors; // Ensure no overflow at 2 * PI
	} // end of if (eye_moved)

	if (search_step++ == test_zones)
	{
		return(LAST_CHUNK); // All options have been used now so caller won't come back
	}
	Vec2i this_offset = ChunkSequence[pointing_index][search_step];
	Vec2i new_chunk = this_chunk + this_offset;

	uint32_t chosen_chunk;
	
	if (test_chunk(new_chunk,layo_ptr->ChAr))
	{
		chosen_chunk= chunk_index(new_chunk,layo_ptr->ChAr); // The chunk is in range so send that back
	}
	else chosen_chunk = INVALID_CHUNK; // Return that the chunk is out of range and then it won't be rendered

	return(chosen_chunk); // return a valid chunk or an error for each chunk derived from the search array
}

// Return a chunk index using eye and direction Vec3f with progressive
// scanning around through the array from the current chunk location
// Sending the layout too as we will be overlaying models
// Indexed doesn't needed a static index nor a 'reset' flag
uint32_t IndexChunkChooser(const Vec3f eye, Vec3f direction, uint32_t index, const WorldLayout* layo_ptr)
{
// direction can't be const as we zero y component in this function

	const Vec2f axis = { 0,-1 }; // The reference axis, hopefully calculated in at compile as it's const

	// These variables to persist as the direction vector stays the same between calls until 'moved'
	// The calculations aren't massive but why do them 8 times extra?
	Vec2i this_chunk;
	Vec3f ch_dir;
	float det, dot,pointing;
	uint32_t pointing_index;

	if (index == test_zones)
	{
		return(LAST_CHUNK); // All options have been used now so caller won't come back
	}
		// Find the chunk that we sit in with result into passed variables
		// and kept for each call to save time
		this_chunk = find_chunk(eye,layo_ptr->ChAr);

		direction.y = 0; // Ignore 3rd dimension of y and then normalise the  way we're looking
		ch_dir = direction.normalize();

		// As atan2 gives result -PI to +PI when we add PI the sequence around the dial is simply 0 to 2PI!
		dot = axis.x * ch_dir.x + axis.y * ch_dir.z; // Note it's z in Vec3f, OK to be zero
		det = ch_dir.z * axis.x - ch_dir.x * axis.y;

		// angle is atan2 (determinant,dot product) plus PI
        constexpr float pointing_factor = test_sectors / (2 * M_PI);
		pointing = (atan2(det, dot) + M_PI) * pointing_factor; // divide into the sectors
		// A bitwise mask was used in the next line to avoid a divide, but not generalisable and it's only done once per frame
		pointing_index = ((uint32_t)floor(pointing)) % test_sectors; // Ensure no overflow at 2 * PI

	Vec2i this_offset = ChunkSequence[pointing_index][index];
	Vec2i new_chunk = this_chunk + this_offset;

	uint32_t chosen_chunk;
	
	if (test_chunk(new_chunk,layo_ptr->ChAr))
	{
		chosen_chunk= chunk_index(new_chunk,layo_ptr->ChAr); // The chunk is in range so send that back
	}
	else chosen_chunk = INVALID_CHUNK; // Return that the chunk is out of range and then it won't be rendered

	return(chosen_chunk); // return a valid chunk or an error for each chunk derived from the search array
}
