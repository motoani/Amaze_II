#pragma once

#include <stdint.h>
#include "geometry.h"
#include "structures.h"

// Various flags returned by the chunk chooser functions
#define INVALID_CHUNK 0xffffffff
#define LAST_CHUNK 0xfffffffe

bool test_chunk(Vec2i chunk, const ChunkArr& chunk_param);

Vec2i find_chunk(const Vec3f location, const ChunkArr& chunk_param);

uint32_t chunk_index(const Vec2i chunk, const ChunkArr& chunk_param);

uint32_t ChunkChooser(const Vec3f eye, Vec3f direction, const bool eye_moved, const WorldLayout* layo_ptr);

uint32_t IndexChunkChooser(const Vec3f eye, Vec3f direction, uint32_t index, const WorldLayout* layo_ptr);
