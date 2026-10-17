#ifndef PATCH_TEST_FIXTURES_H
#define PATCH_TEST_FIXTURES_H

/* Test-only world setup on top of the engine's public API: volumes sized by world bounds,
 * box and sphere fills, box-shaped objects. The game builds volumes with
 * volume_create_dims, edits them through edit batches and spawns objects from voxels. */

#include "engine/core/math.h"
#include "engine/voxel/volume.h"
#include "engine/voxel/voxel_object.h"
#include <math.h>

/* chunks_x * chunks_y * chunks_z chunks over bounds, with the largest cubic voxels that fit */
static inline VoxelVolume *fixture_volume_create(int32_t chunks_x, int32_t chunks_y, int32_t chunks_z, Bounds3D bounds)
{
    float vs_x = (bounds.max_x - bounds.min_x) / (float)(chunks_x * CHUNK_SIZE);
    float vs_y = (bounds.max_y - bounds.min_y) / (float)(chunks_y * CHUNK_SIZE);
    float vs_z = (bounds.max_z - bounds.min_z) / (float)(chunks_z * CHUNK_SIZE);
    return volume_create_dims(chunks_x, chunks_y, chunks_z, vec3_create(bounds.min_x, bounds.min_y, bounds.min_z),
                              minf(vs_x, minf(vs_y, vs_z)));
}

/* Voxel range [*first, *last] of the voxels overlapping [lo, hi) on one axis, clipped to the
 * volume's voxel_count voxels; false when empty */
static inline bool fixture_voxel_span(float lo, float hi, float vol_min, float voxel_size, int32_t voxel_count,
                                      int32_t *first, int32_t *last)
{
    *first = (int32_t)fmaxf(floorf((lo - vol_min) / voxel_size), 0.0f);
    *last = (int32_t)fminf(ceilf((hi - vol_min) / voxel_size) - 1.0f, (float)(voxel_count - 1));
    return *first <= *last;
}

/* volume_edit_set on volume voxel (gx, gy, gz), through its centre */
static inline void fixture_edit_voxel(VoxelVolume *vol, int32_t gx, int32_t gy, int32_t gz, uint8_t material)
{
    volume_edit_set(vol, volume_voxel_to_world(vol, gx, gy, gz), material);
}

/* Writes the voxels of a fill. Inside an open edit batch they are edits of that batch;
 * where the batch's edit budget runs out the fill goes on in a new batch, as a game
 * spreads a big edit over several ticks. Outside a batch the fill is a batch of its own
 * which, like a generator's write, is not queued for connectivity analysis. */
typedef struct
{
    VoxelVolume *vol;
    bool own_batch;
    int32_t changed;
} FixtureFill;

static inline FixtureFill fixture_fill_begin(VoxelVolume *vol)
{
    FixtureFill fill = {vol, !vol->edit_batch_active, 0};
    if (fill.own_batch)
    {
        volume_edit_begin(vol);
        vol->edit_budget_bypass = true;
    }
    return fill;
}

static inline void fixture_fill_voxel(FixtureFill *fill, int32_t gx, int32_t gy, int32_t gz, uint8_t material)
{
    VoxelVolume *vol = fill->vol;
    const Chunk *chunk = &vol->chunks[volume_voxel_chunk_index(vol, gx, gy, gz)];
    if (chunk_get(chunk, gx & CHUNK_SIZE_MASK, gy & CHUNK_SIZE_MASK, gz & CHUNK_SIZE_MASK) == material)
        return;
    if (!vol->edit_budget_bypass && vol->edit_count >= VOLUME_MAX_EDITS_PER_TICK)
    {
        volume_edit_end(vol);
        volume_edit_begin(vol);
    }
    fixture_edit_voxel(vol, gx, gy, gz, material);
    fill->changed++;
}

/* Returns the voxels changed */
static inline int32_t fixture_fill_end(FixtureFill *fill)
{
    if (fill->own_batch)
        volume_edit_end(fill->vol);
    return fill->changed;
}

/* Every voxel overlapping [min_corner, max_corner) */
static inline int32_t fixture_fill_box(VoxelVolume *vol, Vec3 min_corner, Vec3 max_corner, uint8_t material)
{
    int32_t x0, x1, y0, y1, z0, z1;
    float vs = vol->voxel_size;
    if (!fixture_voxel_span(min_corner.x, max_corner.x, vol->bounds.min_x, vs, vol->chunks_x * CHUNK_SIZE, &x0, &x1) ||
        !fixture_voxel_span(min_corner.y, max_corner.y, vol->bounds.min_y, vs, vol->chunks_y * CHUNK_SIZE, &y0, &y1) ||
        !fixture_voxel_span(min_corner.z, max_corner.z, vol->bounds.min_z, vs, vol->chunks_z * CHUNK_SIZE, &z0, &z1))
        return 0;

    FixtureFill fill = fixture_fill_begin(vol);
    for (int32_t z = z0; z <= z1; z++)
        for (int32_t y = y0; y <= y1; y++)
            for (int32_t x = x0; x <= x1; x++)
                fixture_fill_voxel(&fill, x, y, z, material);
    return fixture_fill_end(&fill);
}

/* Every voxel whose centre lies within radius of center */
static inline int32_t fixture_fill_sphere(VoxelVolume *vol, Vec3 center, float radius, uint8_t material)
{
    int32_t x0, x1, y0, y1, z0, z1;
    float vs = vol->voxel_size;
    if (!fixture_voxel_span(center.x - radius, center.x + radius, vol->bounds.min_x, vs, vol->chunks_x * CHUNK_SIZE, &x0, &x1) ||
        !fixture_voxel_span(center.y - radius, center.y + radius, vol->bounds.min_y, vs, vol->chunks_y * CHUNK_SIZE, &y0, &y1) ||
        !fixture_voxel_span(center.z - radius, center.z + radius, vol->bounds.min_z, vs, vol->chunks_z * CHUNK_SIZE, &z0, &z1))
        return 0;

    /* Centre and radius in voxels */
    float cx = (center.x - vol->bounds.min_x) / vs;
    float cy = (center.y - vol->bounds.min_y) / vs;
    float cz = (center.z - vol->bounds.min_z) / vs;
    float r = radius / vs;

    FixtureFill fill = fixture_fill_begin(vol);
    for (int32_t z = z0; z <= z1; z++)
    {
        float dz = (float)z + 0.5f - cz;
        for (int32_t y = y0; y <= y1; y++)
        {
            float dy = (float)y + 0.5f - cy;
            for (int32_t x = x0; x <= x1; x++)
            {
                float dx = (float)x + 0.5f - cx;
                if (dx * dx + dy * dy + dz * dz <= r * r)
                    fixture_fill_voxel(&fill, x, y, z, material);
            }
        }
    }
    return fixture_fill_end(&fill);
}

/* An object of the grid voxels whose centres lie within half_extents of position (its grid
 * centre). Returns the slot, -1 when the world is full. */
static inline int32_t fixture_add_box(VoxelObjectWorld *world, Vec3 position, Vec3 half_extents, uint8_t material)
{
    int32_t slot = voxel_object_world_alloc_slot(world);
    if (slot < 0)
        return -1;

    VoxelObject *obj = &world->objects[slot];
    obj->position = position;
    obj->orientation = quat_identity();
    obj->active = true;
    obj->voxel_size = world->voxel_size;

    float half_grid = (float)VOBJ_GRID_SIZE * 0.5f;
    for (int32_t z = 0; z < VOBJ_GRID_SIZE; z++)
    {
        for (int32_t y = 0; y < VOBJ_GRID_SIZE; y++)
        {
            for (int32_t x = 0; x < VOBJ_GRID_SIZE; x++)
            {
                float dx = ((float)x - half_grid + 0.5f) * obj->voxel_size;
                float dy = ((float)y - half_grid + 0.5f) * obj->voxel_size;
                float dz = ((float)z - half_grid + 0.5f) * obj->voxel_size;
                if (fabsf(dx) <= half_extents.x && fabsf(dy) <= half_extents.y && fabsf(dz) <= half_extents.z)
                {
                    obj->voxels[vobj_index(x, y, z)].material = material;
                    obj->voxel_count++;
                }
            }
        }
    }

    voxel_object_recalc_shape(obj);
    return slot;
}

#endif
