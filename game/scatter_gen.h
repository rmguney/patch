#ifndef PATCH_GAME_SCATTER_GEN_H
#define PATCH_GAME_SCATTER_GEN_H

#include "game/terrain_gen.h"
#include "engine/voxel/volume.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* Grows each biome's ground flora (BiomeDesc.flora) one voxel above the ground of
     * gently sloped columns where that voxel is open air: under trees too, never on water
     * or a lake cover. Without columns (terrain_columns_build ran out of memory) it places
     * nothing. Returns the number of voxels placed. */
    int32_t scatter_gen_apply(VoxelVolume *vol, const TerrainColumns *cols, JobSystem *jobs);

#ifdef __cplusplus
}
#endif

#endif
