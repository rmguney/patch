#ifndef PATCH_GAME_SCATTER_GEN_H
#define PATCH_GAME_SCATTER_GEN_H

#include "content/scatter.h"
#include "engine/voxel/volume.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    void scatter_gen_apply(VoxelVolume *vol, float voxel_size,
                           const ScatterConfig *configs, int32_t config_count,
                           uint32_t seed);

#ifdef __cplusplus
}
#endif

#endif
