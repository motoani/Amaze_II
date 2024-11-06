# Amaze II by motoani 2023 to 2024

The aim of the game is to live as long as possible whilst exploring the world.

## Design and acknowledgements

The code here is built on the algorithms presented in:
- https://tayfunkayhan.wordpress.com/2018/11/24/rasterization-in-one-weekend/
- https://www.scratchapixel.com/lessons/3d-basic-rendering/rendering-3d-scene-overview/computer-discrete-raster.html

with inpsiration from RP2040 projects:
- https://ece4760.github.io/Projects/Fall2023/av522_dy245/index.html
- https://github.com/bernhardstrobl/Pico3D

Note that I don't consider this project to be a port of any of the above. The vector type definitions (geometry.h) have been modified and are combined with homogenous matrix transformations specifically for this project. Also the architecture of the ESP32S3 and its RTOS has underpinned the design in a way that would be absolutely inappropriate for the RP2040 or for effective implementation on x64.

A design principle is that a world can be built for a new game without modification of the code thus giving creatives the opportunity to engage in design without needing to change the application code.

Presently, true NPCs are not included although a simple animated NPC could be built to have limited interaction with the player.

### Credits

During the project I've used fonts and textures from various free-for-non-commerical sites and list some here. If your work is included and overlooked, I apologise, please let me know and I'll acknowledge or remove as required.

- Nesto Beryl font: https://www.fontspace.com/nesto-beryl42-font-f86660
- Wood panel texture : https://www.deviantart.com/devin-busha/art/Wood-Study-3-147242468
- Rug: https://www.pennymorrison.com/products/multi-roman-woven-rug
 a site that sells real rugs!
- Bookcase: https://pngtree.com/freepng/illustrated-bookcase-sticker-vector_11083205.html

## How it works

The code is well commented but here's a narrative of waht is going on in the program.

### Tasks and cores

There are two main tasks each allocated to an ESP32 core. Core 0 identifies which triangles should be projected from 'chunks', does the projection and places triangles and tiles onto queues. Simultaneously Core 1 rasterises the queues into ping-pong frame buffers. 

Small RTOS tasks are also running for managing gameplay events and once per second information. These should not use floating point operations so as not to require additional register saves on task switching.

Presently the ESP-IDF API for DMA is used to send parallel data to the Lilygo T-display TFT unit without the use of a library overlay. At present the program will NOT work on SPI-interfaced displays without modification of the code.

### Partitions

ESP32 partitions are setup for code (1MB), 3d world (4MB) and texture bitmaps (1MB) respectively so that each can be updated independently. Of course, major code revisions may require a different world format, and vice versa.

Whilst the ESP32S3 MMU hardware transparently caches SPI flash ROM and PSRAM, off-cache accesses are inherently slower so more complex worlds are likely to lead to reduced framerates despite the chunking system.

### Textures

Textures are loaded from a .bin as concatenated DIB images of 32 bit depth. In theory, there is no size restriction on textures but it is recommended that images are a power of 2 pixels wide and, due to the low display resolution, there is little merit in images larger than 128x128 (64k). Textures are far more complex to rasterise than block colours. Ideally, design the world to use block colour shapes supplemented by textures for detailed colour.  

The texture name as it appears in the world .mtl file and offset within the .bin must be recorded in the world converter textures.js file manually so they can be linked into the palette. 

### 3D world

World making has only been tested in Blender but this is not essential so long as the relevant .obj and .mtl files are generated. These files are processed by a node.js script into a .bin file for upload into a the world ESP32 partition. The data in the partition is processed by the ESP when the application is started after the boot sequence. Some tables are copied to SPIRAM and offsets are 'linked' to suit the mapping of the partition. This allows partitions to be resized.

The world is broken into chunks, currently defined as 10m squares and ideally all triangles should be constrained to a single chunk. Large objects should be split into sections to achieve this. When primitives cross chunks they are allocated to the chunk containing their centroid. This may result in a nearby feature not being rendered if its centroid is in a distant chunk. Chunks are sent for rendering starting from the viewer's position and going progresively more distant and peripheral according to a predefined sequence. The depth of sequence for rendering is adjusted to maintain the framerate at 10fps or better. When the viewer is not moving and thus the view is stable, the full sequence is sent so that even distant objects are rendered. Consider the complexity of each chunk when building the world. 

The world contains multiple layers. The first layer is considered to be the 'base' and its heights are used to adjust the player's vertical position. It is possible to have caves and bridges so long as the viewer can fit underneath the higher level (around 2m). Another layer is sensibly used for objects that the player will move between, rather than over. Animations can be exported from Blender, the first frame should be 000 and any .obj file that is so named will be assumed to indicate an animation. Animations include chunk, vertex and palette data which consumes plentiful memory. This approach allows objects to change markedly between frames with little code overhead. It is suggested that animations have fewer than 20 frames. The animation frames are chosen through a pointer system so switiching is low-overhead but is likely to require cache updates and thus slow overall frame rates. It is possible to have many layers of world and animations but tests have shown that this gives slower framerates than condensing them, presumably due to caching misses.

### Game play events

Game play events are managed via a queue and are based on a 32bit code being associated with a material in the palette. They are therefore set up when building the world obj/mtl files in Blender. The code word is preceded by EVENT0x in the material name and has a bit pattern as defined in EventManager.h Time, via a one second task, movement, and impacts are the triggers for events.

The current events include:
- Changing player energy and related displays
- Deleting triangles from the world

This small selection allows the player's life to be extended or reduced and objects to be removed on impact. Importantly, removed objects are deleted from the chunk database in PSRAM and so overhead is reduced in future frames.

To a limited degree the options can be OR'd to allow multiple actions per event code.

The system allows some space for extension for other actions and those planned include:
- Activating animations
- Teleport to a new world

## Building a world

Blender has been used to make several worlds and various test environments. Worlds must be exported as .obj files with their associated .mtl information. Use Blender's visibility and 'selection only' features to segregate which structures are exported into particular files. From initial tests it seems best to set all animations to have the same number of frames and to export all of them together as a single .obj file. Note that the frame period is currently coded at 100ms per frame.

Exported files must be processed into a binary .bin by a node.js script (ObjToBin04) before uploading into the ESP32 flash partition. This will be released on GitHub.
