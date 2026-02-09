#include "terrain_gen.h"
#include "tree_gen.h"
#include "game/biome.h"
#include "content/materials.h"
#include "engine/core/noise.h"
#include "engine/core/rng.h"
#include "engine/voxel/chunk.h"
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
#define SLOPE_STEEP_THRESHOLD 2.0f

static const uint8_t PASTEL_MATERIALS[] = {
    MAT_PINK, MAT_CYAN, MAT_PEACH, MAT_MINT, MAT_LAVENDER,
    MAT_SKY, MAT_TEAL, MAT_CORAL, MAT_CLOUD, MAT_ROSE};
static const int32_t PASTEL_COUNT = sizeof(PASTEL_MATERIALS) / sizeof(PASTEL_MATERIALS[0]);

float terrain_noise_2d(float x, float z, uint32_t seed)
{
    return noise_value_2d(x, z, seed);
}

float terrain_gen_height(float x, float z, float amplitude, float frequency, uint32_t seed)
{
    float base = noise_fbm_2d(x * frequency, z * frequency, seed, 6, 2.0f, 0.5f) * amplitude;

    float ridge = noise_ridged_2d(x * frequency * 0.5f, z * frequency * 0.5f,
                                   seed + 2000, 4, 2.2f, 0.5f);
    float ridge_mask = noise_value_2d(x * frequency * 0.1f, z * frequency * 0.1f, seed + 6000);
    ridge_mask = fmaxf(0.0f, ridge_mask);

    return base + ridge * ridge_mask * amplitude * 0.5f;
}

void terrain_gen_heightmap(VoxelVolume *vol, float voxel_size, float amplitude,
                            float frequency, uint32_t seed)
{
    float base_height = TERRAIN_BASE_HEIGHT;
    int32_t total_x = vol->chunks_x * CHUNK_SIZE;
    int32_t total_y = vol->chunks_y * CHUNK_SIZE;
    int32_t total_z = vol->chunks_z * CHUNK_SIZE;

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

            float h = terrain_gen_height(x, z, amplitude, frequency, seed);
            float surface_y = base_height + h;

            float temp = biome_temperature(x, z, seed);
            float humidity = biome_humidity(x, z, seed);
            float relative_alt = (amplitude > 0.0f) ? (h / amplitude) : 0.0f;
            BiomeKind biome = biome_classify(temp, humidity, relative_alt);

            uint8_t surface_mat = biome_surface_material(biome);
            uint8_t sub_mat = biome_subsurface_material(biome);

            float h_dx = terrain_gen_height(x + voxel_size, z, amplitude, frequency, seed);
            float h_dz = terrain_gen_height(x, z + voxel_size, amplitude, frequency, seed);
            float slope = fabsf(h_dx - h) + fabsf(h_dz - h);
            bool steep = slope > voxel_size * SLOPE_STEEP_THRESHOLD;

            float grass_depth = voxel_size * GRASS_DEPTH_MULT;
            if (biome == BIOME_DESERT || biome == BIOME_MOUNTAIN)
                grass_depth = voxel_size * DIRT_DEPTH_MULT;
            else if (biome == BIOME_SWAMP)
                grass_depth *= 0.5f;

            int32_t surface_gy = (int32_t)ceilf((surface_y - vol->bounds.min_y) / voxel_size);
            if (surface_gy > total_y)
                surface_gy = total_y;

            for (int32_t gy = 0; gy < surface_gy; gy++)
            {
                int32_t cy = gy >> CHUNK_SIZE_BITS;
                int32_t ly = gy & CHUNK_SIZE_MASK;
                float depth = surface_y - (vol->bounds.min_y + gy * voxel_size);

                uint8_t mat;
                if (steep)
                    mat = MAT_STONE;
                else if (depth < grass_depth)
                    mat = surface_mat;
                else if (depth < voxel_size * DIRT_DEPTH_MULT)
                    mat = sub_mat;
                else
                    mat = MAT_STONE;

                int32_t idx = cx + cy * vol->chunks_x + cz * vol->chunks_x * vol->chunks_y;
                chunk_set(&vol->chunks[idx], lx, ly, lz, mat);
            }
        }
    }
}

void terrain_gen_water(VoxelVolume *vol, float voxel_size, float amplitude,
                       float frequency, uint32_t seed)
{
    (void)amplitude;
    (void)frequency;
    (void)seed;

    int32_t total_x = vol->chunks_x * CHUNK_SIZE;
    int32_t total_z = vol->chunks_z * CHUNK_SIZE;
    int32_t sea_gy = (int32_t)((TERRAIN_SEA_LEVEL - vol->bounds.min_y) / voxel_size);
    if (sea_gy <= 0)
        return;
    int32_t total_y = vol->chunks_y * CHUNK_SIZE;
    if (sea_gy > total_y)
        sea_gy = total_y;

    volume_edit_begin(vol);

    for (int32_t gz = 0; gz < total_z; gz++)
    {
        int32_t cz = gz >> CHUNK_SIZE_BITS;
        int32_t lz = gz & CHUNK_SIZE_MASK;

        for (int32_t gx = 0; gx < total_x; gx++)
        {
            int32_t cx = gx >> CHUNK_SIZE_BITS;
            int32_t lx = gx & CHUNK_SIZE_MASK;

            for (int32_t gy = sea_gy - 1; gy >= 0; gy--)
            {
                int32_t cy = gy >> CHUNK_SIZE_BITS;
                int32_t ly = gy & CHUNK_SIZE_MASK;
                int32_t idx = cx + cy * vol->chunks_x + cz * vol->chunks_x * vol->chunks_y;
                Chunk *chunk = &vol->chunks[idx];

                if (chunk_get(chunk, lx, ly, lz) != 0)
                    break;
                chunk_set(chunk, lx, ly, lz, MAT_WATER);
            }
        }
    }

    /* Mark all chunks at or below sea level as dirty */
    int32_t max_cy = (sea_gy >> CHUNK_SIZE_BITS) + 1;
    if (max_cy > vol->chunks_y)
        max_cy = vol->chunks_y;
    for (int32_t cz = 0; cz < vol->chunks_z; cz++)
    {
        for (int32_t cy = 0; cy < max_cy; cy++)
        {
            for (int32_t cx = 0; cx < vol->chunks_x; cx++)
            {
                int32_t idx = cx + cy * vol->chunks_x + cz * vol->chunks_x * vol->chunks_y;
                vol->chunks[idx].dirty_frame = vol->current_frame;
            }
        }
    }

    volume_edit_end(vol);
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

/* ---- Tree proclivity system ---- */

#define TREE_SEED_OFFSET 77777
#define TREE_SPACING 5.0f
#define TREE_MARGIN 3.0f
#define TREE_SCALE 0.2f
#define TREE_MAX_DEPTH_SMALL 3

typedef struct
{
    TreeType type;
    float ideal_temp;
    float ideal_humidity;
    float temp_tolerance;
    float hum_tolerance;
    float weight;
} TreeProclivity;

static const TreeProclivity TREE_PROCLIVITIES[] = {
    { TREE_OAK,        0.1f,  0.5f, 0.5f, 0.4f, 10.0f },
    { TREE_PINE,      -0.3f,  0.3f, 0.4f, 0.5f,  8.0f },
    { TREE_FROSTPINE, -0.6f,  0.4f, 0.3f, 0.4f,  6.0f },
    { TREE_BIRCH,      0.0f,  0.5f, 0.4f, 0.3f,  6.0f },
    { TREE_JUNGLE,     0.7f,  0.7f, 0.3f, 0.3f,  5.0f },
    { TREE_BAOBAB,     0.6f,  0.2f, 0.3f, 0.3f,  2.0f },
    { TREE_ACACIA,     0.5f,  0.3f, 0.3f, 0.3f,  5.0f },
    { TREE_CHERRY,     0.2f,  0.4f, 0.3f, 0.3f,  3.0f },
    { TREE_DEAD,       0.0f,  0.1f, 0.8f, 0.3f,  0.5f },
    { TREE_MANGROVE,   0.4f,  0.7f, 0.3f, 0.2f,  2.0f },
    { TREE_REDWOOD,    0.1f,  0.6f, 0.3f, 0.2f,  1.5f },
    { TREE_CEDAR,     -0.1f,  0.3f, 0.4f, 0.3f,  4.0f },
    { TREE_MAPLE,      0.1f,  0.4f, 0.4f, 0.3f,  5.0f },
    { TREE_PALM,       0.7f,  0.5f, 0.3f, 0.4f,  4.0f },
    { TREE_SWAMP,      0.3f,  0.8f, 0.3f, 0.2f,  3.0f },
    { TREE_AUTUMN,     0.0f,  0.3f, 0.4f, 0.4f,  5.0f },
    { TREE_CHESTNUT,   0.1f,  0.5f, 0.4f, 0.3f,  4.0f },
};
#define TREE_PROCLIVITY_COUNT (int32_t)(sizeof(TREE_PROCLIVITIES) / sizeof(TREE_PROCLIVITIES[0]))

static TreeType pick_tree_type(float temp, float humidity, RngState *rng)
{
    float weights[TREE_PROCLIVITY_COUNT];
    float total = 0.0f;

    for (int32_t i = 0; i < TREE_PROCLIVITY_COUNT; i++)
    {
        const TreeProclivity *p = &TREE_PROCLIVITIES[i];
        float tw = biome_closeness(temp, p->ideal_temp, p->temp_tolerance);
        float hw = biome_closeness(humidity, p->ideal_humidity, p->hum_tolerance);
        weights[i] = tw * hw * p->weight;
        total += weights[i];
    }

    if (total <= 0.0f)
        return TREE_DEAD;

    float roll = rng_float(rng) * total;
    float cumulative = 0.0f;
    for (int32_t i = 0; i < TREE_PROCLIVITY_COUNT; i++)
    {
        cumulative += weights[i];
        if (roll <= cumulative)
            return TREE_PROCLIVITIES[i].type;
    }
    return TREE_OAK;
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
                if (mat != 0 && mat != MAT_GRASS && mat != MAT_DIRT && mat != MAT_STONE
                    && mat != MAT_SAND && mat != MAT_SNOW)
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
            float density = noise_value_2d(x * 0.25f, z * 0.25f, seed + 5000);
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

            float temp = biome_temperature(jx, jz, seed);
            float humidity = biome_humidity(jx, jz, seed);

            BiomeKind biome = biome_classify(temp, humidity, 0.0f);
            if (biome == BIOME_DESERT)
                continue;

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
