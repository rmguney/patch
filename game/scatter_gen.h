#ifndef PATCH_GAME_SCATTER_GEN_H
#define PATCH_GAME_SCATTER_GEN_H

#include "engine/voxel/volume.h"
#include "engine/core/types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        uint8_t material;
        uint8_t surface_material;
        float density;
        float noise_wavelength;
        float noise_threshold;
        float temp_center;
        float temp_tolerance;
        float humidity_center;
        float humidity_tolerance;
    } ScatterConfig;

    void scatter_gen_apply(VoxelVolume *vol, float voxel_size,
                           const ScatterConfig *configs, int32_t config_count,
                           uint32_t seed);

#ifdef __cplusplus
}
#endif

#endif
