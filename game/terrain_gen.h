#ifndef PATCH_GAME_TERRAIN_GEN_H
#define PATCH_GAME_TERRAIN_GEN_H

#include "engine/voxel/volume.h"
#include "engine/core/types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        float amplitude;
        float frequency;
        int32_t num_pillars;
    } TerrainGenParams;

#define TERRAIN_BASE_HEIGHT 2.0f

    float terrain_gen_height(float x, float z, float amplitude, float frequency, uint32_t seed);

    void terrain_gen_heightmap(VoxelVolume *vol, float voxel_size, float amplitude,
                               float frequency, uint32_t seed);

    void terrain_gen_pillars(VoxelVolume *vol, float voxel_size, int32_t count,
                             float amplitude, float frequency, uint32_t seed);

#ifdef __cplusplus
}
#endif

#endif
