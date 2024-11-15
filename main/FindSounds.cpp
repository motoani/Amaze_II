// Scan a 'sounds' partition for .wav files and make them available for I2S player
#include "stdint.h"
#include <vector>

#include "esp_log.h"

#include "structures.h"

#include "FindSounds.h"

// Compares contents pointed by source to the passed magic number vector
// and returns true if the magic matches exactly throughout its length, else false
// Not aware of the structure of a header / magic number, simply compares bytes
bool CheckMagic(const uint8_t * source,  const std::vector<uint8_t> & magic)
{
    bool matches = true; // Assume a match but set to false on mismatch and quit
    // Comparison is bytewise in all cases
    auto i = 0; // To offset into the source
    // Traverse the elements, no need to know the length
    for (uint8_t magic_byte : magic)
    {
        if (magic_byte != 0xFF) // Skip pad
        {
            if (magic_byte != source[i])
            {
                matches = false;
                break; // Leave on first mismatch
            }
        }
        i++; // Move to the next source test byte
    }
    return (matches);
} // End of CheckMagic

std::vector <Dot_wav *> sounds; // A vector so it can be dynamically built and checked later

// Input a pointer to the mapped sound partition and scan that for valid .wav files
// Build an array of the buffer locations and their lengths
void FindSounds(const void *sound_map_ptr)
{
    extern std::vector <Dot_wav *> sounds; // Sound resume will be placed here
    static const char *TAG = "FindSounds";

    Dot_wav *this_wav; // Use a structure to find features of the header
    // Set the wav structure to be located at current pointer position
    this_wav = (Dot_wav *)sound_map_ptr; // Make a local copy that we can adjust

    while (CheckMagic((uint8_t *) this_wav,wav_magic)) // Does the pointer really point to a .wav file?
    {
        // Process this wav file
        ESP_LOGI(TAG,"this_wav is %p ",this_wav);
        ESP_LOGI(TAG, "Sample is at %p",&(this_wav->SampledData[0]));

        // Push a ptr to the wav headerthe structure that's made into the vector for use in player
        sounds.push_back(this_wav);

        // Step to next header, which _may_ be present, managing byte offsets
        // Unless the sounds EXACTLY fill the partition it should be safe to step
        // after last wav as the final byte has to be played
        this_wav = (Dot_wav *)((uintptr_t)this_wav + (this_wav->FileSize + 8));

        // Quits on first mismatch so may return fewer wavs than player expects
    }
} // End of FindSounds
