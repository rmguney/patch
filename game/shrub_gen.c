#include "shrub_gen.h"
#include "terrain_gen.h"
#include "game/biome.h"
#include "content/materials.h"
#include "engine/core/noise.h"
#include "engine/core/rng.h"
#include <math.h>

#define SHRUB_SPACING 4.0f
#define SHRUB_SEED_OFFSET 7777

typedef struct
{
    uint8_t leaf_material;
    float ideal_temp;
    float ideal_humidity;
    float temp_tolerance;
    float humidity_tolerance;
    float base_weight;
    int32_t min_height;
    int32_t max_height;
} ShrubConfig;

static const ShrubConfig s_shrub_configs[] = {
    /* Grass bush — temperate meadows */
    {MAT_OAK_LEAF, 0.0f, 0.3f, 0.6f, 0.5f, 1.0f, 2, 3},
    /* Flowering bush — humid forests */
    {MAT_CHERRY_LEAF, 0.1f, 0.5f, 0.5f, 0.4f, 0.6f, 2, 4},
    /* Desert scrub — hot/dry */
    {MAT_AUTUMN_LEAF, 0.5f, 0.0f, 0.4f, 0.3f, 0.5f, 1, 2},
    /* Snow bush — cold regions */
    {MAT_PINE_LEAF, -0.3f, 0.3f, 0.4f, 0.4f, 0.4f, 1, 3},
};

#define SHRUB_CONFIG_COUNT (int32_t)(sizeof(s_shrub_configs) / sizeof(s_shrub_configs[0]))

static void place_shrub(VoxelVolume *vol, float voxel_size,
                         float cx, float cy, float cz,
                         int32_t height, uint8_t leaf_mat)
{
    float radius = voxel_size * (float)(height - 1) * 0.5f + voxel_size * 0.5f;

    for (int32_t dy = 0; dy < height; dy++)
    {
        float y = cy + (float)dy * voxel_size;
        float t = (float)dy / (float)height;
        float r = radius * (1.0f - t * 0.4f);

        for (float dx = -r; dx <= r; dx += voxel_size)
        {
            for (float dz = -r; dz <= r; dz += voxel_size)
            {
                float dist2 = dx * dx + dz * dz;
                if (dist2 <= r * r)
                {
                    Vec3 pos = vec3_create(cx + dx, y, cz + dz);
                    volume_edit_set(vol, pos, leaf_mat);
                }
            }
        }
    }
}

void shrub_gen_place(VoxelVolume *vol, float voxel_size,
                     float amplitude, float frequency, uint32_t seed)
{
    RngState rng;
    rng_seed(&rng, seed + SHRUB_SEED_OFFSET);

    float spacing = SHRUB_SPACING;

    volume_edit_begin(vol);

    for (float x = vol->bounds.min_x + spacing; x < vol->bounds.max_x - spacing; x += spacing)
    {
        for (float z = vol->bounds.min_z + spacing; z < vol->bounds.max_z - spacing; z += spacing)
        {
            float jx = x + rng_range_f32(&rng, -spacing * 0.4f, spacing * 0.4f);
            float jz = z + rng_range_f32(&rng, -spacing * 0.4f, spacing * 0.4f);

            float surface_y = TERRAIN_BASE_HEIGHT + terrain_gen_height(jx, jz, amplitude, frequency, seed);

            if (surface_y <= TERRAIN_SEA_LEVEL)
                continue;

            float temp = biome_temperature(jx, jz, seed);
            float humidity = biome_humidity(jx, jz, seed);

            float total_weight = 0.0f;
            float weights[SHRUB_CONFIG_COUNT];
            for (int32_t i = 0; i < SHRUB_CONFIG_COUNT; i++)
            {
                const ShrubConfig *cfg = &s_shrub_configs[i];
                float w = biome_closeness(temp, cfg->ideal_temp, cfg->temp_tolerance) *
                          biome_closeness(humidity, cfg->ideal_humidity, cfg->humidity_tolerance) *
                          cfg->base_weight;
                weights[i] = w;
                total_weight += w;
            }

            if (total_weight < 0.1f)
                continue;

            float placement_chance = total_weight * 0.3f;
            if (rng_float(&rng) > placement_chance)
                continue;

            float roll = rng_float(&rng) * total_weight;
            float cumulative = 0.0f;
            int32_t chosen = 0;
            for (int32_t i = 0; i < SHRUB_CONFIG_COUNT; i++)
            {
                cumulative += weights[i];
                if (roll <= cumulative)
                {
                    chosen = i;
                    break;
                }
            }

            const ShrubConfig *cfg = &s_shrub_configs[chosen];
            int32_t height = cfg->min_height + (int32_t)(rng_float(&rng) * (float)(cfg->max_height - cfg->min_height + 1));
            if (height > cfg->max_height)
                height = cfg->max_height;

            place_shrub(vol, voxel_size, jx, surface_y + voxel_size, jz,
                       height, cfg->leaf_material);
        }
    }

    volume_edit_end(vol);
}
