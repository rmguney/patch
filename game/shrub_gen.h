#ifndef PATCH_GAME_SHRUB_GEN_H
#define PATCH_GAME_SHRUB_GEN_H

#include "game/terrain_gen.h"
#include "engine/voxel/volume.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* One candidate spot per grid cell; a spot grows one of its biome's shrubs with the
     * biome's shrub chance when the voxel above the terrain is free. Returns the count. */
    int32_t shrub_gen_place(VoxelVolume *vol, const TerrainColumns *cols);

#ifdef __cplusplus
}
#endif

#endif
