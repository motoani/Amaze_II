#include <vector>

#include "structures.h"
#include "ParseWorld.h"
#include "esp_log.h" 
#include "esp_heap_caps_init.h"

//extern std::vector<WorldLayout> world;
extern std::vector<EachLayout> world; // An unsized vector of layouts which can contain multiple frames
extern std::vector<WorldLayout> the_layouts; // Global presently but not good idea

// Takes an average of the whole texture to retun an average shade
// As images are gamma adjusted it's not ideal to do a mean but it is sufficient
// Ns is sent from the blender roughness
uint32_t AvgCol(const uint32_t * bitmap_ptr, const uint32_t size, const uint32_t Ns)
{
static const char *TAG = "avgcol";
//ESP_LOGI(TAG,"offset %p",bitmap_ptr);
//ESP_LOG_BUFFER_HEX_LEVEL(TAG, bitmap_ptr, 128, (esp_log_level_t)1); // Dump 128 bytes
    
    // Keep track of the totals, 32 bit is sufficient up to 4k x 4k texture
    uint32_t tot_red = 0,tot_green = 0, tot_blue = 0;
    //uint32_t tot_Ns = 0x80 ; // Default roughness value

    for (uint32_t i = 0; i < size; i++)
    {
        uint32_t pixel = bitmap_ptr[i];
        // get each of the colour values and add it on
        tot_red += (pixel & 0x00ff0000) >> 16;
        tot_green += (pixel & 0x0000ff00) >> 8;
        tot_blue += (pixel & 0x000000ff) >> 0; // Shift for illustration 
    }
    // Do the averages and mask just to be safe
    tot_red = 0xff & (tot_red / size);
    tot_green = 0xff & (tot_green / size);
    tot_blue = 0xff & (tot_blue / size);

    // rebuild the RGB
    //uint32_t rgb_out = (tot_Ns << 24) | (tot_red<<16) | (tot_green<<8) | (tot_blue);
    uint32_t rgb_out = (Ns & 0xff000000) | (tot_red<<16) | (tot_green<<8) | (tot_blue);
    return (rgb_out);
}

// Work through the partition world header to understand what is there
// and build structures that describe each of the worlds
// Enter with a pointer to the partition
void ParseWorld(const void * w_ptr , const void * texture_map_ptr )
{
static const char *TAG = "ParseWorld";

    EachLayout temp_layout;
    uint32_t member;

    world_partition_header * world_partition_header_ptr = (world_partition_header *) w_ptr;
    uint32_t * descriptor_ptr = (uint32_t *) & (world_partition_header_ptr->world_start);

    // Loop until zero, the end marker
    while ((member = * descriptor_ptr)) {
        
        //ESP_LOGI(TAG, "member is %x", (unsigned int) member);
        // The first read member SHOULD be a descriptor
        if (! (member & 0x10000000)) assert ("Error parsing descriptor");

        // It is a descriptor so a fresh layout follows
            
        temp_layout.frame_layouts.clear(); // Clear the vector part of the temp_layout for each new layout
        const uint32_t frames = (member & 0x00ff0000) >> 16; // Extract the frame count
        temp_layout.frames = frames; // Store how many frames are in this layout 

        descriptor_ptr ++ ; // Step to first offset value
            
        // Loop through the frame layouts
        for (unsigned int frame_i = 0 ; frame_i < frames ; frame_i++)
            {
            const uint32_t offset = * descriptor_ptr;
            //ESP_LOGI(TAG,"Offset is %x",(unsigned int)offset);
            //ESP_LOGI(TAG, "looking at %p",(void *)(w_ptr + offset));

            // Interpret a layout and push its structure into a vector of frames
            temp_layout.frame_layouts.push_back(ReadWorld(w_ptr + offset, texture_map_ptr));

            descriptor_ptr++; // Go to the next offset
            }
        // All of the offsets have been read now so push that layout
        world.push_back(temp_layout); // Store the layout we have just read
    } // End of while (member)
}; // End of ParseWorld

// Input a pointer into the partition, read the values, adjust offsets to build a temporary structure
// This is then pushed into a global vector so we don't need to track indicies here
// Using a vector is better C++ practice than malloc and we don't need to know that this is DMA-friendly RAM
WorldLayout ReadWorld(const void * w_ptr , const void * texture_map_ptr)
{
    static const char *TAG = "ReadWorld";

    const part_faceMaterials * temp_thin_palette; // To access the thin palette in the word partition
    const uint8_t * tex_ptr; // Use a byte wise at present
    const uint8_t * w_map_ptr = (uint8_t *) w_ptr; // Cast for use

    // Uses a structure into partition header to save a lot of pointer messing
    WorldLayout temp_world; // A temp_world will be defined to be later pushed into the vector

    // Base address of this world strcture in the partition is indicated by w_map_ptr
    // Make and cast a new pointer (which is at the same location in memory to read nicely
    world_header * world_header_ptr = (world_header *) w_map_ptr;

    temp_world.vertices = (Vec3f *) (w_map_ptr + world_header_ptr->vertices);
    temp_world.nvertices = (uint16_t *) (w_map_ptr + world_header_ptr->nvertices);
    temp_world.vts = (Vec2f *) (w_map_ptr + world_header_ptr->vts);
    temp_world.texel_verts = (uint16_t *) (w_map_ptr + world_header_ptr->texels);
    temp_world.attributes = (uint16_t *) (w_map_ptr + world_header_ptr->attributes);

    ESP_LOGI(TAG,"Attributes are at %p",temp_world.attributes);

    // Find the thin palette in the world binary and build a proper palette in RAM
    ESP_LOGI(TAG,"Palette offset is %p",(void *)world_header_ptr->thin_palette);
    const uint32_t * part_palette_ptr = (uint32_t const *)(w_map_ptr + world_header_ptr->thin_palette);
    const uint32_t pal_nmats = * part_palette_ptr; // Fetch the size of the thin palette in the partition
    temp_thin_palette = (const part_faceMaterials *) (part_palette_ptr + 1); // Start a structured thin palette

    ESP_LOGI(TAG,"Working on a palette with %d entries",(int)pal_nmats);
    faceMaterials * world_palette_ptr = (faceMaterials *) heap_caps_malloc(sizeof(faceMaterials) * pal_nmats , MALLOC_CAP_INTERNAL | MALLOC_CAP_32BIT); 
    if (world_palette_ptr == NULL) assert ("Failed to malloc world palette");

    temp_world.palette = world_palette_ptr; // Place ptr for the RAM palette in world structure

    //ESP_LOG_BUFFER_HEX_LEVEL(TAG, (texture_map_ptr+ 0x00000), 128, (esp_log_level_t)1); // Dump 128 bytes

    // Open texture partition with a byte pointer so offsets make sense without adjustment
    tex_ptr = (uint8_t *)texture_map_ptr; // cast to suit 
    
    // The palette texture pointers need to be updated with new memory mapping
    ESP_LOGI(TAG,"Calculating texture offsets and making average shade");

    for(int i=0;i<pal_nmats;i++ ) 
        {
        switch(temp_thin_palette[i].type)
            {
                case PAL_PLAIN: // Process RGB888 palette entry
                // Copy rgb from partition and set to show that no texture
                world_palette_ptr[i].rgb888 = temp_thin_palette[i].parameter;
                world_palette_ptr[i].width = 0x00;
                world_palette_ptr[i].height= 0x00; // Texture doesn't matter
                break;

                case PAL_TEXOFF: // Process texture offset entry
                // Fetch the offset of the texture within the file and add it to the base
                // also adding the offset of the bitmap away from the dib start
                const uint32_t combined_offset = temp_thin_palette[i].parameter;
                const uint32_t texture_Ns = combined_offset & 0xff000000; // Separate off the texture's Ns roughness value
                const uint32_t offset = combined_offset & 0x00ffffff; // Mask off the high order byte to leave the texture
                const uint8_t * dib_base = tex_ptr + offset; // the origin of the dib in the partition
                const uint32_t dib_offset = * (uint32_t *)(dib_base + 0x00); // Fetch the offset of bitmap in the dib which is 32 bit

                //ESP_LOGI(TAG,"offset %x",(unsigned int)dib_offset);

                world_palette_ptr[i].image = (uint32_t *)(dib_base + dib_offset); // the actual bitmap is saved in the part_palette
                ESP_LOGI(TAG,"Texture is placed %p",world_palette_ptr[i].image);

                // Use offsets to access dimensions in the DIB header
                // They are 32 bit in dib, 16 bit in the facematerials structure here
                world_palette_ptr[i].width = * (uint16_t *)(dib_base + 0x04); // Fetch the image width
                world_palette_ptr[i].height = * (uint16_t *)(dib_base + 0x08); // Fetch the image height

                // Work out the average texture colour to be used at a distance
                world_palette_ptr[i].rgb888 = AvgCol( world_palette_ptr[i].image , world_palette_ptr[i].width * world_palette_ptr[i].height, texture_Ns);

                break;
            } // end of palette swtich 
            ESP_LOGI(TAG,"Event %08X and colour %08X ",
                (int)temp_thin_palette[i].event_code,
                (int)world_palette_ptr[i].rgb888);
            // Copy event number into the working palette
            world_palette_ptr[i].event = temp_thin_palette[i].event_code;
        } // end of palette nmats loop

    ESP_LOGI(TAG,"Mapping chunk information to arrays in RAM");
    ESP_LOG_BUFFER_HEX_LEVEL(TAG,(w_map_ptr + world_header_ptr->chunks),0x10,(esp_log_level_t)1);

    int16_t * chunk_param_ptr = (int16_t *)(w_map_ptr + world_header_ptr->chunks);
    
    // Chunk parameters copied from partition into the structure
    temp_world.ChAr.xmin = * (chunk_param_ptr + 0);
    temp_world.ChAr.zmin =  * (chunk_param_ptr + 1);
    temp_world.ChAr.xcount =  * (chunk_param_ptr + 2);
    temp_world.ChAr.zcount =  * (chunk_param_ptr + 3);
    temp_world.ChAr.size =  * (chunk_param_ptr + 4);

    // calculate the size of the chunk map array based on its components
    int16_t chunk_map_size = temp_world.ChAr.xcount * temp_world.ChAr.zcount * (sizeof(uint16_t *) + sizeof(uint32_t));

    //ESP_LOGI(TAG, "Chunk map size in bytes is %d",chunk_map_size);

    // make memory for the map, point to it from world struct
    temp_world.TheChunks = (ChunkFaces *) heap_caps_malloc(chunk_map_size , MALLOC_CAP_INTERNAL | MALLOC_CAP_32BIT);
    if (temp_world.TheChunks == NULL) assert ("Chunk table malloc failed");

    // Find the start of the chunk map in the partition
    constexpr uint32_t chunk_header_size = sizeof(uint16_t) * 6; // 5 values plus 1 padding

    uint8_t * temp_ch_ptr = (uint8_t *) chunk_param_ptr; // Cast to byte size to keep arithetic in line  
    uint32_t * chunk_map_ptr = (uint32_t *) (temp_ch_ptr + chunk_header_size); // Ptr to read from the map

    // Do a looped copy so that pointers can be updated as it happens using offsets in the binary
    // The chunk map will then point to chunk list in flash partition
    for (uint32_t ch_index = 0; ch_index < temp_world.ChAr.xcount * temp_world.ChAr.zcount ; ch_index++)
    {
        // Adapt to copy face lists into arrays in heap

        // Fetch the pointer to the list of faces, and step to the count
        uint16_t * const faces_ptr = (uint16_t *)(temp_ch_ptr + (* chunk_map_ptr)); chunk_map_ptr++;

        // Fetch the count and step for next chunk
        const uint32_t face_count = * chunk_map_ptr; chunk_map_ptr++; 

        //ESP_LOGI(TAG,"Chunk index %d and face count is %d",(int)ch_index, (int)face_count);

        temp_world.TheChunks[ch_index].face_count = face_count;
        if (face_count == 0) continue; // No faces in this chunk so don't do malloc etc
 
        // malloc enough space for the count and put the address in the pointer area
        uint16_t * temp_chunk_face_ptr = (uint16_t *) heap_caps_malloc(face_count * sizeof(uint16_t) , MALLOC_CAP_INTERNAL);
        if (temp_chunk_face_ptr == NULL) assert ("Chunk face list malloc failed");
        temp_world.TheChunks[ch_index].faces_ptr = temp_chunk_face_ptr; 
        
        // Copy array of faces from the partition to the RAM
        for (uint32_t i = 0; i < face_count; i++)
        {
            //ESP_LOGI(TAG,"face in chunk %d",(int)faces_ptr[i]);
            temp_chunk_face_ptr[i] = faces_ptr[i]; 
        }
    } // End of for to each chunk

    // Return the world layout that's been built
    return(temp_world);
}; // End of ReadWorld