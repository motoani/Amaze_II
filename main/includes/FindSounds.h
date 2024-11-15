#pragma once

#include <stdint.h>
#include "geometry.h"
#include "structures.h"

#include <vector>

// A string for checking presence of a valid .wav header
// This includes confirming mono channel of 16000hz sampling
// 0xFF is NOT compared for value but acts as a positional pad
// 0x00 cannot be the pad as it's needed in small numbers - eg channels
const std::vector <uint8_t> wav_magic {
    'R', 'I', 'F', 'F',
    0xFF, 0xFF, 0xFF, 0xFF,
    'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 
    0xFF, 0xFF, 0xFF, 0xFF,
    0x01, 0x00, 0x01, 0x00, // Single channel of PCM
    0x80, 0x3e, 0x00, 0x00, // 16000Hz
    0x00, 0x7D, 0x00, 0x00, 0x02,  0x00, 0x10, 0x00, // Through to 16bit
    'd', 'a', 't', 'a'
    };

bool CheckMagic(const uint8_t * source,  const std::vector<uint8_t> & magic);

void FindSounds(const void *sound_map_ptr);
