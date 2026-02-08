#include "terrain_gen.h"
#include "tree_gen.h"
#include "content/materials.h"
#include "engine/core/rng.h"
#include <math.h>

#define GRASS_DEPTH_MULT 1.5f
#define DIRT_DEPTH_MULT 4.0f
#define PILLAR_BASE_MULT 1.2f
#define PILLAR_TOP_MULT 0.8f
#define PILLAR_BASE_DEPTH 2.0f
#define STRUCTURE_MARGIN 2.0f
#define STRUCTURE_SEED 12345
#define PILLAR_HEIGHT_MIN 3.0f
#define PILLAR_HEIGHT_MAX 8.0f
#define PILLAR_RADIUS_MIN 0.3f
#define PILLAR_RADIUS_MAX 0.6f

static const uint8_t PASTEL_MATERIALS[] = {
    MAT_PINK, MAT_CYAN, MAT_PEACH, MAT_MINT, MAT_LAVENDER,
    MAT_SKY, MAT_TEAL, MAT_CORAL, MAT_CLOUD, MAT_ROSE};
static const int32_t PASTEL_COUNT = sizeof(PASTEL_MATERIALS) / sizeof(PASTEL_MATERIALS[0]);

static float noise_hash(int32_t x, int32_t z, uint32_t seed)
{
    uint32_t n = (uint32_t)x + (uint32_t)z * 57 + seed * 131;
    n = (n << 13) ^ n;
    return 1.0f - (float)((n * (n * n * 15731 + 789221) + 1376312589) & 0x7FFFFFFF) / 1073741824.0f;
}

static float noise_lerp(float a, float b, float t)
{
    return a + t * (b - a);
}

static float noise_smooth(float t)
{
    return t * t * (3.0f - 2.0f * t);
}

float terrain_noise_2d(float x, float z, uint32_t seed)
{
    int32_t ix = (int32_t)floorf(x);
    int32_t iz = (int32_t)floorf(z);
    float fx = x - (float)ix;
    float fz = z - (float)iz;

    float v00 = noise_hash(ix, iz, seed);
    float v10 = noise_hash(ix + 1, iz, seed);
    float v01 = noise_hash(ix, iz + 1, seed);
    float v11 = noise_hash(ix + 1, iz + 1, seed);

    float sx = noise_smooth(fx);
    float sz = noise_smooth(fz);

    float nx0 = noise_lerp(v00, v10, sx);
    float nx1 = noise_lerp(v01, v11, sx);

    return noise_lerp(nx0, nx1, sz);
}

float terrain_gen_height(float x, float z, float amplitude, float frequency, uint32_t seed)
{
    float height = 0.0f;
    float amp = amplitude;
    float freq = frequency;

    for (int32_t octave = 0; octave < 4; octave++)
    {
        height += terrain_noise_2d(x * freq, z * freq, seed + (uint32_t)octave * 1000) * amp;
        amp *= 0.5f;
        freq *= 2.0f;
    }

    return height;
}

void terrain_gen_heightmap(VoxelVolume *vol, float voxel_size, float amplitude,
                           float frequency, uint32_t seed)
{
    float base_height = TERRAIN_BASE_HEIGHT;

    for (float x = vol->bounds.min_x; x < vol->bounds.max_x; x += voxel_size)
    {
        for (float z = vol->bounds.min_z; z < vol->bounds.max_z; z += voxel_size)
        {
            float h = terrain_gen_height(x, z, amplitude, frequency, seed);
            float surface_y = base_height + h;

            for (float y = vol->bounds.min_y; y < surface_y && y < vol->bounds.max_y; y += voxel_size)
            {
                Vec3 pos = vec3_create(x, y, z);
                float depth = surface_y - y;

                uint8_t mat;
                if (depth < voxel_size * GRASS_DEPTH_MULT)
                    mat = MAT_GRASS;
                else if (depth < voxel_size * DIRT_DEPTH_MULT)
                    mat = MAT_DIRT;
                else
                    mat = MAT_STONE;

                volume_set_at(vol, pos, mat);
            }
        }
    }
}

static void generate_pillar(VoxelVolume *vol, Vec3 base, float height, float radius,
                            uint8_t material, float voxel_size)
{
    for (float y = 0.0f; y < height; y += voxel_size)
    {
        float r = radius;
        if (y < voxel_size * PILLAR_BASE_DEPTH)
            r = radius * PILLAR_BASE_MULT;
        if (y > height - voxel_size * PILLAR_BASE_DEPTH)
            r = radius * PILLAR_TOP_MULT;

        for (float dx = -r; dx <= r; dx += voxel_size)
        {
            for (float dz = -r; dz <= r; dz += voxel_size)
            {
                if (dx * dx + dz * dz <= r * r)
                {
                    Vec3 pos = vec3_create(base.x + dx, base.y + y, base.z + dz);
                    volume_set_at(vol, pos, material);
                }
            }
        }
    }
}

void terrain_gen_pillars(VoxelVolume *vol, float voxel_size, int32_t count,
                         float amplitude, float frequency, uint32_t seed)
{
    RngState rng;
    rng_seed(&rng, seed + STRUCTURE_SEED);

    float margin = STRUCTURE_MARGIN;
    float area_min_x = vol->bounds.min_x + margin;
    float area_max_x = vol->bounds.max_x - margin;
    float area_min_z = vol->bounds.min_z + margin;
    float area_max_z = vol->bounds.max_z - margin;

    for (int32_t i = 0; i < count; i++)
    {
        float x = rng_range_f32(&rng, area_min_x, area_max_x);
        float z = rng_range_f32(&rng, area_min_z, area_max_z);
        float base_y = TERRAIN_BASE_HEIGHT + terrain_gen_height(x, z, amplitude, frequency, seed);

        float height = rng_range_f32(&rng, PILLAR_HEIGHT_MIN, PILLAR_HEIGHT_MAX);
        float radius = rng_range_f32(&rng, PILLAR_RADIUS_MIN, PILLAR_RADIUS_MAX);
        uint8_t mat = PASTEL_MATERIALS[rng_range_u32(&rng, (uint32_t)PASTEL_COUNT)];

        Vec3 base = vec3_create(x, base_y, z);
        generate_pillar(vol, base, height, radius, mat, voxel_size);
    }
}

#define TREE_SEED_OFFSET 77777
#define TREE_SPACING 5.0f  /* world units between tree placement candidates */
#define TREE_MARGIN 3.0f
#define TREE_SCALE 0.2f
#define TREE_MAX_DEPTH_SMALL 3  /* cap depth for small-scale trees */

static TreeType pick_tree_type(float temp, float humidity, RngState *rng)
{
    if (temp < -0.3f)
    {
        if (humidity > 0.3f)
            return TREE_FROSTPINE;
        return rng_float(rng) < 0.7f ? TREE_PINE : TREE_DEAD;
    }
    if (temp > 0.5f)
    {
        if (humidity > 0.5f)
            return rng_float(rng) < 0.6f ? TREE_JUNGLE : TREE_PALM;
        if (humidity > 0.2f)
            return TREE_ACACIA;
        return TREE_DEAD;
    }
    /* Temperate zone */
    float roll = rng_float(rng);
    if (humidity > 0.6f)
        return roll < 0.4f ? TREE_OAK : (roll < 0.7f ? TREE_BIRCH : TREE_CHESTNUT);
    if (humidity > 0.3f)
        return roll < 0.3f ? TREE_OAK : (roll < 0.5f ? TREE_MAPLE : (roll < 0.7f ? TREE_CHERRY : TREE_AUTUMN));
    return roll < 0.5f ? TREE_PINE : TREE_CEDAR;
}

static bool is_structure_at(const VoxelVolume *vol, float x, float z,
                             float surface_y, float voxel_size, float check_radius)
{
    for (float dx = -check_radius; dx <= check_radius; dx += voxel_size)
    {
        for (float dz = -check_radius; dz <= check_radius; dz += voxel_size)
        {
            for (float dy = 0.0f; dy < voxel_size * 3.0f; dy += voxel_size)
            {
                Vec3 pos = vec3_create(x + dx, surface_y + dy, z + dz);
                uint8_t mat = volume_get_at(vol, pos);
                if (mat != 0 && mat != MAT_GRASS && mat != MAT_DIRT && mat != MAT_STONE)
                    return true;
            }
        }
    }
    return false;
}

void terrain_gen_trees(VoxelVolume *vol, float voxel_size, float tree_density,
                       float amplitude, float frequency, uint32_t seed)
{
    RngState rng;
    rng_seed(&rng, seed + TREE_SEED_OFFSET);

    float margin = TREE_MARGIN;
    float spacing = TREE_SPACING;

    volume_edit_begin(vol);

    for (float x = vol->bounds.min_x + margin; x < vol->bounds.max_x - margin; x += spacing)
    {
        for (float z = vol->bounds.min_z + margin; z < vol->bounds.max_z - margin; z += spacing)
        {
            float density = terrain_noise_2d(x * 0.25f, z * 0.25f, seed + 5000);
            density = (density + 1.0f) * 0.5f;
            if (density < (1.0f - tree_density))
                continue;

            if (rng_float(&rng) > tree_density * 0.5f)
                continue;

            float jx = x + rng_range_f32(&rng, -spacing * 0.4f, spacing * 0.4f);
            float jz = z + rng_range_f32(&rng, -spacing * 0.4f, spacing * 0.4f);

            float surface_y = TERRAIN_BASE_HEIGHT + terrain_gen_height(jx, jz, amplitude, frequency, seed);

            if (is_structure_at(vol, jx, jz, surface_y, voxel_size, voxel_size * 2.0f))
                continue;

            float temp = terrain_noise_2d(jx * 0.1f, jz * 0.1f, seed + 3000);
            float humidity = terrain_noise_2d(jx * 0.15f, jz * 0.15f, seed + 4000);

            TreeType type = pick_tree_type(temp, humidity, &rng);
            TreeConfig config = tree_config_create(type, &rng, TREE_SCALE);

            if (config.max_depth > TREE_MAX_DEPTH_SMALL)
                config.max_depth = TREE_MAX_DEPTH_SMALL;

            ProceduralTree tree;
            tree_generate(&tree, &config, &rng);

            Vec3 origin = vec3_create(jx, surface_y, jz);
            tree_voxelize(&tree, &config, vol, origin, voxel_size);
        }
    }

    volume_edit_end(vol);
}
