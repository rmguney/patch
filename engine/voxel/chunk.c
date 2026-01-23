#include "chunk.h"
#include <math.h>

void chunk_rebuild_occupancy(Chunk *chunk)
{
    chunk->occupancy.level0 = 0;
    chunk->occupancy.level1 = 0;
    chunk->occupancy.solid_count = 0;

    for (int32_t rz = 0; rz < CHUNK_MIP0_SIZE; rz++)
    {
        for (int32_t ry = 0; ry < CHUNK_MIP0_SIZE; ry++)
        {
            for (int32_t rx = 0; rx < CHUNK_MIP0_SIZE; rx++)
            {
                bool region_has_solid = false;
                int32_t base_x = rx * 8;
                int32_t base_y = ry * 8;
                int32_t base_z = rz * 8;

                for (int32_t z = 0; z < 8 && !region_has_solid; z++)
                {
                    for (int32_t y = 0; y < 8 && !region_has_solid; y++)
                    {
                        for (int32_t x = 0; x < 8; x++)
                        {
                            int32_t idx = chunk_voxel_index(base_x + x, base_y + y, base_z + z);
                            if (chunk->voxels[idx].material != MATERIAL_EMPTY)
                            {
                                region_has_solid = true;
                                break;
                            }
                        }
                    }
                }

                if (region_has_solid)
                {
                    int32_t bit_idx = rx + ry * CHUNK_MIP0_SIZE + rz * CHUNK_MIP0_SIZE * CHUNK_MIP0_SIZE;
                    chunk->occupancy.level0 |= (1ULL << bit_idx);
                }
            }
        }
    }

    for (int32_t rz = 0; rz < CHUNK_MIP1_SIZE; rz++)
    {
        for (int32_t ry = 0; ry < CHUNK_MIP1_SIZE; ry++)
        {
            for (int32_t rx = 0; rx < CHUNK_MIP1_SIZE; rx++)
            {
                bool region_has_solid = false;
                for (int32_t dz = 0; dz < 2 && !region_has_solid; dz++)
                {
                    for (int32_t dy = 0; dy < 2 && !region_has_solid; dy++)
                    {
                        for (int32_t dx = 0; dx < 2; dx++)
                        {
                            int32_t l0_x = rx * 2 + dx;
                            int32_t l0_y = ry * 2 + dy;
                            int32_t l0_z = rz * 2 + dz;
                            int32_t bit_idx = l0_x + l0_y * CHUNK_MIP0_SIZE + l0_z * CHUNK_MIP0_SIZE * CHUNK_MIP0_SIZE;
                            if (chunk->occupancy.level0 & (1ULL << bit_idx))
                            {
                                region_has_solid = true;
                                break;
                            }
                        }
                    }
                }

                if (region_has_solid)
                {
                    int32_t bit_idx = rx + ry * CHUNK_MIP1_SIZE + rz * CHUNK_MIP1_SIZE * CHUNK_MIP1_SIZE;
                    chunk->occupancy.level1 |= (1 << bit_idx);
                }
            }
        }
    }

    /* Count solid voxels */
    for (int32_t i = 0; i < CHUNK_VOXEL_COUNT; i++)
    {
        if (chunk->voxels[i].material != MATERIAL_EMPTY)
        {
            chunk->occupancy.solid_count++;
        }
    }

    chunk->occupancy.has_any = (chunk->occupancy.solid_count > 0) ? 1 : 0;
}

void chunk_update_occupancy_region(Chunk *chunk, int32_t region_x, int32_t region_y, int32_t region_z)
{
    if (region_x < 0 || region_x >= CHUNK_MIP0_SIZE ||
        region_y < 0 || region_y >= CHUNK_MIP0_SIZE ||
        region_z < 0 || region_z >= CHUNK_MIP0_SIZE)
        return;

    /* Check if this 8x8x8 region has any solid voxels */
    bool region_has_solid = false;
    int32_t base_x = region_x * 8;
    int32_t base_y = region_y * 8;
    int32_t base_z = region_z * 8;

    for (int32_t z = 0; z < 8 && !region_has_solid; z++)
    {
        for (int32_t y = 0; y < 8 && !region_has_solid; y++)
        {
            for (int32_t x = 0; x < 8; x++)
            {
                int32_t idx = chunk_voxel_index(base_x + x, base_y + y, base_z + z);
                if (chunk->voxels[idx].material != MATERIAL_EMPTY)
                {
                    region_has_solid = true;
                    break;
                }
            }
        }
    }

    /* Update level0 bit */
    int32_t l0_bit = region_x + region_y * CHUNK_MIP0_SIZE + region_z * CHUNK_MIP0_SIZE * CHUNK_MIP0_SIZE;
    if (region_has_solid)
    {
        chunk->occupancy.level0 |= (1ULL << l0_bit);
    }
    else
    {
        chunk->occupancy.level0 &= ~(1ULL << l0_bit);
    }

    /* Update parent level1 region (2x2x2 of level0 regions) */
    int32_t l1_x = region_x / 2;
    int32_t l1_y = region_y / 2;
    int32_t l1_z = region_z / 2;

    bool l1_has_solid = false;
    for (int32_t dz = 0; dz < 2 && !l1_has_solid; dz++)
    {
        for (int32_t dy = 0; dy < 2 && !l1_has_solid; dy++)
        {
            for (int32_t dx = 0; dx < 2; dx++)
            {
                int32_t child_x = l1_x * 2 + dx;
                int32_t child_y = l1_y * 2 + dy;
                int32_t child_z = l1_z * 2 + dz;
                int32_t child_bit = child_x + child_y * CHUNK_MIP0_SIZE + child_z * CHUNK_MIP0_SIZE * CHUNK_MIP0_SIZE;
                if (chunk->occupancy.level0 & (1ULL << child_bit))
                {
                    l1_has_solid = true;
                    break;
                }
            }
        }
    }

    int32_t l1_bit = l1_x + l1_y * CHUNK_MIP1_SIZE + l1_z * CHUNK_MIP1_SIZE * CHUNK_MIP1_SIZE;
    if (l1_has_solid)
    {
        chunk->occupancy.level1 |= (1 << l1_bit);
    }
    else
    {
        chunk->occupancy.level1 &= ~(1 << l1_bit);
    }
}

void chunk_update_occupancy_range(Chunk *chunk, int32_t x0, int32_t y0, int32_t z0,
                                  int32_t x1, int32_t y1, int32_t z1)
{
    /* Clamp to chunk bounds */
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (z0 < 0)
        z0 = 0;
    if (x1 >= CHUNK_SIZE)
        x1 = CHUNK_SIZE - 1;
    if (y1 >= CHUNK_SIZE)
        y1 = CHUNK_SIZE - 1;
    if (z1 >= CHUNK_SIZE)
        z1 = CHUNK_SIZE - 1;

    /* Find affected 8x8x8 regions */
    int32_t region_x0 = x0 / 8;
    int32_t region_y0 = y0 / 8;
    int32_t region_z0 = z0 / 8;
    int32_t region_x1 = x1 / 8;
    int32_t region_y1 = y1 / 8;
    int32_t region_z1 = z1 / 8;

    /* Update each affected region */
    for (int32_t rz = region_z0; rz <= region_z1; rz++)
    {
        for (int32_t ry = region_y0; ry <= region_y1; ry++)
        {
            for (int32_t rx = region_x0; rx <= region_x1; rx++)
            {
                chunk_update_occupancy_region(chunk, rx, ry, rz);
            }
        }
    }

    chunk->occupancy.has_any = (chunk->occupancy.solid_count > 0) ? 1 : 0;
}

void chunk_fill(Chunk *chunk, uint8_t material)
{
    for (int32_t i = 0; i < CHUNK_VOXEL_COUNT; i++)
    {
        chunk->voxels[i].material = material;
    }

    if (material == MATERIAL_EMPTY)
    {
        chunk->occupancy.level0 = 0;
        chunk->occupancy.level1 = 0;
        chunk->occupancy.has_any = 0;
        chunk->occupancy.solid_count = 0;
    }
    else
    {
        chunk->occupancy.level0 = 0xFFFFFFFFFFFFFFFFULL;
        chunk->occupancy.level1 = 0xFF;
        chunk->occupancy.has_any = 1;
        chunk->occupancy.solid_count = CHUNK_VOXEL_COUNT;
    }

    if (chunk->state == CHUNK_STATE_ACTIVE)
    {
        chunk->state = CHUNK_STATE_DIRTY;
    }
}

int32_t chunk_fill_sphere(Chunk *chunk, float cx, float cy, float cz, float radius, uint8_t material)
{
    int32_t modified = 0;
    float radius_sq = radius * radius;

    int32_t min_x = (int32_t)floorf(cx - radius);
    int32_t max_x = (int32_t)ceilf(cx + radius);
    int32_t min_y = (int32_t)floorf(cy - radius);
    int32_t max_y = (int32_t)ceilf(cy + radius);
    int32_t min_z = (int32_t)floorf(cz - radius);
    int32_t max_z = (int32_t)ceilf(cz + radius);

    /* Track actual bounds of modified voxels for incremental occupancy update */
    int32_t actual_min_x = CHUNK_SIZE, actual_max_x = -1;
    int32_t actual_min_y = CHUNK_SIZE, actual_max_y = -1;
    int32_t actual_min_z = CHUNK_SIZE, actual_max_z = -1;

    for (int32_t z = min_z; z <= max_z; z++)
    {
        if (z < 0 || z >= CHUNK_SIZE)
            continue;
        float dz = (float)z + 0.5f - cz;

        for (int32_t y = min_y; y <= max_y; y++)
        {
            if (y < 0 || y >= CHUNK_SIZE)
                continue;
            float dy = (float)y + 0.5f - cy;

            for (int32_t x = min_x; x <= max_x; x++)
            {
                if (x < 0 || x >= CHUNK_SIZE)
                    continue;
                float dx = (float)x + 0.5f - cx;

                if (dx * dx + dy * dy + dz * dz <= radius_sq)
                {
                    int32_t idx = chunk_voxel_index(x, y, z);
                    if (chunk->voxels[idx].material != material)
                    {
                        uint8_t old_mat = chunk->voxels[idx].material;
                        chunk->voxels[idx].material = material;
                        modified++;

                        if (old_mat == MATERIAL_EMPTY && material != MATERIAL_EMPTY)
                        {
                            chunk->occupancy.solid_count++;
                        }
                        else if (old_mat != MATERIAL_EMPTY && material == MATERIAL_EMPTY)
                        {
                            chunk->occupancy.solid_count--;
                        }

                        if (x < actual_min_x)
                            actual_min_x = x;
                        if (x > actual_max_x)
                            actual_max_x = x;
                        if (y < actual_min_y)
                            actual_min_y = y;
                        if (y > actual_max_y)
                            actual_max_y = y;
                        if (z < actual_min_z)
                            actual_min_z = z;
                        if (z > actual_max_z)
                            actual_max_z = z;
                    }
                }
            }
        }
    }

    if (modified > 0)
    {
        /* Incremental occupancy update for affected regions only */
        chunk_update_occupancy_range(chunk, actual_min_x, actual_min_y, actual_min_z,
                                     actual_max_x, actual_max_y, actual_max_z);
        if (chunk->state == CHUNK_STATE_ACTIVE)
        {
            chunk->state = CHUNK_STATE_DIRTY;
        }
    }

    return modified;
}

int32_t chunk_fill_box(Chunk *chunk, int32_t x0, int32_t y0, int32_t z0,
                       int32_t x1, int32_t y1, int32_t z1, uint8_t material)
{
    int32_t modified = 0;

    /* Clamp to chunk bounds */
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (z0 < 0)
        z0 = 0;
    if (x1 >= CHUNK_SIZE)
        x1 = CHUNK_SIZE - 1;
    if (y1 >= CHUNK_SIZE)
        y1 = CHUNK_SIZE - 1;
    if (z1 >= CHUNK_SIZE)
        z1 = CHUNK_SIZE - 1;

    for (int32_t z = z0; z <= z1; z++)
    {
        for (int32_t y = y0; y <= y1; y++)
        {
            for (int32_t x = x0; x <= x1; x++)
            {
                int32_t idx = chunk_voxel_index(x, y, z);
                if (chunk->voxels[idx].material != material)
                {
                    uint8_t old_mat = chunk->voxels[idx].material;
                    chunk->voxels[idx].material = material;
                    modified++;

                    if (old_mat == MATERIAL_EMPTY && material != MATERIAL_EMPTY)
                    {
                        chunk->occupancy.solid_count++;
                    }
                    else if (old_mat != MATERIAL_EMPTY && material == MATERIAL_EMPTY)
                    {
                        chunk->occupancy.solid_count--;
                    }
                }
            }
        }
    }

    if (modified > 0)
    {
        /* Incremental occupancy update for affected regions only */
        chunk_update_occupancy_range(chunk, x0, y0, z0, x1, y1, z1);
        if (chunk->state == CHUNK_STATE_ACTIVE)
        {
            chunk->state = CHUNK_STATE_DIRTY;
        }
    }

    return modified;
}
