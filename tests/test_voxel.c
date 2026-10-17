#include "engine/core/types.h"
#include "engine/core/math.h"
#include "engine/core/rng.h"
#include "engine/voxel/volume.h"
#include "engine/voxel/voxel_object.h"
#include "engine/voxel/bvh.h"
#include "content/materials.h"
#include "test_common.h"
#include "test_fixtures.h"
#include <math.h>
#include <string.h>

/* hash_volume_state after volume_edits_golden's edits */
#define VOLUME_EDITS_GOLDEN 0x880dad2a638c524fULL

TEST(volume_create_destroy)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 32.0f, -16.0f, 16.0f};
    VoxelVolume *vol = fixture_volume_create(2, 2, 2, bounds);
    ASSERT(vol != NULL);
    ASSERT(vol->chunks_x == 2);
    ASSERT(vol->chunks_y == 2);
    ASSERT(vol->chunks_z == 2);
    volume_destroy(vol);
    return 1;
}

/* FNV-1a over every voxel, the occupancy of every chunk and the volume counters */
static uint64_t hash_volume_state(const VoxelVolume *vol)
{
    uint64_t h = 1469598103934665603ULL;
#define HASH_BYTE(b) (h = (h ^ (uint8_t)(b)) * 1099511628211ULL)
    for (int32_t c = 0; c < vol->total_chunks; c++)
    {
        const Chunk *chunk = &vol->chunks[c];
        for (int32_t v = 0; v < CHUNK_VOXEL_COUNT; v++)
            HASH_BYTE(chunk->voxels[v].material);
        for (int32_t b = 0; b < 8; b++)
            HASH_BYTE(chunk->occupancy.level0 >> (b * 8));
        HASH_BYTE(chunk->occupancy.level1);
        HASH_BYTE(chunk->occupancy.solid_count);
        HASH_BYTE(chunk->occupancy.solid_count >> 8);
    }
    for (int32_t b = 0; b < 4; b++)
    {
        HASH_BYTE((uint32_t)vol->total_solid_voxels >> (b * 8));
        HASH_BYTE((uint32_t)vol->active_chunks >> (b * 8));
    }
#undef HASH_BYTE
    return h;
}

TEST(volume_edits_golden)
{
    /* Batches of random voxel edits (places and removals, past the per-batch budget)
     * end in one exact state; any change to what the edits do shows up here */
    VoxelVolume *vol = volume_create_dims(2, 2, 2, vec3_zero(), 0.5f);
    ASSERT(vol != NULL);
    RngState rng;
    rng_seed(&rng, 0x5EED);
    int32_t applied = 0;
    for (int32_t batch = 0; batch < 6; batch++)
    {
        volume_edit_begin(vol);
        for (int32_t i = 0; i < 8000; i++)
        {
            int32_t x = (int32_t)rng_range_u32(&rng, 64);
            int32_t y = (int32_t)rng_range_u32(&rng, 24);
            int32_t z = (int32_t)rng_range_u32(&rng, 64);
            uint8_t mat = rng_range_u32(&rng, 3) == 0 ? MATERIAL_EMPTY : (uint8_t)(1 + rng_range_u32(&rng, 4));
            fixture_edit_voxel(vol, x, y, z, mat);
        }
        applied += volume_edit_end(vol);
    }
    uint64_t h = hash_volume_state(vol);
    printf("(%d edits, state 0x%016llx) ", applied, (unsigned long long)h);
    ASSERT(applied == 6 * VOLUME_MAX_EDITS_PER_TICK);
    ASSERT(h == VOLUME_EDITS_GOLDEN);

    volume_destroy(vol);
    return 1;
}

TEST(world_voxel_mapping_at_chunk_borders)
{
    /* Chunk and local coordinates came from two separately rounded divisions: within
     * an ulp of a chunk border they disagreed and gave local -1, so solid voxels read
     * as empty there (scene 0: x = -48.0000038, y = 28.7999992). Every float within
     * 64 ulps of every chunk border, on every axis, must land in its own voxel. */
    VoxelVolume *vol = volume_create_dims(12, 12, 3, vec3_create(-76.8f, 0.0f, -76.8f), 0.1f);
    ASSERT(vol != NULL);

    /* The probes run along three voxel lines through (x, y, z) = (-70.05, 1.05, -70.05),
     * voxel (67, 10, 67): give their voxels distinct materials */
    const int32_t line[3] = {67, 10, 67};
    const int32_t dims[3] = {vol->chunks_x * CHUNK_SIZE, vol->chunks_y * CHUNK_SIZE, vol->chunks_z * CHUNK_SIZE};
    volume_edit_begin(vol);
    vol->edit_budget_bypass = true;
    for (int32_t axis = 0; axis < 3; axis++)
    {
        for (int32_t i = 0; i < dims[axis]; i++)
        {
            int32_t g[3] = {line[0], line[1], line[2]};
            g[axis] = i;
            fixture_edit_voxel(vol, g[0], g[1], g[2], (uint8_t)(1 + ((g[0] + g[1] + g[2]) & 3)));
        }
    }
    volume_edit_end(vol);

    const float origin[3] = {vol->bounds.min_x, vol->bounds.min_y, vol->bounds.min_z};
    int32_t checked = 0;
    for (int32_t axis = 0; axis < 3; axis++)
    {
        for (int32_t border = CHUNK_SIZE; border < dims[axis]; border += CHUNK_SIZE)
        {
            float v = origin[axis] + (float)border * vol->voxel_size;
            for (int32_t i = 0; i < 64; i++)
                v = nextafterf(v, -INFINITY);
            int32_t prev_g = border - 1;
            for (int32_t i = 0; i < 128; i++, v = nextafterf(v, INFINITY))
            {
                float p[3] = {-70.05f, 1.05f, -70.05f};
                p[axis] = v;
                Vec3 pos = vec3_create(p[0], p[1], p[2]);
                int32_t g[3];
                ASSERT(volume_world_to_voxel(vol, pos, &g[0], &g[1], &g[2]));
                ASSERT(g[(axis + 1) % 3] == line[(axis + 1) % 3] && g[(axis + 2) % 3] == line[(axis + 2) % 3]);

                /* One side of the border or the other, never going back, and the voxel
                 * holds the point up to the rounding of its bounds */
                ASSERT(g[axis] == border - 1 || g[axis] == border);
                ASSERT(g[axis] >= prev_g);
                prev_g = g[axis];
                ASSERT(v >= origin[axis] + (float)g[axis] * vol->voxel_size - 1e-5f);
                ASSERT(v <= origin[axis] + (float)(g[axis] + 1) * vol->voxel_size + 1e-5f);

                /* Reads there see that voxel */
                ASSERT(volume_get_at(vol, pos) == (uint8_t)(1 + ((g[0] + g[1] + g[2]) & 3)));
                checked++;
            }
        }
    }

    /* The two scene-0 values from the review */
    int32_t gx, gy, gz;
    ASSERT(volume_world_to_voxel(vol, vec3_create(-48.0000038f, 1.05f, -70.05f), &gx, &gy, &gz));
    ASSERT(gx == 287 || gx == 288);
    ASSERT(volume_get_at(vol, vec3_create(-48.0000038f, 1.05f, -70.05f)) == (uint8_t)(1 + ((gx + gy + gz) & 3)));
    ASSERT(volume_world_to_voxel(vol, vec3_create(-70.05f, 28.7999992f, -70.05f), &gx, &gy, &gz));
    ASSERT(gy == 287 || gy == 288);

    /* An edit there changes exactly the voxel a read there sees */
    volume_edit_begin(vol);
    volume_edit_set(vol, vec3_create(-48.0000038f, 1.05f, -70.05f), MAT_WATER);
    ASSERT(volume_edit_end(vol) == 1);
    ASSERT(volume_get_at(vol, vec3_create(-48.0000038f, 1.05f, -70.05f)) == MAT_WATER);
    printf("(%d points) ", checked);

    volume_destroy(vol);
    return 1;
}

TEST(edit_outside_volume_changes_nothing)
{
    /* An edit that lands on no voxel must leave every counter and the analysis queue
     * alone: one at a chunk-border local -1 used to count a solid voxel it never wrote
     * and queue a seed that unpacked to chunk (63,31,63), read out of bounds */
    VoxelVolume *vol = volume_create_dims(2, 2, 2, vec3_create(-3.2f, 0.0f, -3.2f), 0.1f);
    ASSERT(vol != NULL);
    const Vec3 outside[] = {
        {-3.3f, 1.0f, 0.0f}, {3.2f, 1.0f, 0.0f}, {0.0f, -0.01f, 0.0f}, {0.0f, 6.4f, 0.0f},
        {0.0f, 1.0f, 3.25f}, {NAN, 1.0f, 0.0f}, {0.0f, INFINITY, 0.0f}, {0.0f, 1.0f, -INFINITY},
    };
    volume_edit_begin(vol);
    for (size_t i = 0; i < sizeof(outside) / sizeof(outside[0]); i++)
    {
        volume_edit_set(vol, outside[i], MAT_STONE);
        volume_set_at(vol, outside[i], MAT_STONE);
        ASSERT(volume_get_at(vol, outside[i]) == MATERIAL_EMPTY);
    }
    ASSERT(vol->edit_count == 0 && vol->cut_boundary.count == 0 && !vol->edit_may_detach);
    ASSERT(volume_edit_end(vol) == 0);
    ASSERT(vol->total_solid_voxels == 0 && vol->active_chunks == 0);
    ASSERT(vol->pending_analysis_count == 0 && vol->pending_cut_boundary.count == 0);

    volume_destroy(vol);
    return 1;
}

TEST(edit_batch_occupancy_exact_at_end)
{
    /* Inside a batch an emptied voxel only marks its 8^3 region stale (rescanned once by
     * volume_edit_end, instead of after every removal). Occupancy bits may stay set for
     * an emptied region until then, never be missing for a solid one, and after the
     * batch everything equals a full rebuild. */
    VoxelVolume *vol = volume_create_dims(2, 2, 2, vec3_zero(), 0.1f);
    ASSERT(vol != NULL);
    static Chunk rebuilt;
    RngState rng;
    rng_seed(&rng, 77);
    for (int32_t round = 0; round < 12; round++)
    {
        /* Random edits in a 20^3 box across chunk and region borders, alternately dense
         * and sparse; every third round clears the box so whole regions empty out */
        uint8_t fill_bias = (round % 2 == 0) ? 3 : 1;
        volume_edit_begin(vol);
        vol->edit_budget_bypass = true;
        for (int32_t i = 0; i < 20000; i++)
        {
            int32_t x = 22 + (int32_t)rng_range_u32(&rng, 20);
            int32_t y = 22 + (int32_t)rng_range_u32(&rng, 20);
            int32_t z = 22 + (int32_t)rng_range_u32(&rng, 20);
            uint8_t mat = rng_range_u32(&rng, 4) < fill_bias ? (uint8_t)(1 + rng_range_u32(&rng, 3)) : MATERIAL_EMPTY;
            if (round % 3 == 2)
                mat = MATERIAL_EMPTY;
            fixture_edit_voxel(vol, x, y, z, mat);
        }
        if (round % 3 == 2)
            for (int32_t z = 22; z < 42; z++)
                for (int32_t y = 22; y < 42; y++)
                    for (int32_t x = 22; x < 42; x++)
                        fixture_edit_voxel(vol, x, y, z, MATERIAL_EMPTY);
        for (int32_t c = 0; c < vol->total_chunks; c++)
        {
            rebuilt = vol->chunks[c];
            chunk_rebuild_occupancy(&rebuilt);
            ASSERT((vol->chunks[c].occupancy.level0 & rebuilt.occupancy.level0) == rebuilt.occupancy.level0);
            ASSERT(vol->chunks[c].occupancy.solid_count == rebuilt.occupancy.solid_count);
        }
        volume_edit_end(vol);

        int32_t solid = 0, active = 0;
        for (int32_t c = 0; c < vol->total_chunks; c++)
        {
            rebuilt = vol->chunks[c];
            chunk_rebuild_occupancy(&rebuilt);
            ASSERT(vol->chunks[c].occupancy.level0 == rebuilt.occupancy.level0);
            ASSERT(vol->chunks[c].occupancy.level1 == rebuilt.occupancy.level1);
            ASSERT(vol->chunks[c].occupancy.has_any == rebuilt.occupancy.has_any);
            ASSERT(vol->chunks[c].stale_regions == 0);
            solid += rebuilt.occupancy.solid_count;
            active += rebuilt.occupancy.has_any;
        }
        ASSERT(vol->total_solid_voxels == solid && vol->active_chunks == active);
    }

    volume_destroy(vol);
    return 1;
}

TEST(volume_create_clamps_counts_and_bounds)
{
    /* Bounds were computed from the requested chunk counts, then the counts clamped */
    VoxelVolume *vol = volume_create_dims(VOLUME_MAX_CHUNKS_X + 6, 1, 2, vec3_create(-1.0f, 2.0f, 3.0f), 0.25f);
    ASSERT(vol != NULL);
    ASSERT(vol->chunks_x == VOLUME_MAX_CHUNKS_X && vol->chunks_y == 1 && vol->chunks_z == 2);
    ASSERT(vol->total_chunks == VOLUME_MAX_CHUNKS_X * 2);
    ASSERT(vol->bounds.max_x == -1.0f + (float)(VOLUME_MAX_CHUNKS_X * CHUNK_SIZE) * 0.25f);
    ASSERT(vol->bounds.max_y == 2.0f + (float)CHUNK_SIZE * 0.25f);
    ASSERT(vol->bounds.max_z == 3.0f + (float)(2 * CHUNK_SIZE) * 0.25f);

    /* A fresh volume is empty, current on the GPU and exact without a pass over it */
    for (int32_t c = 0; c < vol->total_chunks; c++)
        ASSERT(vol->chunks[c].state == CHUNK_STATE_ACTIVE && !vol->chunks[c].occupancy.has_any);
    volume_destroy(vol);

    ASSERT(volume_create_dims(0, 1, 1, vec3_zero(), 0.1f) == NULL);
    ASSERT(volume_create_dims(1, 1, 1, vec3_zero(), 0.0f) == NULL);
    return 1;
}

TEST(set_at_marks_shadow_dirty)
{
    /* volume_set_at (and the old fills) changed voxels without telling the shadow
     * volume, which kept the old shadows there */
    VoxelVolume *vol = volume_create_dims(4, 1, 4, vec3_zero(), 0.1f);
    ASSERT(vol != NULL);

    /* Before the renderer's first (full) pack nothing is tracked: it packs everything */
    volume_set_at(vol, vec3_create(0.05f, 0.05f, 0.05f), MAT_STONE);
    int32_t indices[VOLUME_SHADOW_DIRTY_MAX];
    ASSERT(volume_get_shadow_dirty_chunks(vol, indices, VOLUME_SHADOW_DIRTY_MAX) == 0);
    volume_clear_shadow_dirty(vol);

    volume_set_at(vol, vec3_create(3.25f, 0.05f, 6.45f), MAT_STONE);
    volume_set_at(vol, vec3_create(3.35f, 0.05f, 6.45f), MAT_DIRT);
    ASSERT(volume_get_shadow_dirty_chunks(vol, indices, VOLUME_SHADOW_DIRTY_MAX) == 1);
    ASSERT(indices[0] == volume_chunk_index(vol, 1, 0, 2));
    volume_clear_shadow_dirty(vol);
    ASSERT(volume_get_shadow_dirty_chunks(vol, indices, VOLUME_SHADOW_DIRTY_MAX) == 0);

    volume_destroy(vol);
    return 1;
}

TEST(removal_log_records_emptied_boxes)
{
    /* Batches and volume_set_at that empty voxels log their box, for bodies resting there */
    VoxelVolume *vol = volume_create_dims(2, 2, 2, vec3_zero(), 0.1f);
    ASSERT(vol != NULL);
    fixture_fill_box(vol, vec3_zero(), vec3_create(6.4f, 1.0f, 6.4f), MAT_STONE);
    ASSERT(vol->removal_count == 0);

    volume_edit_begin(vol);
    fixture_edit_voxel(vol, 3, 9, 40, MATERIAL_EMPTY);
    fixture_edit_voxel(vol, 7, 5, 33, MATERIAL_EMPTY);
    fixture_edit_voxel(vol, 20, 20, 20, MAT_STONE); /* placing logs nothing */
    volume_edit_end(vol);
    ASSERT(vol->removal_count == 1);
    const VoxelBox *box = &vol->removal_log[0];
    ASSERT(box->min_x == 3 && box->min_y == 5 && box->min_z == 33);
    ASSERT(box->max_x == 7 && box->max_y == 9 && box->max_z == 40);

    volume_edit_begin(vol);
    fixture_edit_voxel(vol, 1, 1, 1, MAT_DIRT); /* solid to solid: nothing emptied */
    volume_edit_end(vol);
    ASSERT(vol->removal_count == 1);

    volume_set_at(vol, volume_voxel_to_world(vol, 10, 2, 11), MATERIAL_EMPTY);
    ASSERT(vol->removal_count == 2);
    box = &vol->removal_log[1];
    ASSERT(box->min_x == 10 && box->max_x == 10 && box->min_y == 2 && box->max_y == 2 && box->min_z == 11 && box->max_z == 11);

    /* The log is a ring: entry n lives at n % VOLUME_REMOVAL_LOG_SIZE */
    for (int32_t i = 0; i < VOLUME_REMOVAL_LOG_SIZE; i++)
        volume_set_at(vol, volume_voxel_to_world(vol, i, 0, 0), MATERIAL_EMPTY);
    ASSERT(vol->removal_count == 2 + VOLUME_REMOVAL_LOG_SIZE);
    box = &vol->removal_log[(vol->removal_count - 1) % VOLUME_REMOVAL_LOG_SIZE];
    ASSERT(box->min_x == VOLUME_REMOVAL_LOG_SIZE - 1);

    volume_destroy(vol);
    return 1;
}

/* Floor of stone up to y = 1 m, a dirt wall from x = 2.0 to 2.5 and a water pool 0.5 m
 * deep on the floor at x in [-2, -1): 4x1x4 chunks of 0.1 m from (-6.4, 0, -6.4). The
 * fills name voxel centres, so no bound lies on a voxel face. */
static VoxelVolume *raycast_scene(void)
{
    VoxelVolume *vol = volume_create_dims(4, 1, 4, vec3_create(-6.4f, 0.0f, -6.4f), 0.1f);
    fixture_fill_box(vol, vec3_create(-6.35f, 0.05f, -6.35f), vec3_create(6.35f, 0.95f, 6.35f), MAT_STONE);
    fixture_fill_box(vol, vec3_create(2.05f, 1.05f, -6.35f), vec3_create(2.45f, 2.95f, 6.35f), MAT_DIRT);
    fixture_fill_box(vol, vec3_create(-1.95f, 1.05f, -6.35f), vec3_create(-1.05f, 1.45f, 6.35f), MAT_WATER);
    return vol;
}

TEST(raycast_distance_normal_and_material)
{
    VoxelVolume *vol = raycast_scene();
    ASSERT(vol != NULL);
    Vec3 hit, normal;
    uint8_t mat = 0;

    /* Along +x onto the wall face: distance to the face, normal against the ray */
    float d = volume_raycast(vol, vec3_create(0.33f, 2.0f, 0.21f), vec3_create(1.0f, 0.0f, 0.0f), 10.0f,
                             &hit, &normal, &mat, 0);
    ASSERT_NEAR(d, 2.0f - 0.33f, 1e-4f);
    ASSERT_NEAR(hit.x, 2.0f, 1e-4f);
    ASSERT(normal.x == -1.0f && normal.y == 0.0f && normal.z == 0.0f);
    ASSERT(mat == MAT_DIRT);

    /* Straight down onto the floor */
    d = volume_raycast(vol, vec3_create(0.27f, 3.0f, -0.42f), vec3_create(0.0f, -1.0f, 0.0f), 10.0f,
                       &hit, &normal, &mat, 0);
    ASSERT_NEAR(d, 2.0f, 1e-4f);
    ASSERT(normal.y == 1.0f && normal.x == 0.0f && normal.z == 0.0f);
    ASSERT(mat == MAT_STONE);

    /* Diagonal: the hit lies on the ray and on the floor plane */
    Vec3 dir = vec3_normalize(vec3_create(0.3f, -1.0f, 0.2f));
    Vec3 origin = vec3_create(-0.5f, 2.5f, 0.0f);
    d = volume_raycast(vol, origin, dir, 10.0f, &hit, &normal, &mat, 0);
    ASSERT_NEAR(hit.y, 1.0f, 1e-4f);
    ASSERT_NEAR(d, 1.5f / -dir.y, 1e-4f);
    ASSERT_NEAR(hit.x, origin.x + dir.x * d, 1e-4f);

    /* From outside the volume into it */
    d = volume_raycast(vol, vec3_create(-8.0f, 0.5f, 0.05f), vec3_create(1.0f, 0.0f, 0.0f), 10.0f,
                       &hit, &normal, &mat, 0);
    ASSERT_NEAR(d, 1.6f, 1e-4f);
    ASSERT(normal.x == -1.0f && mat == MAT_STONE);

    /* Nothing along the ray */
    ASSERT(volume_raycast(vol, vec3_create(0.0f, 2.0f, 0.0f), vec3_create(0.0f, 1.0f, 0.0f), 10.0f,
                          &hit, &normal, &mat, 0) < 0.0f);

    volume_destroy(vol);
    return 1;
}

TEST(raycast_max_dist_and_skip_flags)
{
    VoxelVolume *vol = raycast_scene();
    ASSERT(vol != NULL);
    Vec3 hit, normal;
    uint8_t mat = 0;
    Vec3 origin = vec3_create(0.33f, 2.0f, 0.21f);
    Vec3 east = vec3_create(1.0f, 0.0f, 0.0f);

    /* The wall is 1.67 m away */
    ASSERT(volume_raycast(vol, origin, east, 1.6f, &hit, &normal, &mat, 0) < 0.0f);
    ASSERT_NEAR(volume_raycast(vol, origin, east, 1.7f, &hit, &normal, &mat, 0), 1.67f, 1e-4f);

    /* Down onto the pool: the water, or with liquids skipped the floor under it */
    Vec3 above_pool = vec3_create(-1.55f, 3.0f, 0.15f);
    Vec3 down = vec3_create(0.0f, -1.0f, 0.0f);
    ASSERT_NEAR(volume_raycast(vol, above_pool, down, 10.0f, &hit, &normal, &mat, 0), 1.5f, 1e-4f);
    ASSERT(mat == MAT_WATER);
    ASSERT_NEAR(volume_raycast(vol, above_pool, down, 10.0f, &hit, &normal, &mat, MAT_FLAG_LIQUID), 2.0f, 1e-4f);
    ASSERT(mat == MAT_STONE && normal.y == 1.0f);

    /* Optional outputs */
    ASSERT_NEAR(volume_raycast(vol, above_pool, down, 10.0f, NULL, NULL, NULL, 0), 1.5f, 1e-4f);

    volume_destroy(vol);
    return 1;
}

TEST(volume_dirty_tracking)
{
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 32.0f, -16.0f, 16.0f};
    VoxelVolume *vol = fixture_volume_create(2, 2, 2, bounds);
    ASSERT(vol != NULL);

    volume_begin_frame(vol);

    volume_edit_begin(vol);
    volume_edit_set(vol, vec3_create(0.5f, 0.5f, 0.5f), MAT_STONE);
    volume_edit_end(vol);

    ASSERT(vol->pending_analysis_count == 1);

    volume_destroy(vol);
    return 1;
}

TEST(chunk_set_occupancy_matches_rebuild)
{
    /* chunk_set keeps the hierarchical occupancy incrementally (filling sets bits,
     * clearing rescans the region); it must always equal a full rebuild */
    static Chunk chunk, rebuilt; /* zeroed: an empty chunk */
    RngState rng;
    rng_seed(&rng, 0xC0FFEE);

    for (int32_t round = 0; round < 40; round++)
    {
        /* Alternate dense filling and dense clearing so regions empty out again */
        uint8_t fill_bias = (round % 2 == 0) ? 3 : 1;
        for (int32_t i = 0; i < 4000; i++)
        {
            int32_t x = (int32_t)rng_range_u32(&rng, CHUNK_SIZE);
            int32_t y = (int32_t)rng_range_u32(&rng, CHUNK_SIZE);
            int32_t z = (int32_t)rng_range_u32(&rng, CHUNK_SIZE);
            uint8_t mat = rng_range_u32(&rng, 4) < fill_bias ? (uint8_t)(1 + rng_range_u32(&rng, 3)) : MATERIAL_EMPTY;
            chunk_set(&chunk, x, y, z, mat);
        }

        rebuilt = chunk;
        chunk_rebuild_occupancy(&rebuilt);
        ASSERT(chunk.occupancy.level0 == rebuilt.occupancy.level0);
        ASSERT(chunk.occupancy.level1 == rebuilt.occupancy.level1);
        ASSERT(chunk.occupancy.has_any == rebuilt.occupancy.has_any);
        ASSERT(chunk.occupancy.solid_count == rebuilt.occupancy.solid_count);
    }

    /* Clear everything voxel by voxel: all bits must drop */
    for (int32_t z = 0; z < CHUNK_SIZE; z++)
        for (int32_t y = 0; y < CHUNK_SIZE; y++)
            for (int32_t x = 0; x < CHUNK_SIZE; x++)
                chunk_set(&chunk, x, y, z, MATERIAL_EMPTY);
    ASSERT(chunk.occupancy.level0 == 0 && chunk.occupancy.level1 == 0);
    ASSERT(chunk.occupancy.has_any == 0 && chunk.occupancy.solid_count == 0);
    return 1;
}

TEST(active_chunk_count_follows_edits)
{
    /* active_chunks (shown in the debug overlay) started at the total chunk count and
     * only changed on a full occupancy rebuild */
    Bounds3D bounds = {-16.0f, 16.0f, 0.0f, 32.0f, -16.0f, 16.0f};
    VoxelVolume *vol = fixture_volume_create(2, 2, 2, bounds);
    ASSERT(vol != NULL);
    ASSERT(vol->active_chunks == 0);

    volume_edit_begin(vol);
    volume_edit_set(vol, vec3_create(0.5f, 0.5f, 0.5f), MAT_STONE);
    volume_edit_set(vol, vec3_create(1.5f, 0.5f, 0.5f), MAT_STONE);
    volume_edit_end(vol);
    volume_set_at(vol, vec3_create(-0.5f, 0.5f, 0.5f), MAT_STONE);
    fixture_fill_box(vol, vec3_create(-2.0f, 20.0f, -2.0f), vec3_create(-1.0f, 21.0f, -1.0f), MAT_STONE);
    ASSERT(vol->active_chunks == 3);
    ASSERT(vol->total_solid_voxels == 2 + 1 + 8);

    volume_edit_begin(vol);
    volume_edit_set(vol, vec3_create(0.5f, 0.5f, 0.5f), MAT_AIR);
    volume_edit_set(vol, vec3_create(1.5f, 0.5f, 0.5f), MAT_AIR);
    volume_edit_end(vol);
    fixture_fill_sphere(vol, vec3_create(-1.5f, 20.5f, -1.5f), 1.0f, MAT_AIR);
    ASSERT(vol->active_chunks == 1);
    ASSERT(vol->total_solid_voxels == 1);

    volume_destroy(vol);
    return 1;
}

TEST(volume_edit_batch_uploads_every_chunk)
{
    /* One edit batch touching 100 chunks: the touched-chunk list holds 64, and the
     * rest used to stay DIRTY without ever being queued for upload. */
    Bounds3D bounds = {0.0f, 32.0f, 0.0f, 3.2f, 0.0f, 32.0f};
    VoxelVolume *vol = fixture_volume_create(10, 1, 10, bounds);
    ASSERT(vol != NULL);
    ASSERT(vol->total_chunks == 100);

    float chunk_size = vol->voxel_size * CHUNK_SIZE;
    volume_edit_begin(vol);
    for (int32_t cz = 0; cz < 10; cz++)
        for (int32_t cx = 0; cx < 10; cx++)
            volume_edit_set(vol, vec3_create(((float)cx + 0.5f) * chunk_size, 1.0f,
                                             ((float)cz + 0.5f) * chunk_size), MAT_STONE);
    volume_edit_end(vol);

    bool seen[100] = {false};
    int32_t delivered = 0;
    for (int32_t frame = 0; frame < 32; frame++)
    {
        volume_begin_frame(vol);
        int32_t indices[VOLUME_MAX_DIRTY_PER_FRAME];
        int32_t count = volume_get_dirty_chunks(vol, indices, VOLUME_MAX_DIRTY_PER_FRAME);
        for (int32_t i = 0; i < count; i++)
        {
            if (indices[i] >= 0 && indices[i] < 100 && !seen[indices[i]])
            {
                seen[indices[i]] = true;
                delivered++;
            }
        }
        volume_mark_chunks_uploaded(vol, indices, count);
    }

    int32_t occupied = 0;
    for (int32_t i = 0; i < vol->total_chunks; i++)
        if (vol->chunks[i].occupancy.has_any)
            occupied++;

    printf("(delivered=%d, occupancy=%d) ", delivered, occupied);
    ASSERT(delivered == 100);
    ASSERT(occupied == 100);

    volume_destroy(vol);
    return 1;
}

/* One voxel edit in chunk (cx, cz) of a single-layer volume, as its own edit batch */
static void edit_chunk(VoxelVolume *vol, int32_t cx, int32_t cz, uint8_t material)
{
    float chunk_size = vol->voxel_size * CHUNK_SIZE;
    volume_edit_begin(vol);
    volume_edit_set(vol, vec3_create(vol->bounds.min_x + ((float)cx + 0.5f) * chunk_size, vol->bounds.min_y + 0.05f,
                                     vol->bounds.min_z + ((float)cz + 0.5f) * chunk_size),
                    material);
    volume_edit_end(vol);
}

/* Run one renderer frame: fetch the dirty chunks, check none repeats, upload them */
static int32_t upload_frame(VoxelVolume *vol, int32_t *delivered_per_chunk, bool *duplicate)
{
    volume_begin_frame(vol);
    int32_t indices[VOLUME_MAX_DIRTY_PER_FRAME];
    int32_t count = volume_get_dirty_chunks(vol, indices, VOLUME_MAX_DIRTY_PER_FRAME);
    for (int32_t i = 0; i < count; i++)
    {
        for (int32_t j = 0; j < i; j++)
            if (indices[j] == indices[i])
                *duplicate = true;
        delivered_per_chunk[indices[i]]++;
    }
    volume_mark_chunks_uploaded(vol, indices, count);
    return count;
}

TEST(dirty_chunks_unique_per_frame)
{
    /* The same chunk edited by several batches (and by volume_set_at) within one
     * frame used to enter the dirty ring once per edit, and the renderer got the
     * index several times in one frame: overlapping buffer copies. */
    Bounds3D bounds = {0.0f, 12.8f, 0.0f, 3.2f, 0.0f, 12.8f};
    VoxelVolume *vol = fixture_volume_create(4, 1, 4, bounds);
    ASSERT(vol != NULL);

    for (int32_t i = 0; i < 3; i++)
        edit_chunk(vol, 1, 2, (uint8_t)(MAT_STONE + i));
    for (int32_t i = 0; i < 5; i++)
        volume_set_at(vol, vec3_create(4.0f + 0.1f * (float)i, 0.05f, 7.0f), MAT_DIRT);

    int32_t delivered[16] = {0};
    bool duplicate = false;
    int32_t count = upload_frame(vol, delivered, &duplicate);
    printf("(%d entries) ", count);
    ASSERT(!duplicate);
    ASSERT(count == 1);
    ASSERT(delivered[1 + 2 * 4] == 1);

    volume_destroy(vol);
    return 1;
}

TEST(dirty_ring_overflow_delivers_every_chunk_once)
{
    /* Two batches touching all 100 chunks overflow the 64-entry dirty ring. While
     * the overflow is drained from the bitmap, chunks re-dirtied below the scan
     * position must still arrive (they were dropped when the drain finished), and
     * no frame may list a chunk twice. */
    Bounds3D bounds = {0.0f, 32.0f, 0.0f, 3.2f, 0.0f, 32.0f};
    VoxelVolume *vol = fixture_volume_create(10, 1, 10, bounds);
    ASSERT(vol != NULL);

    for (int32_t pass = 0; pass < 2; pass++)
    {
        volume_edit_begin(vol);
        float chunk_size = vol->voxel_size * CHUNK_SIZE;
        for (int32_t cz = 0; cz < 10; cz++)
            for (int32_t cx = 0; cx < 10; cx++)
                volume_edit_set(vol, vec3_create(((float)cx + 0.5f) * chunk_size, 0.05f + 0.1f * (float)pass,
                                                 ((float)cz + 0.5f) * chunk_size), MAT_STONE);
        volume_edit_end(vol);
    }
    ASSERT(vol->dirty_ring_overflow);

    /* Drain until the bitmap scan has left the first 64-chunk word */
    int32_t delivered[100] = {0};
    bool duplicate = false;
    for (int32_t frame = 0; frame < 32 && delivered[79] == 0; frame++)
        upload_frame(vol, delivered, &duplicate);
    ASSERT(delivered[0] == 1);
    ASSERT(vol->dirty_ring_overflow);

    /* Chunk 0 changes again after the drain has moved past it */
    edit_chunk(vol, 0, 0, MAT_DIRT);

    for (int32_t frame = 0; frame < 32; frame++)
        upload_frame(vol, delivered, &duplicate);

    int32_t stuck_dirty = 0;
    for (int32_t i = 0; i < vol->total_chunks; i++)
        if (vol->chunks[i].state == CHUNK_STATE_DIRTY)
            stuck_dirty++;
    printf("(chunk 0 delivered %d times, %d stuck dirty) ", delivered[0], stuck_dirty);

    ASSERT(!duplicate);
    ASSERT(delivered[0] == 2);
    for (int32_t i = 1; i < 100; i++)
        ASSERT(delivered[i] == 1);
    ASSERT(stuck_dirty == 0);

    volume_destroy(vol);
    return 1;
}

/* The shadow volume packed voxel by voxel over the whole volume: the reference the chunk-wise
 * packing (volume_pack_shadow_chunk) is checked against */
static void pack_shadow_volume_reference(const VoxelVolume *vol, const bool *occludes, uint8_t *out_packed,
                                         uint32_t *out_width, uint32_t *out_height, uint32_t *out_depth)
{
    if (!vol || !out_packed || !out_width || !out_height || !out_depth)
        return;

    int32_t total_voxels_x = vol->chunks_x * CHUNK_SIZE;
    int32_t total_voxels_y = vol->chunks_y * CHUNK_SIZE;
    int32_t total_voxels_z = vol->chunks_z * CHUNK_SIZE;

    uint32_t packed_w = (uint32_t)(total_voxels_x >> 1);
    uint32_t packed_h = (uint32_t)(total_voxels_y >> 1);
    uint32_t packed_d = (uint32_t)(total_voxels_z >> 1);

    *out_width = packed_w;
    *out_height = packed_h;
    *out_depth = packed_d;

    size_t packed_size = (size_t)packed_w * packed_h * packed_d;
    memset(out_packed, 0, packed_size);

    for (int32_t cz = 0; cz < vol->chunks_z; cz++)
    {
        for (int32_t cy = 0; cy < vol->chunks_y; cy++)
        {
            for (int32_t cx = 0; cx < vol->chunks_x; cx++)
            {
                int32_t chunk_idx = cx + cy * vol->chunks_x + cz * vol->chunks_x * vol->chunks_y;
                const Chunk *chunk = &vol->chunks[chunk_idx];

                if (!chunk->occupancy.has_any)
                    continue;

                int32_t base_vx = cx * CHUNK_SIZE;
                int32_t base_vy = cy * CHUNK_SIZE;
                int32_t base_vz = cz * CHUNK_SIZE;

                for (int32_t lz = 0; lz < CHUNK_SIZE; lz++)
                {
                    for (int32_t ly = 0; ly < CHUNK_SIZE; ly++)
                    {
                        for (int32_t lx = 0; lx < CHUNK_SIZE; lx++)
                        {
                            uint8_t material = chunk->voxels[chunk_voxel_index(lx, ly, lz)].material;
                            if (material == MATERIAL_EMPTY || (occludes && !occludes[material]))
                                continue;

                            int32_t vx = base_vx + lx;
                            int32_t vy = base_vy + ly;
                            int32_t vz = base_vz + lz;

                            int32_t bit_idx = (vx & 1) + ((vy & 1) << 1) + ((vz & 1) << 2);
                            int32_t px = vx >> 1;
                            int32_t py = vy >> 1;
                            int32_t pz = vz >> 1;
                            size_t packed_idx = (size_t)px + (size_t)py * packed_w +
                                                (size_t)pz * packed_w * packed_h;

                            out_packed[packed_idx] |= (uint8_t)(1 << bit_idx);
                        }
                    }
                }
            }
        }
    }
}

TEST(shadow_pack_skips_transparent_materials)
{
    /* Water let no sun or sky light through: every non-empty voxel went into the shadow
     * volume, so lake beds sat in full shadow and black AO */
    Bounds3D bounds = {0.0f, 3.2f, 0.0f, 3.2f, 0.0f, 3.2f};
    VoxelVolume *vol = fixture_volume_create(1, 1, 1, bounds);
    ASSERT(vol != NULL);
    Vec3 stone_pos = vec3_create(0.05f, 0.05f, 0.05f); /* voxel (0,0,0): bit 0 of byte 0 */
    Vec3 water_pos = vec3_create(0.15f, 0.05f, 0.05f); /* voxel (1,0,0): bit 1 of byte 0 */
    volume_set_at(vol, stone_pos, MAT_STONE);
    volume_set_at(vol, water_pos, MAT_WATER);

    bool occludes[VOXEL_MATERIAL_MAX];
    for (int32_t i = 0; i < VOXEL_MATERIAL_MAX; i++)
        occludes[i] = !(material_get((uint8_t)i) && (material_get((uint8_t)i)->flags & MAT_FLAG_TRANSPARENT));

    uint8_t packed[16 * 16 * 16];
    uint32_t w, h, d;
    pack_shadow_volume_reference(vol, occludes, packed, &w, &h, &d);
    ASSERT_EQ(packed[0], 0x01);
    pack_shadow_volume_reference(vol, NULL, packed, &w, &h, &d);
    ASSERT_EQ(packed[0], 0x03);

    memset(packed, 0xFF, sizeof(packed));
    volume_pack_shadow_chunk(vol, occludes, 0, packed, w, h, d);
    ASSERT_EQ(packed[0], 0x01);

    volume_destroy(vol);
    return 1;
}

TEST(shadow_dirty_overflow_delivers_every_chunk)
{
    /* 300 shadow-dirty chunks overflow the 256-entry list. The renderer fetches 256
     * per frame and then clears; the clear used to wipe the whole bitmap, so the
     * other 44 chunks kept stale shadows. */
    Bounds3D bounds = {0.0f, 64.0f, 0.0f, 3.2f, 0.0f, 64.0f};
    VoxelVolume *vol = fixture_volume_create(20, 1, 20, bounds);
    ASSERT(vol != NULL);
    volume_clear_shadow_dirty(vol); /* the renderer's initial full build */

    float chunk_size = vol->voxel_size * CHUNK_SIZE;
    volume_edit_begin(vol);
    for (int32_t i = 0; i < 300; i++)
        volume_edit_set(vol, vec3_create(((float)(i % 20) + 0.5f) * chunk_size, 0.05f,
                                         ((float)(i / 20) + 0.5f) * chunk_size), MAT_STONE);
    volume_edit_end(vol);

    bool seen[400] = {false};
    int32_t delivered = 0;
    for (int32_t frame = 0; frame < 8; frame++)
    {
        int32_t indices[VOLUME_SHADOW_DIRTY_MAX];
        int32_t count = volume_get_shadow_dirty_chunks(vol, indices, VOLUME_SHADOW_DIRTY_MAX);
        for (int32_t i = 0; i < count; i++)
        {
            if (!seen[indices[i]])
            {
                seen[indices[i]] = true;
                delivered++;
            }
        }
        volume_clear_shadow_dirty(vol);
    }
    printf("(delivered=%d) ", delivered);
    ASSERT(delivered == 300);

    volume_destroy(vol);
    return 1;
}

/* Boxes must cover the chunk set exactly once each: a chunk left out keeps stale shadows on the
 * GPU, one outside the set would upload texels that were never repacked */
static int check_chunk_boxes(const VoxelVolume *vol, const int32_t *chunks, int32_t count, int32_t expected_boxes)
{
    int32_t sorted[VOLUME_SHADOW_DIRTY_MAX];
    memcpy(sorted, chunks, (size_t)count * sizeof(int32_t));
    ChunkBox boxes[VOLUME_SHADOW_DIRTY_MAX];
    const int32_t box_count = volume_shadow_chunk_boxes(vol, sorted, count, boxes);
    if (expected_boxes > 0)
        ASSERT_EQ(box_count, expected_boxes);
    ASSERT(box_count >= 1 && box_count <= count);

    uint8_t covered[256] = {0};
    ASSERT(vol->total_chunks <= 256);
    for (int32_t b = 0; b < box_count; b++)
        for (int32_t z = boxes[b].min_z; z <= boxes[b].max_z; z++)
            for (int32_t y = boxes[b].min_y; y <= boxes[b].max_y; y++)
                for (int32_t x = boxes[b].min_x; x <= boxes[b].max_x; x++)
                    covered[volume_chunk_index(vol, x, y, z)]++;
    bool in_set[256] = {false};
    for (int32_t i = 0; i < count; i++)
        in_set[chunks[i]] = true;
    for (int32_t c = 0; c < vol->total_chunks; c++)
        ASSERT_EQ(covered[c], in_set[c] ? 1 : 0);

    /* Every box's texels, mip by mip, tightly packed */
    ShadowCopyRegion regions[VOLUME_SHADOW_MIPS * VOLUME_SHADOW_DIRTY_MAX];
    ASSERT_EQ(volume_shadow_copy_regions(boxes, box_count, regions), (size_t)count * VOLUME_SHADOW_CHUNK_BYTES);
    return 1;
}

TEST(shadow_chunk_boxes_cover_the_set_exactly)
{
    Bounds3D bounds = {0.0f, 19.2f, 0.0f, 9.6f, 0.0f, 16.0f};
    VoxelVolume *vol = fixture_volume_create(6, 3, 5, bounds);
    ASSERT(vol != NULL);

    /* A 2x2x2 block is one box, a row and a far chunk two, an L shape two */
    int32_t block[8];
    int32_t n = 0;
    for (int32_t z = 1; z <= 2; z++)
        for (int32_t y = 0; y <= 1; y++)
            for (int32_t x = 3; x <= 4; x++)
                block[n++] = volume_chunk_index(vol, x, y, z);
    ASSERT(check_chunk_boxes(vol, block, n, 1));

    const int32_t row_and_far[5] = {volume_chunk_index(vol, 5, 2, 4), volume_chunk_index(vol, 0, 0, 0), volume_chunk_index(vol, 1, 0, 0),
                                    volume_chunk_index(vol, 2, 0, 0), volume_chunk_index(vol, 3, 0, 0)};
    ASSERT(check_chunk_boxes(vol, row_and_far, 5, 2));

    const int32_t l_shape[3] = {volume_chunk_index(vol, 0, 1, 0), volume_chunk_index(vol, 1, 0, 0), volume_chunk_index(vol, 0, 0, 0)};
    ASSERT(check_chunk_boxes(vol, l_shape, 3, 2));

    /* Scattered sets of every size: holes, touching boxes, whole rows */
    RngState rng;
    rng_seed(&rng, 7);
    for (int32_t trial = 0; trial < 50; trial++)
    {
        int32_t set[90];
        int32_t count = 0;
        const uint32_t density = 1 + rng_range_u32(&rng, 9);
        for (int32_t c = 0; c < vol->total_chunks; c++)
            if (rng_range_u32(&rng, 10) < density)
                set[count++] = c;
        if (count > 0)
            ASSERT(check_chunk_boxes(vol, set, count, 0));
    }

    volume_destroy(vol);
    return 1;
}

/* The shadow volume as the GPU holds it, written the way vkCmdCopyBufferToImage writes the
 * regions (tightly packed staging) */
static void apply_shadow_copies(const VoxelVolume *vol, const ShadowCopyRegion *regions, int32_t count,
                                const uint8_t *staging, uint8_t *gpu[VOLUME_SHADOW_MIPS])
{
    for (int32_t i = 0; i < count; i++)
    {
        const ShadowCopyRegion *r = &regions[i];
        uint32_t dims[3];
        volume_shadow_mip_dims(vol, r->level, dims);
        const uint8_t *src = staging + r->staging_offset;
        for (uint32_t z = 0; z < r->extent[2]; z++)
            for (uint32_t y = 0; y < r->extent[1]; y++)
                for (uint32_t x = 0; x < r->extent[0]; x++)
                    gpu[r->level][(r->offset[0] + x) + (size_t)(r->offset[1] + y) * dims[0] +
                                  (size_t)(r->offset[2] + z) * dims[0] * dims[1]] = *src++;
    }
}

/* Packs the whole volume and builds its mips, as for a new scene */
static void build_full_shadow_mips(const VoxelVolume *vol, uint8_t *mips[VOLUME_SHADOW_MIPS])
{
    uint32_t w, h, d;
    pack_shadow_volume_reference(vol, NULL, mips[0], &w, &h, &d);
    const ChunkBox all = {0, 0, 0, vol->chunks_x - 1, vol->chunks_y - 1, vol->chunks_z - 1};
    volume_shadow_build_mips(vol, &all, mips);
}

TEST(shadow_box_upload_matches_full_rebuild)
{
    /* Each edit used to repack nothing but upload all three mips whole (~130 MB in the game's
     * world); now only the boxes of the edited chunks are rebuilt and copied. Whatever the GPU
     * holds after those copies must equal a full rebuild of the edited volume. */
    Bounds3D bounds = {0.0f, 12.8f, 0.0f, 6.4f, 0.0f, 12.8f};
    VoxelVolume *vol = fixture_volume_create(4, 2, 4, bounds);
    ASSERT(vol != NULL);
    for (int32_t gz = 0; gz < 128; gz++)
        for (int32_t gx = 0; gx < 128; gx++)
            for (int32_t gy = 0; gy < (gx * 7 + gz * 13) % 23; gy++)
                volume_set_at(vol, volume_voxel_to_world(vol, gx, gy, gz), MAT_STONE);

    uint8_t *cpu[VOLUME_SHADOW_MIPS], *gpu[VOLUME_SHADOW_MIPS], *full[VOLUME_SHADOW_MIPS];
    size_t sizes[VOLUME_SHADOW_MIPS];
    size_t total = 0;
    for (uint32_t level = 0; level < VOLUME_SHADOW_MIPS; level++)
    {
        uint32_t dims[3];
        volume_shadow_mip_dims(vol, level, dims);
        sizes[level] = (size_t)dims[0] * dims[1] * dims[2];
        total += sizes[level];
        cpu[level] = malloc(sizes[level]);
        gpu[level] = malloc(sizes[level]);
        full[level] = malloc(sizes[level]);
        memset(gpu[level], 0xCD, sizes[level]);
    }
    uint8_t *staging = malloc(total);
    ShadowCopyRegion regions[VOLUME_SHADOW_MIPS * VOLUME_SHADOW_DIRTY_MAX];

    /* The renderer's first upload: one box over the whole volume */
    build_full_shadow_mips(vol, cpu);
    const ChunkBox all = {0, 0, 0, vol->chunks_x - 1, vol->chunks_y - 1, vol->chunks_z - 1};
    ASSERT_EQ(volume_shadow_copy_regions(&all, 1, regions), total);
    volume_shadow_stage_regions(vol, (const uint8_t *const *)cpu, regions, VOLUME_SHADOW_MIPS, staging);
    apply_shadow_copies(vol, regions, VOLUME_SHADOW_MIPS, staging, gpu);
    volume_clear_shadow_dirty(vol);
    for (uint32_t level = 0; level < VOLUME_SHADOW_MIPS; level++)
        ASSERT(memcmp(gpu[level], cpu[level], sizes[level]) == 0);
    const uint8_t corner_before = gpu[VOLUME_SHADOW_MIPS - 1][sizes[VOLUME_SHADOW_MIPS - 1] - 1];

    /* Edits in two neighbouring chunks, in a chunk of the upper layer and in the far corner */
    volume_set_at(vol, volume_voxel_to_world(vol, 5, 1, 5), MATERIAL_EMPTY);
    volume_set_at(vol, volume_voxel_to_world(vol, 6, 1, 5), MATERIAL_EMPTY);
    volume_set_at(vol, volume_voxel_to_world(vol, 40, 30, 3), MAT_STONE);
    volume_set_at(vol, volume_voxel_to_world(vol, 10, 40, 70), MAT_STONE);
    volume_set_at(vol, volume_voxel_to_world(vol, 127, 63, 127), MAT_STONE);

    int32_t dirty[VOLUME_SHADOW_DIRTY_MAX];
    const int32_t dirty_count = volume_get_shadow_dirty_chunks(vol, dirty, VOLUME_SHADOW_DIRTY_MAX);
    ASSERT_EQ(dirty_count, 4);
    uint32_t dims0[3];
    volume_shadow_mip_dims(vol, 0, dims0);
    for (int32_t i = 0; i < dirty_count; i++)
        volume_pack_shadow_chunk(vol, NULL, dirty[i], cpu[0], dims0[0], dims0[1], dims0[2]);
    ChunkBox boxes[VOLUME_SHADOW_DIRTY_MAX];
    const int32_t box_count = volume_shadow_chunk_boxes(vol, dirty, dirty_count, boxes);
    ASSERT_EQ(box_count, 3);
    for (int32_t b = 0; b < box_count; b++)
        volume_shadow_build_mips(vol, &boxes[b], cpu);
    volume_clear_shadow_dirty(vol);

    const size_t staged = volume_shadow_copy_regions(boxes, box_count, regions);
    printf("(%d chunks in %d boxes, %zu of %zu bytes) ", dirty_count, box_count, staged, total);
    ASSERT_EQ(staged, (size_t)dirty_count * VOLUME_SHADOW_CHUNK_BYTES);
    volume_shadow_stage_regions(vol, (const uint8_t *const *)cpu, regions, box_count * VOLUME_SHADOW_MIPS, staging);
    apply_shadow_copies(vol, regions, box_count * VOLUME_SHADOW_MIPS, staging, gpu);

    build_full_shadow_mips(vol, full);
    for (uint32_t level = 0; level < VOLUME_SHADOW_MIPS; level++)
    {
        ASSERT(memcmp(cpu[level], full[level], sizes[level]) == 0);
        ASSERT(memcmp(gpu[level], full[level], sizes[level]) == 0);
    }
    /* Not vacuous: the far corner voxel reached the coarsest mip */
    ASSERT(corner_before == 0 && gpu[VOLUME_SHADOW_MIPS - 1][sizes[VOLUME_SHADOW_MIPS - 1] - 1] != 0);

    for (uint32_t level = 0; level < VOLUME_SHADOW_MIPS; level++)
    {
        free(cpu[level]);
        free(gpu[level]);
        free(full[level]);
    }
    free(staging);
    volume_destroy(vol);
    return 1;
}

TEST(split_queue_holds_each_object_once)
{
    /* Every hit on an object queues a split check (a full flood fill), and the
     * per-tick limit only counts checks that split. Repeated hits used to queue the
     * same object again each time, so one tick could run hundreds of fills. */
    Bounds3D bounds = {-8.0f, 8.0f, 0.0f, 8.0f, -8.0f, 8.0f};
    VoxelObjectWorld *world = voxel_object_world_create(bounds, 0.1f);
    ASSERT(world != NULL);
    int32_t idx = fixture_add_box(world, vec3_create(0.0f, 4.0f, 0.0f),
                                             vec3_create(0.5f, 0.5f, 0.5f), MAT_STONE);
    ASSERT(idx >= 0);

    for (int32_t i = 0; i < 10; i++)
        voxel_object_world_queue_split(world, idx);
    int32_t queued = (world->split_queue_tail - world->split_queue_head + VOBJ_SPLIT_QUEUE_SIZE) %
                     VOBJ_SPLIT_QUEUE_SIZE;
    printf("(%d queued) ", queued);
    ASSERT(queued == 1);

    /* Once processed it can be queued again */
    voxel_object_world_process_splits(world);
    ASSERT(world->split_queue_head == world->split_queue_tail);
    voxel_object_world_queue_split(world, idx);
    ASSERT(world->split_queue_head != world->split_queue_tail);

    voxel_object_world_destroy(world);
    return 1;
}

/* Length of the object dirty list, or -1 if it does not end within object_count links */
static int32_t dirty_list_length(const VoxelObjectWorld *world)
{
    int32_t length = 0;
    for (int32_t i = world->first_dirty; i >= 0; i = world->objects[i].next_dirty)
    {
        if (++length > world->object_count)
            return -1;
    }
    return length;
}

TEST(split_separates_face_connected_components)
{
    /* Splitting floods the first voxel's face-connected component (along 32-bit voxel
     * rows, with the scratch on the stack) and moves the rest into a new object, which
     * is queued again. Pieces: a U (base and two arms, joined only through the base), a
     * cube, two voxels touching only along an edge, and a row broken by a one-voxel gap. */
    static uint8_t grid[VOBJ_TOTAL_VOXELS];
    memset(grid, 0, sizeof(grid));
#define SET(x, y, z) (grid[(x) + (y) * VOBJ_GRID_SIZE + (z) * VOBJ_GRID_SIZE * VOBJ_GRID_SIZE] = MAT_STONE)
    for (int32_t x = 2; x < 12; x++)
        for (int32_t z = 2; z < 6; z++)
        {
            SET(x, 2, z);                       /* base 10x1x4 */
            if (x < 4 || x >= 10)
                for (int32_t y = 3; y < 9; y++) /* arms 2x6x4 each */
                    SET(x, y, z);
        }
    for (int32_t z = 20; z < 25; z++)
        for (int32_t y = 20; y < 25; y++)
            for (int32_t x = 20; x < 25; x++)
                SET(x, y, z);                   /* cube 5^3 */
    SET(0, 30, 30);
    SET(1, 31, 30);                             /* edge contact only */
    for (int32_t x = 0; x < 32; x++)
        if (x != 15)
            SET(x, 14, 9);                      /* row 0..14 and 16..31 */
#undef SET

    Bounds3D bounds = {-8.0f, 8.0f, 0.0f, 8.0f, -8.0f, 8.0f};
    VoxelObjectWorld *world = voxel_object_world_create(bounds, 0.1f);
    ASSERT(world != NULL);
    int32_t idx = voxel_object_world_add_from_voxels(world, grid, VOBJ_GRID_SIZE, VOBJ_GRID_SIZE, VOBJ_GRID_SIZE,
                                                     vec3_create(-1.6f, 1.0f, -1.6f), 0.1f);
    ASSERT(idx >= 0);
    int32_t total = world->objects[idx].voxel_count;
    voxel_object_world_queue_split(world, idx);
    for (int32_t tick = 0; tick < 16; tick++)
        voxel_object_world_process_splits(world);

    /* Expected piece sizes, in any order */
    int32_t expected[] = {10 * 4 + 2 * (2 * 6 * 4), 125, 1, 1, 15, 16};
    int32_t matched[6] = {0};
    int32_t pieces = 0, sum = 0;
    for (int32_t i = 0; i < world->object_count; i++)
    {
        const VoxelObject *obj = &world->objects[i];
        if (!obj->active)
            continue;
        pieces++;
        sum += obj->voxel_count;
        bool found = false;
        for (int32_t e = 0; e < 6 && !found; e++)
        {
            if (!matched[e] && expected[e] == obj->voxel_count)
            {
                matched[e] = 1;
                found = true;
            }
        }
        ASSERT(found);
    }
    printf("(%d pieces) ", pieces);
    ASSERT(pieces == 6);
    ASSERT(sum == total);

    voxel_object_world_destroy(world);
    return 1;
}

TEST(dirty_list_survives_direct_recalc)
{
    /* A split recalculates the object it splits right away, even while the object is
     * queued in the dirty list. That recalc cleared shape_dirty, the flag that says
     * "linked", so the next mark_dirty linked it a second time: a cycle, and the
     * objects behind it were never recalculated (or freed) again. */
    Bounds3D bounds = {-50.0f, 50.0f, -50.0f, 50.0f, -50.0f, 50.0f};
    VoxelObjectWorld *world = voxel_object_world_create(bounds, 0.1f);
    ASSERT(world != NULL);
    int32_t idx[10];
    for (int32_t i = 0; i < 10; i++)
    {
        idx[i] = fixture_add_box(world, vec3_create((float)i * 5.0f, 0.0f, 0.0f),
                                            vec3_create(0.4f, 0.4f, 0.4f), MAT_STONE);
        ASSERT(idx[i] >= 0);
    }

    /* Deepest in the (LIFO) list: object 9, then object 0 cut in two with a split
     * queued, as a hit does, then eight more ahead of it */
    voxel_object_world_mark_dirty(world, idx[9]);
    VoxelObject *obj = &world->objects[idx[0]];
    for (int32_t z = 0; z < VOBJ_GRID_SIZE; z++)
        for (int32_t y = 0; y < VOBJ_GRID_SIZE; y++)
        {
            int32_t i = vobj_index(VOBJ_GRID_SIZE / 2, y, z);
            if (obj->voxels[i].material != 0)
            {
                obj->voxels[i].material = 0;
                obj->voxel_count--;
            }
        }
    voxel_object_world_mark_dirty(world, idx[0]);
    voxel_object_world_queue_split(world, idx[0]);
    for (int32_t i = 1; i <= 8; i++)
        voxel_object_world_mark_dirty(world, idx[i]);

    voxel_object_world_process_splits(world);  /* recalcs object 0 while it is linked */
    voxel_object_world_process_recalcs(world); /* the per-tick budget stops before it */
    voxel_object_world_mark_dirty(world, idx[0]);
    int32_t length = dirty_list_length(world);
    printf("(list length %d) ", length);
    ASSERT(length == 2);

    for (int32_t tick = 0; tick < 4; tick++)
        voxel_object_world_process_recalcs(world);
    ASSERT(world->first_dirty == -1);
    for (int32_t i = 0; i < world->object_count; i++)
        ASSERT(!world->objects[i].shape_dirty);

    voxel_object_world_free_slot(world, idx[9]);
    ASSERT(world->objects[idx[9]].in_free_list);
    ASSERT(world->first_free_slot == idx[9]);

    voxel_object_world_destroy(world);
    return 1;
}

TEST(freed_slot_leaves_dirty_list_intact)
{
    /* An object recalculated directly while queued, then freed: free_slot took the
     * cleared shape_dirty for "not linked" and put the slot on the free list. The next
     * allocation reset the still-linked slot and cut the list off behind it. */
    Bounds3D bounds = {-50.0f, 50.0f, -50.0f, 50.0f, -50.0f, 50.0f};
    VoxelObjectWorld *world = voxel_object_world_create(bounds, 0.1f);
    ASSERT(world != NULL);
    int32_t behind = fixture_add_box(world, vec3_create(-5.0f, 0.0f, 0.0f),
                                                vec3_create(0.4f, 0.4f, 0.4f), MAT_STONE);
    int32_t ahead = fixture_add_box(world, vec3_create(5.0f, 0.0f, 0.0f),
                                               vec3_create(0.4f, 0.4f, 0.4f), MAT_STONE);
    ASSERT(behind >= 0 && ahead >= 0);

    voxel_object_world_mark_dirty(world, behind);
    voxel_object_world_mark_dirty(world, ahead);
    voxel_object_recalc_shape(&world->objects[ahead]);
    voxel_object_world_free_slot(world, ahead);

    int32_t fresh = fixture_add_box(world, vec3_create(0.0f, 5.0f, 0.0f),
                                               vec3_create(0.4f, 0.4f, 0.4f), MAT_STONE);
    printf("(fresh slot %d, freed %d) ", fresh, ahead);
    ASSERT(fresh >= 0 && fresh != ahead);

    for (int32_t tick = 0; tick < 2; tick++)
        voxel_object_world_process_recalcs(world);
    ASSERT(world->first_dirty == -1);
    ASSERT(!world->objects[behind].shape_dirty);
    ASSERT(world->objects[ahead].in_free_list);
    ASSERT(world->objects[fresh].active && !world->objects[fresh].shape_dirty);

    voxel_object_world_destroy(world);
    return 1;
}

TEST(object_from_voxels_keeps_their_positions)
{
    /* The grid centre was put at the centre of the source box. Odd sizes sit half a
     * voxel off the grid centre in the object grid, so every voxel landed half a voxel
     * towards -axis of where it was. */
    Bounds3D bounds = {-50.0f, 50.0f, -50.0f, 50.0f, -50.0f, 50.0f};
    VoxelObjectWorld *world = voxel_object_world_create(bounds, 0.1f);
    ASSERT(world != NULL);
    const int32_t sizes[][3] = {{2, 2, 2}, {3, 3, 3}, {5, 4, 1}, {1, 6, 7}, {32, 31, 9}};
    const float vs = 0.1f;
    const Vec3 origin = vec3_create(1.0f, 2.0f, 3.0f);
    static uint8_t source[VOBJ_TOTAL_VOXELS];

    float worst = 0.0f, worst_box = 0.0f;
    for (int32_t s = 0; s < (int32_t)(sizeof(sizes) / sizeof(sizes[0])); s++)
    {
        int32_t sx = sizes[s][0], sy = sizes[s][1], sz = sizes[s][2];
        memset(source, MAT_STONE, (size_t)(sx * sy * sz));
        int32_t idx = voxel_object_world_add_from_voxels(world, source, sx, sy, sz, origin, vs);
        ASSERT(idx >= 0);
        const VoxelObject *obj = &world->objects[idx];
        ASSERT(obj->voxel_count == sx * sy * sz);

        /* The lowest object voxel's world centre must be the first source voxel's */
        int32_t min[3] = {VOBJ_GRID_SIZE, VOBJ_GRID_SIZE, VOBJ_GRID_SIZE};
        for (int32_t i = 0; i < VOBJ_TOTAL_VOXELS; i++)
        {
            if (obj->voxels[i].material == 0)
                continue;
            int32_t x, y, z;
            vobj_coords(i, &x, &y, &z);
            min[0] = x < min[0] ? x : min[0];
            min[1] = y < min[1] ? y : min[1];
            min[2] = z < min[2] ? z : min[2];
        }
        float half_grid = (float)VOBJ_GRID_SIZE * 0.5f;
        Vec3 first = vec3_add(obj->position, vec3_create(((float)min[0] + 0.5f - half_grid) * vs,
                                                         ((float)min[1] + 0.5f - half_grid) * vs,
                                                         ((float)min[2] + 0.5f - half_grid) * vs));
        Vec3 expected = vec3_add(origin, vec3_create(0.5f * vs, 0.5f * vs, 0.5f * vs));
        worst = fmaxf(worst, vec3_length(vec3_sub(first, expected)));

        /* The shape box is the source box */
        Vec3 box_min = vec3_sub(voxel_object_box_center(obj), obj->shape_half_extents);
        Vec3 box_max = vec3_add(voxel_object_box_center(obj), obj->shape_half_extents);
        Vec3 source_max = vec3_add(origin, vec3_create((float)sx * vs, (float)sy * vs, (float)sz * vs));
        worst_box = fmaxf(worst_box, fmaxf(vec3_length(vec3_sub(box_min, origin)),
                                           vec3_length(vec3_sub(box_max, source_max))));
    }

    printf("(worst voxel offset %.4f, worst box offset %.4f) ", worst, worst_box);
    ASSERT(worst < 1e-4f);
    ASSERT(worst_box < 1e-4f);

    voxel_object_world_destroy(world);
    return 1;
}

static int32_t bvh_max_leaf_depth(const BVH *bvh, int32_t node, int32_t depth)
{
    if (bvh->nodes[node].count > 0)
        return depth;
    int32_t left = bvh_max_leaf_depth(bvh, bvh->nodes[node].left_first, depth + 1);
    int32_t right = bvh_max_leaf_depth(bvh, bvh->nodes[node].left_first + 1, depth + 1);
    return left > right ? left : right;
}

TEST(bvh_deep_layout_stays_traversable)
{
    /* Objects at geometrically growing distances make each SAH split peel off only a
     * few objects, so the tree gets deep. Traversal stacks (32 entries here, 16 in
     * the shaders) silently dropped nodes below their depth: queries missed objects. */
    Bounds3D bounds = {-8.0f, 8.0f, 0.0f, 8.0f, -8.0f, 8.0f};
    VoxelObjectWorld *world = voxel_object_world_create(bounds, 0.1f);
    ASSERT(world != NULL && world->bvh != NULL);

    const int32_t count = 130;
    float x = 0.0f, step = 1.0f;
    for (int32_t i = 0; i < count; i++)
    {
        VoxelObject *obj = &world->objects[i];
        obj->active = true;
        obj->position = vec3_create(x, 0.0f, 0.0f);
        obj->radius = 0.4f;
        x += step;
        step *= 1.9f;
    }
    world->object_count = count;
    voxel_object_world_update_bvh(world);

    int32_t depth = bvh_max_leaf_depth(world->bvh, 0, 0);
    int32_t candidates[VOBJ_MAX_OBJECTS];
    int32_t hits = bvh_query_ray_candidates(world->bvh, vec3_create(-1.0f, 0.0f, 0.0f),
                                            vec3_create(1.0f, 0.0f, 0.0f), 1e38f, candidates, VOBJ_MAX_OBJECTS);
    printf("(depth %d, ray found %d of %d) ", depth, hits, count);
    ASSERT(hits == count);

    for (int32_t i = 0; i < count; i++)
    {
        BVHQueryResult r = bvh_query_sphere(world->bvh, world->objects[i].position, 0.1f);
        bool found = false;
        for (int32_t k = 0; k < r.count; k++)
            found = found || r.indices[k] == i;
        ASSERT(found);
    }
    ASSERT(depth <= BVH_MAX_DEPTH);

    voxel_object_world_destroy(world);
    return 1;
}

TEST(chunkwise_shadow_pack_matches_whole_pack)
{
    /* A new scene's shadow volume is packed chunk by chunk, only the chunks that hold voxels,
     * each with its own mips, in parallel (TerrainVolume::pack_shadow_volume): it must equal
     * the whole volume packed at once */
    Bounds3D bounds = {0.0f, 12.8f, 0.0f, 6.4f, 0.0f, 12.8f};
    VoxelVolume *vol = fixture_volume_create(4, 2, 4, bounds);
    ASSERT(vol != NULL);
    for (int32_t gz = 0; gz < 128; gz++)
        for (int32_t gx = 0; gx < 64; gx++)
            for (int32_t gy = 0; gy < (gx * 7 + gz * 13) % 23; gy++)
                volume_set_at(vol, volume_voxel_to_world(vol, gx, gy, gz), (gx + gy) % 5 ? MAT_STONE : MAT_WATER);

    bool occludes[VOXEL_MATERIAL_MAX];
    for (int32_t i = 0; i < VOXEL_MATERIAL_MAX; i++)
        occludes[i] = !(material_get((uint8_t)i) && (material_get((uint8_t)i)->flags & MAT_FLAG_TRANSPARENT));

    uint8_t *chunkwise[VOLUME_SHADOW_MIPS], *whole[VOLUME_SHADOW_MIPS];
    size_t sizes[VOLUME_SHADOW_MIPS];
    for (uint32_t level = 0; level < VOLUME_SHADOW_MIPS; level++)
    {
        uint32_t dims[3];
        volume_shadow_mip_dims(vol, level, dims);
        sizes[level] = (size_t)dims[0] * dims[1] * dims[2];
        chunkwise[level] = calloc(sizes[level], 1);
        whole[level] = malloc(sizes[level]);
    }

    uint32_t dims0[3];
    volume_shadow_mip_dims(vol, 0, dims0);
    int32_t filled = 0;
    for (int32_t ci = 0; ci < vol->total_chunks; ci++)
    {
        if (!vol->chunks[ci].occupancy.has_any)
            continue;
        filled++;
        volume_pack_shadow_chunk(vol, occludes, ci, chunkwise[0], dims0[0], dims0[1], dims0[2]);
        const int32_t cx = ci % vol->chunks_x, cy = ci / vol->chunks_x % vol->chunks_y, cz = ci / (vol->chunks_x * vol->chunks_y);
        const ChunkBox box = {cx, cy, cz, cx, cy, cz};
        volume_shadow_build_mips(vol, &box, chunkwise);
    }
    ASSERT(filled > 0 && filled < vol->total_chunks); /* the empty half is skipped */

    uint32_t w, h, d;
    pack_shadow_volume_reference(vol, occludes, whole[0], &w, &h, &d);
    const ChunkBox all = {0, 0, 0, vol->chunks_x - 1, vol->chunks_y - 1, vol->chunks_z - 1};
    volume_shadow_build_mips(vol, &all, whole);
    for (uint32_t level = 0; level < VOLUME_SHADOW_MIPS; level++)
    {
        ASSERT(memcmp(chunkwise[level], whole[level], sizes[level]) == 0);
        free(chunkwise[level]);
        free(whole[level]);
    }
    volume_destroy(vol);
    return 1;
}

/* World centre of grid cell (x, y, z) of an object drawn at position / orientation */
static Vec3 cell_centre(Vec3 position, Quat orientation, float vs, int32_t x, int32_t y, int32_t z)
{
    const float half = (float)VOBJ_GRID_SIZE * 0.5f;
    Vec3 local = vec3_create(((float)x + 0.5f - half) * vs, ((float)y + 0.5f - half) * vs, ((float)z + 0.5f - half) * vs);
    return vec3_add(position, quat_rotate_vec3(orientation, local));
}

TEST(object_draw_pose_spans_the_last_tick)
{
    uint8_t block[27];
    memset(block, MAT_STONE, sizeof(block));
    Bounds3D bounds = {-8.0f, 8.0f, 0.0f, 8.0f, -8.0f, 8.0f};
    VoxelObjectWorld *world = voxel_object_world_create(bounds, 0.1f);
    ASSERT(world != NULL);
    int32_t idx = voxel_object_world_add_from_voxels(world, block, 3, 3, 3, vec3_create(0.0f, 2.0f, 0.0f), 0.1f);
    ASSERT(idx >= 0);
    VoxelObject *obj = &world->objects[idx];

    /* A new object is drawn where it is, from its first frame */
    Vec3 p;
    Quat q;
    voxel_object_draw_pose(obj, 0.0f, &p, &q);
    ASSERT_NEAR(vec3_length(vec3_sub(p, obj->position)), 0.0f, 1e-6f);

    /* A tick moves it 1 m and turns it a quarter about y: halfway, half of each */
    voxel_object_world_begin_tick(world);
    const Vec3 start = obj->position;
    obj->position = vec3_add(obj->position, vec3_create(1.0f, 0.0f, 0.0f));
    obj->orientation = quat_create(0.0f, 0.70710678f, 0.0f, 0.70710678f);
    voxel_object_draw_pose(obj, 0.5f, &p, &q);
    ASSERT_NEAR(p.x, start.x + 0.5f, 1e-5f);
    Vec3 axis = quat_rotate_vec3(q, vec3_create(1.0f, 0.0f, 0.0f));
    ASSERT_NEAR(axis.x, 0.70710678f, 1e-5f);

    /* Recentering after a voxel change moves the previous pose with the voxels: a voxel that
     * stays is drawn where it was */
    const float vs = obj->voxel_size;
    int32_t lo = (VOBJ_GRID_SIZE - 3) / 2;
    Vec3 before = cell_centre(obj->prev_position, obj->prev_orientation, vs, lo, lo, lo);
    for (int32_t z = 0; z < 3; z++)
        for (int32_t y = 0; y < 3; y++)
        {
            obj->voxels[vobj_index(lo + 2, lo + y, lo + z)].material = 0;
            obj->voxel_count--;
        }
    voxel_object_mark_changed(obj, VOBJ_ALL_BRICKS);
    voxel_object_recalc_shape(obj);
    ASSERT_EQ(obj->voxels[vobj_index(lo, lo, lo)].material, 0); /* the voxels moved in the grid */
    Vec3 after = cell_centre(obj->prev_position, obj->prev_orientation, vs, lo + 1, lo, lo);
    ASSERT_NEAR(vec3_length(vec3_sub(after, before)), 0.0f, 1e-4f);

    voxel_object_world_destroy(world);
    return 1;
}

int main(void)
{
    printf("=== Voxel Tests ===\n");

    RUN_TEST(volume_create_destroy);
    RUN_TEST(volume_edits_golden);
    RUN_TEST(world_voxel_mapping_at_chunk_borders);
    RUN_TEST(edit_outside_volume_changes_nothing);
    RUN_TEST(edit_batch_occupancy_exact_at_end);
    RUN_TEST(volume_create_clamps_counts_and_bounds);
    RUN_TEST(set_at_marks_shadow_dirty);
    RUN_TEST(removal_log_records_emptied_boxes);
    RUN_TEST(raycast_distance_normal_and_material);
    RUN_TEST(raycast_max_dist_and_skip_flags);
    RUN_TEST(volume_dirty_tracking);
    RUN_TEST(chunk_set_occupancy_matches_rebuild);
    RUN_TEST(active_chunk_count_follows_edits);
    RUN_TEST(volume_edit_batch_uploads_every_chunk);
    RUN_TEST(dirty_chunks_unique_per_frame);
    RUN_TEST(dirty_ring_overflow_delivers_every_chunk_once);
    RUN_TEST(shadow_dirty_overflow_delivers_every_chunk);
    RUN_TEST(shadow_pack_skips_transparent_materials);
    RUN_TEST(shadow_chunk_boxes_cover_the_set_exactly);
    RUN_TEST(shadow_box_upload_matches_full_rebuild);
    RUN_TEST(split_queue_holds_each_object_once);
    RUN_TEST(split_separates_face_connected_components);
    RUN_TEST(dirty_list_survives_direct_recalc);
    RUN_TEST(freed_slot_leaves_dirty_list_intact);
    RUN_TEST(object_from_voxels_keeps_their_positions);
    RUN_TEST(bvh_deep_layout_stays_traversable);
    RUN_TEST(object_draw_pose_spans_the_last_tick);
    RUN_TEST(chunkwise_shadow_pack_matches_whole_pack);

    printf("\nResults: %d/%d passed\n", g_tests_passed, g_tests_run);
    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
