#include "scatter_gen.h"
#include "game/biome.h"
#include "content/materials.h"
#include "engine/core/noise.h"
#include "engine/core/math.h"
#include "engine/core/rng.h"
#include "engine/voxel/chunk.h"

#define SCATTER_SEED_OFFSET 99999

void scatter_gen_apply(VoxelVolume *vol, float voxel_size,
                       const ScatterConfig *configs, int32_t config_count,
                       uint32_t seed)
{
    RngState rng;
    rng_seed(&rng, seed + SCATTER_SEED_OFFSET);

    int32_t total_x = vol->chunks_x * CHUNK_SIZE;
    int32_t total_z = vol->chunks_z * CHUNK_SIZE;
    int32_t total_y = vol->chunks_y * CHUNK_SIZE;

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

            float temp = biome_temperature(x, z, seed);
            float humidity = biome_humidity(x, z, seed);

            for (int32_t gy = total_y - 1; gy > 0; gy--)
            {
                int32_t cy = gy >> CHUNK_SIZE_BITS;
                int32_t ly = gy & CHUNK_SIZE_MASK;
                int32_t idx = cx + cy * vol->chunks_x + cz * vol->chunks_x * vol->chunks_y;
                Chunk *chunk = &vol->chunks[idx];

                uint8_t mat = chunk_get(chunk, lx, ly, lz);
                if (mat == 0)
                    continue;

                int32_t ay = gy + 1;
                if (ay >= total_y)
                    break;
                int32_t acy = ay >> CHUNK_SIZE_BITS;
                int32_t aly = ay & CHUNK_SIZE_MASK;
                int32_t aidx = cx + acy * vol->chunks_x + cz * vol->chunks_x * vol->chunks_y;
                if (chunk_get(&vol->chunks[aidx], lx, aly, lz) != 0)
                    break;

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

                break;
            }
        }
    }

    volume_edit_end(vol);
}
