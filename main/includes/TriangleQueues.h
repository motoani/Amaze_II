#pragma once
#include <stdint.h>
#include "geometry.h"
#include "structures.h"

void rasteriseTask(void * parameter);

void MakeQueue(const uint32_t tri_count, const uint32_t block);

void EmptyQueue(const uint32_t block);

void EmptyQueues();

uint32_t QueueTriangle(const TriToRaster triangle, const uint32_t block);

void SendQueue(const uint32_t block);

bool SendImpactQueue(const uint32_t block, Near_pix * to_test);

