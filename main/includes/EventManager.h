#pragma once

// An event code is uint32_t and encoded from the Blender material name by the 
// node.js script into the palette.
// The intention is that a material is ascribed to a set of faces that behave 
// in a defined way collectively. Note though that the bit pattern is not
// granular enough to allow every permutation unambiguously.
// One event can push another into the queue although care is needed
// for example, after faes are deleted they cannot be re-referenced by a code 
// The event codes are global across the world, which can have benefits and drawbacks!
// As is traditional, codes are OR'd to generate complex actions.
// Format of the word is:
// 0xTEFSSNNXX
//   ||||||-- - identification number used by world designer
//   ||||--   - one byte number in twos complement to action things
//   |||-     - three nibbles for an action's subtype organised as Energy, Faces, TBC
//   -        - one nibble for an action
#define EVNT_NN_MASK    0x0000ff00  // Mask to present the operand
#define EVNT_NN_SHIFT   8           // Bit shift required to move operand to LSB

#define EVNT_ENERGY     0x10000000
    #define EE_DISPLAY  0x01000000  // Sends the energy bar display via DMA
    #define EE_CHANGE   0x02000000  // Alters the energy level as per NN
    #define EE_SCORE    0x04000000  // Activates display of an energy award graphic as per NN 
    #define EE_SET      0x08000000  // Set the energy level as per NN

#define EVNT_FACES      0x20000000
    #define EF_DELETE   0x00100000  // Delete all faces with the exact same event code

#include <stdint.h>
#include "structures.h"

void GetGameEvent(void * parameter);

void EvntDeleteFaces(Near_pix this_event_pix);

void EvntHealthBar(const uint16_t health);

void OverlayTwoD(TwoD_overlay & this_overlay);

void StopOverlayTwoD(TimerHandle_t tracked_handle);

void MakeNumber(uint16_t font_index, uint16_t score, TwoD_overlay & this_overlay);


