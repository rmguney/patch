#ifndef PATCH_GAME_SHRUB_GEN_H
#define PATCH_GAME_SHRUB_GEN_H

#include "engine/voxel/volume.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    void shrub_gen_place(VoxelVolume *vol, float voxel_size,
                         float amplitude, float frequency, uint32_t seed);

#ifdef __cplusplus
}
#endif

#endif
