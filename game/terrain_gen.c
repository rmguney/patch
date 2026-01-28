#include "terrain_gen.h"
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

static float noise_2d(float x, float z, uint32_t seed)
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
        height += noise_2d(x * freq, z * freq, seed + (uint32_t)octave * 1000) * amp;
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
