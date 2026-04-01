#include "scatter_gen.h"
#include "game/biome.h"
#include "content/materials.h"
#include "engine/core/noise.h"
#include "engine/core/math.h"
#include "engine/core/rng.h"
#include "engine/voxel/chunk.h"
#include <stdlib.h>

#define SCATTER_SEED_OFFSET 99999
#define SCATTER_SLOPE_THRESHOLD 2

void scatter_gen_apply(VoxelVolume *vol, float voxel_size,
                       const ScatterConfig *configs, int32_t config_count,
                       uint32_t seed)
{
    RngState rng;
    rng_seed(&rng, seed + SCATTER_SEED_OFFSET);

    int32_t total_x = vol->chunks_x * CHUNK_SIZE;
    int32_t total_z = vol->chunks_z * CHUNK_SIZE;
    int32_t total_y = vol->chunks_y * CHUNK_SIZE;

    /* Pre-compute surface height map for O(1) slope lookups */
    int32_t *surface_map = (int32_t *)malloc((size_t)total_x * (size_t)total_z * sizeof(int32_t));
    if (!surface_map)
        return;

    for (int32_t gx = 0; gx < total_x; gx++)
    {
        int32_t cx = gx >> CHUNK_SIZE_BITS;
        int32_t lx = gx & CHUNK_SIZE_MASK;
        for (int32_t gz = 0; gz < total_z; gz++)
        {
            int32_t cz = gz >> CHUNK_SIZE_BITS;
            int32_t lz = gz & CHUNK_SIZE_MASK;
            int32_t surface_gy = -1;
            for (int32_t gy = total_y - 1; gy >= 0; gy--)
            {
                int32_t cy = gy >> CHUNK_SIZE_BITS;
                int32_t ly = gy & CHUNK_SIZE_MASK;
                int32_t idx = cx + cy * vol->chunks_x + cz * vol->chunks_x * vol->chunks_y;
                if (chunk_get(&vol->chunks[idx], lx, ly, lz) != 0)
                {
                    surface_gy = gy;
                    break;
                }
            }
            surface_map[gx * total_z + gz] = surface_gy;
        }
    }

    volume_edit_begin(vol);

    for (int32_t gx = 0; gx < total_x; gx++)
    {
        int32_t cx = gx >> CHUNK_SIZE_BITS;
        int32_t lx = gx & CHUNK_SIZE_MASK;
        float x = vol->bounds.min_x + gx * voxel_size;

        for (int32_t gz = 0; gz < total_z; gz++)
        {
            int32_t cz = gz >> CHUNK_SIZE_BITS;
            int32_t lz = gz & CHUNK_SIZE_MASK;
            float z = vol->bounds.min_z + gz * voxel_size;

            int32_t gy = surface_map[gx * total_z + gz];
            if (gy < 0 || gy + 1 >= total_y)
                continue;

            /* Slope rejection via surface height map (O(1) lookup) */
            bool steep = false;
            if (gx + 1 < total_x && abs(surface_map[(gx + 1) * total_z + gz] - gy) > SCATTER_SLOPE_THRESHOLD)
                steep = true;
            if (!steep && gz + 1 < total_z && abs(surface_map[gx * total_z + gz + 1] - gy) > SCATTER_SLOPE_THRESHOLD)
                steep = true;
            if (steep)
                continue;

            int32_t cy = gy >> CHUNK_SIZE_BITS;
            int32_t ly = gy & CHUNK_SIZE_MASK;
            int32_t idx = cx + cy * vol->chunks_x + cz * vol->chunks_x * vol->chunks_y;
            uint8_t mat = chunk_get(&vol->chunks[idx], lx, ly, lz);
            if (mat == 0)
                continue;

            int32_t ay = gy + 1;
            int32_t acy = ay >> CHUNK_SIZE_BITS;
            int32_t aly = ay & CHUNK_SIZE_MASK;
            int32_t aidx = cx + acy * vol->chunks_x + cz * vol->chunks_x * vol->chunks_y;
            if (chunk_get(&vol->chunks[aidx], lx, aly, lz) != 0)
                continue;

            float temp = biome_temperature(x, z, seed);
            float humidity = biome_humidity(x, z, seed);

            for (int32_t c = 0; c < config_count; c++)
            {
                const ScatterConfig *cfg = &configs[c];
                if (mat != cfg->surface_material)
                    continue;

                float density = cfg->density;

                float biome_factor = biome_closeness(temp, cfg->temp_center, cfg->temp_tolerance)
                                   * biome_closeness(humidity, cfg->humidity_center, cfg->humidity_tolerance);
                density *= biome_factor;

                if (cfg->noise_wavelength > 0.0f)
                {
                    float noise = noise_value_2d(
                        x / cfg->noise_wavelength,
                        z / cfg->noise_wavelength,
                        seed + (uint32_t)c * 7919);
                    noise = (noise + 1.0f) * 0.5f;
                    if (noise < cfg->noise_threshold)
                        density = 0.0f;
                    else
                        density *= noise;
                }

                if (density > 0.0f && rng_float(&rng) < density)
                {
                    chunk_set(&vol->chunks[aidx], lx, aly, lz, cfg->material);
                    break;
                }
            }
        }
    }

    volume_edit_end(vol);
    free(surface_map);
}
