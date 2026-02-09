#include "scatter_gen.h"
#include "game/biome.h"
#include "content/materials.h"
#include "engine/core/noise.h"
#include "engine/core/math.h"
#include "engine/core/rng.h"

#define SCATTER_SEED_OFFSET 99999

void scatter_gen_apply(VoxelVolume *vol, float voxel_size,
                       const ScatterConfig *configs, int32_t config_count,
                       uint32_t seed)
{
    RngState rng;
    rng_seed(&rng, seed + SCATTER_SEED_OFFSET);

    volume_edit_begin(vol);

    for (float x = vol->bounds.min_x; x < vol->bounds.max_x; x += voxel_size)
    {
        for (float z = vol->bounds.min_z; z < vol->bounds.max_z; z += voxel_size)
        {
            float temp = biome_temperature(x, z, seed);
            float humidity = biome_humidity(x, z, seed);

            for (float y = vol->bounds.max_y - voxel_size; y > vol->bounds.min_y; y -= voxel_size)
            {
                Vec3 pos = vec3_create(x, y, z);
                uint8_t mat = volume_get_at(vol, pos);
                if (mat == 0)
                    continue;

                Vec3 above = vec3_create(x, y + voxel_size, z);
                if (volume_get_at(vol, above) != 0)
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
                        volume_edit_set(vol, above, cfg->material);
                        break;
                    }
                }

                break;
            }
        }
    }

    volume_edit_end(vol);
}
