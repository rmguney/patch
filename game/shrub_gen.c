#include "shrub_gen.h"
#include "game/biome.h"
#include "content/materials.h"
#include "engine/core/rng.h"
#include <math.h>

#define SHRUB_SPACING 2.0f
#define SHRUB_SEED_OFFSET 7777
#define REED_STALKS 5
#define REED_SPREAD 2 /* Voxels from the clump centre */

/* Writes only into empty voxels inside the volume, so shrubs grow around what stands there */
static void grow_voxel(VoxelVolume *vol, int32_t gx, int32_t gy, int32_t gz, uint8_t material)
{
    if (gx < 0 || gy < 0 || gz < 0 || gx >= vol->chunks_x * CHUNK_SIZE ||
        gy >= vol->chunks_y * CHUNK_SIZE || gz >= vol->chunks_z * CHUNK_SIZE)
        return;
    if (terrain_voxel_get(vol, gx, gy, gz) == MAT_AIR)
        terrain_voxel_set(vol, gx, gy, gz, material);
}

/* Rounded bush, narrowing towards the top */
static void grow_dome(VoxelVolume *vol, int32_t gx, int32_t gy, int32_t gz, int32_t height, uint8_t material)
{
    float radius = (float)(height - 1) * 0.5f + 0.5f;
    for (int32_t dy = 0; dy < height; dy++)
    {
        float r = radius * (1.0f - (float)dy / (float)height * 0.4f);
        int32_t reach = (int32_t)r;
        for (int32_t dz = -reach; dz <= reach; dz++)
        {
            for (int32_t dx = -reach; dx <= reach; dx++)
            {
                if ((float)(dx * dx + dz * dz) <= r * r)
                    grow_voxel(vol, gx + dx, gy + dy, gz + dz, material);
            }
        }
    }
}

static void grow_column(VoxelVolume *vol, int32_t gx, int32_t gy, int32_t gz, int32_t height, uint8_t material)
{
    for (int32_t dy = 0; dy < height; dy++)
        for (int32_t dz = 0; dz < 2; dz++)
            for (int32_t dx = 0; dx < 2; dx++)
                grow_voxel(vol, gx + dx, gy + dy, gz + dz, material);
}

static int32_t shrub_height(const BiomeShrub *shrub, RngState *rng)
{
    return shrub->min_height + (int32_t)rng_range_u32(rng, (uint32_t)(shrub->max_height - shrub->min_height + 1));
}

/* Thin stalks around a centre, each rising from its own column's ground */
static void grow_stalks(VoxelVolume *vol, const TerrainColumns *cols, int32_t gx, int32_t gz,
                        const BiomeShrub *shrub, RngState *rng)
{
    for (int32_t s = 0; s < REED_STALKS; s++)
    {
        int32_t x = gx + (int32_t)rng_range_u32(rng, 2 * REED_SPREAD + 1) - REED_SPREAD;
        int32_t z = gz + (int32_t)rng_range_u32(rng, 2 * REED_SPREAD + 1) - REED_SPREAD;
        int32_t height = shrub_height(shrub, rng);
        if (x < 0 || z < 0 || x >= cols->nx || z >= cols->nz)
            continue;
        int32_t y = terrain_surface_voxel(vol, cols->height[terrain_column_index(cols, x, z)]);
        for (int32_t dy = 0; dy < height; dy++)
            grow_voxel(vol, x, y + dy, z, shrub->material);
    }
}

static const BiomeShrub *pick_shrub(const BiomeDesc *biome, RngState *rng)
{
    float total = 0.0f;
    for (int32_t i = 0; i < BIOME_MAX_SHRUBS; i++)
        total += biome->shrubs[i].weight;

    float roll = rng_float(rng) * total;
    const BiomeShrub *picked = &biome->shrubs[0];
    for (int32_t i = 0; i < BIOME_MAX_SHRUBS; i++)
    {
        if (biome->shrubs[i].weight <= 0.0f)
            continue;
        picked = &biome->shrubs[i];
        roll -= biome->shrubs[i].weight;
        if (roll < 0.0f)
            break;
    }
    return picked;
}

int32_t shrub_gen_place(VoxelVolume *vol, const TerrainColumns *cols)
{
    if (cols->nx == 0 || cols->nz == 0)
        return 0;

    const float spacing = SHRUB_SPACING;
    const int32_t total_y = vol->chunks_y * CHUNK_SIZE;
    const int32_t cells_x = (int32_t)ceilf((vol->bounds.max_x - vol->bounds.min_x - 2.0f * spacing) / spacing);
    const int32_t cells_z = (int32_t)ceilf((vol->bounds.max_z - vol->bounds.min_z - 2.0f * spacing) / spacing);

    int32_t placed = 0;
    for (int32_t iz = 0; iz < cells_z; iz++)
    {
        for (int32_t ix = 0; ix < cells_x; ix++)
        {
            RngState rng = terrain_cell_rng(cols->seed + SHRUB_SEED_OFFSET, ix, iz);
            float jx = vol->bounds.min_x + spacing * (float)(ix + 1) + rng_range_f32(&rng, -spacing * 0.4f, spacing * 0.4f);
            float jz = vol->bounds.min_z + spacing * (float)(iz + 1) + rng_range_f32(&rng, -spacing * 0.4f, spacing * 0.4f);

            int32_t i = terrain_column_at(cols, vol, jx, jz);
            if (cols->height[i] <= TERRAIN_SEA_LEVEL)
                continue;

            const BiomeDesc *biome = &g_biomes[cols->biome[i]];
            if (rng_float(&rng) >= biome->shrub_chance)
                continue;

            /* Lakes above sea level, trees and pillars already own the spot */
            int32_t gx = i % cols->nx;
            int32_t gz = i / cols->nx;
            int32_t gy = terrain_surface_voxel(vol, cols->height[i]);
            if (gy < 0 || gy >= total_y || terrain_voxel_get(vol, gx, gy, gz) != MAT_AIR)
                continue;

            const BiomeShrub *shrub = pick_shrub(biome, &rng);
            switch ((ShrubShape)shrub->shape)
            {
            case SHRUB_DOME:
                grow_dome(vol, gx, gy, gz, shrub_height(shrub, &rng), shrub->material);
                break;
            case SHRUB_COLUMN:
                grow_column(vol, gx, gy, gz, shrub_height(shrub, &rng), shrub->material);
                break;
            case SHRUB_STALKS:
                grow_stalks(vol, cols, gx, gz, shrub, &rng);
                break;
            }
            placed++;
        }
    }
    return placed;
}
