#include "engine/core/types.h"
#include "engine/core/math.h"
#include "engine/voxel/volume.h"
#include "engine/voxel/connectivity.h"
#include "content/materials.h"
#include "test_common.h"
#include "test_fixtures.h"
#include "engine/platform/platform.h"
#include <string.h>

/* 2x2x2 chunks of 0.5 m voxels: 32 m cube, floor at y = 0 */
static const Bounds3D TEST_BOUNDS = {-16.0f, 16.0f, 0.0f, 32.0f, -16.0f, 16.0f};

static void edit_box(VoxelVolume *vol, Vec3 min_corner, Vec3 max_corner, uint8_t material)
{
    volume_edit_begin(vol);
    fixture_fill_box(vol, min_corner, max_corner, material);
    volume_edit_end(vol);
}

/* Remove voxel layer `layer` (y) of the box x in [x0, x1), z in [z0, z1) voxel by
 * voxel, which records a cut boundary like gameplay destruction does */
static void cut_layer(VoxelVolume *vol, int32_t x0, int32_t x1, int32_t layer, int32_t z0, int32_t z1)
{
    float vs = vol->voxel_size;
    volume_edit_begin(vol);
    for (int32_t z = z0; z < z1; z++)
        for (int32_t x = x0; x < x1; x++)
            volume_edit_set(vol, vec3_create(vol->bounds.min_x + ((float)x + 0.5f) * vs,
                                             vol->bounds.min_y + ((float)layer + 0.5f) * vs,
                                             vol->bounds.min_z + ((float)z + 0.5f) * vs),
                            MATERIAL_EMPTY);
    volume_edit_end(vol);
}

static void analyze(VoxelVolume *vol, ConnectivityWorkBuffer *work, ConnectivityResult *result)
{
    volume_consume_pending_analysis(vol);
    connectivity_analyze_dirty(vol, vol->bounds.min_y + 0.1f, work, result);
}

TEST(work_buffer_init_destroy)
{
    Bounds3D bounds = {-8.0f, 8.0f, 0.0f, 16.0f, -8.0f, 8.0f};
    VoxelVolume *vol = fixture_volume_create(1, 1, 1, bounds);
    ASSERT(vol != NULL);

    ConnectivityWorkBuffer work;
    bool ok = connectivity_work_init(&work, vol);
    ASSERT(ok);
    ASSERT(work.visited_gen != NULL);
    ASSERT(work.island_ids != NULL);

    connectivity_work_destroy(&work);
    ASSERT(work.island_ids == NULL);
    volume_destroy(vol);
    return 1;
}

TEST(anchored_block_not_reported)
{
    VoxelVolume *vol = fixture_volume_create(2, 2, 2, TEST_BOUNDS);
    ASSERT(vol != NULL);
    ConnectivityWorkBuffer work;
    ASSERT(connectivity_work_init(&work, vol));

    edit_box(vol, vec3_create(-2.0f, 0.0f, -2.0f), vec3_create(2.0f, 4.0f, 2.0f), MAT_STONE);

    ConnectivityResult result;
    analyze(vol, &work, &result);
    ASSERT(result.island_count == 0);
    ASSERT(!result.truncated);

    connectivity_work_destroy(&work);
    volume_destroy(vol);
    return 1;
}

TEST(floating_block_reported)
{
    VoxelVolume *vol = fixture_volume_create(2, 2, 2, TEST_BOUNDS);
    ASSERT(vol != NULL);
    ConnectivityWorkBuffer work;
    ASSERT(connectivity_work_init(&work, vol));

    /* 8x8x8 voxels: x, z voxel 28..35, y voxel 20..27 */
    edit_box(vol, vec3_create(-2.0f, 10.0f, -2.0f), vec3_create(2.0f, 14.0f, 2.0f), MAT_STONE);

    ConnectivityResult result;
    analyze(vol, &work, &result);
    ASSERT(result.island_count == 1);

    const IslandInfo *island = &result.islands[0];
    ASSERT(island->voxel_count == 8 * 8 * 8);
    ASSERT(island->voxel_min_x == 28 && island->voxel_max_x == 35);
    ASSERT(island->voxel_min_y == 20 && island->voxel_max_y == 27);
    ASSERT(island->voxel_min_z == 28 && island->voxel_max_z == 35);
    ASSERT_NEAR(island->center_of_mass.x, 0.0f, 0.001f);
    ASSERT_NEAR(island->center_of_mass.y, 12.0f, 0.001f);
    ASSERT_NEAR(island->center_of_mass.z, 0.0f, 0.001f);

    connectivity_work_destroy(&work);
    volume_destroy(vol);
    return 1;
}

TEST(only_floating_islands_reported)
{
    VoxelVolume *vol = fixture_volume_create(2, 2, 2, TEST_BOUNDS);
    ASSERT(vol != NULL);
    ConnectivityWorkBuffer work;
    ASSERT(connectivity_work_init(&work, vol));

    volume_edit_begin(vol);
    fixture_fill_box(vol, vec3_create(-8.0f, 0.0f, -2.0f), vec3_create(-4.0f, 4.0f, 2.0f), MAT_STONE);
    fixture_fill_box(vol, vec3_create(4.0f, 10.0f, -2.0f), vec3_create(8.0f, 14.0f, 2.0f), MAT_STONE);
    volume_edit_end(vol);

    ConnectivityResult result;
    analyze(vol, &work, &result);
    ASSERT(result.island_count == 1);
    ASSERT(result.islands[0].voxel_min_y == 20);

    connectivity_work_destroy(&work);
    volume_destroy(vol);
    return 1;
}

TEST(cut_detaches_top)
{
    /* Column 4x20x4 voxels on the floor; removing layer 8 frees layers 9..19 */
    VoxelVolume *vol = fixture_volume_create(2, 2, 2, TEST_BOUNDS);
    ASSERT(vol != NULL);
    ConnectivityWorkBuffer work;
    ASSERT(connectivity_work_init(&work, vol));

    edit_box(vol, vec3_create(0.0f, 0.0f, 0.0f), vec3_create(2.0f, 10.0f, 2.0f), MAT_STONE);
    volume_consume_pending_analysis(vol);
    cut_layer(vol, 32, 36, 8, 32, 36);
    ASSERT(vol->pending_cut_boundary.count > 0);

    ConnectivityResult result;
    analyze(vol, &work, &result);
    ASSERT(result.island_count == 1);
    ASSERT(result.islands[0].voxel_count == 11 * 4 * 4);
    ASSERT(result.islands[0].voxel_min_y == 9 && result.islands[0].voxel_max_y == 19);

    connectivity_work_destroy(&work);
    volume_destroy(vol);
    return 1;
}

TEST(stale_cut_boundary_seeds_through_neighbours)
{
    /* The cut's boundary voxels above the gap are removed by a later batch that records
     * no boundary of its own and is not queued: island removal, e.g. while the analysis
     * was requeued. Their solid neighbours must seed instead. */
    VoxelVolume *vol = fixture_volume_create(2, 2, 2, TEST_BOUNDS);
    ASSERT(vol != NULL);
    ConnectivityWorkBuffer work;
    ASSERT(connectivity_work_init(&work, vol));

    edit_box(vol, vec3_create(0.0f, 0.0f, 0.0f), vec3_create(2.0f, 10.0f, 2.0f), MAT_STONE);
    volume_consume_pending_analysis(vol);
    cut_layer(vol, 32, 36, 8, 32, 36);
    int32_t boundary = vol->pending_cut_boundary.count;

    float vs = vol->voxel_size;
    volume_edit_begin(vol);
    vol->edit_budget_bypass = true; /* as connectivity_remove_island does */
    for (int32_t z = 32; z < 36; z++)
        for (int32_t x = 32; x < 36; x++)
            volume_edit_set(vol, vec3_create(vol->bounds.min_x + ((float)x + 0.5f) * vs, 9.5f * vs,
                                             vol->bounds.min_z + ((float)z + 0.5f) * vs),
                            MATERIAL_EMPTY);
    volume_edit_end(vol);
    ASSERT(vol->pending_cut_boundary.count == boundary && !vol->pending_cut_boundary.overflow);

    ConnectivityResult result;
    analyze(vol, &work, &result);
    ASSERT(result.island_count == 1);
    ASSERT(result.islands[0].voxel_count == 10 * 4 * 4);

    connectivity_work_destroy(&work);
    volume_destroy(vol);
    return 1;
}

TEST(fill_cut_pending_with_edit_set_cut)
{
    /* Two columns cut in separate batches: one voxel by voxel (records a cut boundary),
     * one with fill_box (records none). The analysis used the boundary alone whenever
     * it had entries, so the fill-cut top was never seeded and kept floating. */
    VoxelVolume *vol = fixture_volume_create(2, 2, 2, TEST_BOUNDS);
    ASSERT(vol != NULL);
    ConnectivityWorkBuffer work;
    ASSERT(connectivity_work_init(&work, vol));

    /* 4x20x4-voxel columns on the floor at voxel x 16..19 and 44..47 */
    edit_box(vol, vec3_create(-8.0f, 0.0f, 0.0f), vec3_create(-6.0f, 10.0f, 2.0f), MAT_STONE);
    edit_box(vol, vec3_create(6.0f, 0.0f, 0.0f), vec3_create(8.0f, 10.0f, 2.0f), MAT_STONE);
    volume_consume_pending_analysis(vol);

    cut_layer(vol, 16, 20, 8, 32, 36);
    edit_box(vol, vec3_create(6.0f, 4.0f, 0.0f), vec3_create(8.0f, 4.5f, 2.0f), MATERIAL_EMPTY);

    ConnectivityResult result;
    analyze(vol, &work, &result);
    printf("(%d islands) ", result.island_count);
    ASSERT(result.island_count == 2);
    bool left = false, right = false;
    for (int32_t i = 0; i < result.island_count; i++)
    {
        ASSERT(result.islands[i].voxel_count == 11 * 4 * 4);
        left = left || result.islands[i].voxel_min_x == 16;
        right = right || result.islands[i].voxel_min_x == 44;
    }
    ASSERT(left && right);

    connectivity_work_destroy(&work);
    volume_destroy(vol);
    return 1;
}

TEST(placing_against_solid_needs_no_analysis)
{
    /* Voxels placed on solid ones (snow) cannot leave anything floating. Queued anyway,
     * their chunks were scanned across the map whenever a long dig overflowed the cut
     * boundary. A voxel placed touching nothing can start a floating piece: that one is
     * still analysed. */
    VoxelVolume *vol = fixture_volume_create(2, 2, 2, TEST_BOUNDS);
    ASSERT(vol != NULL);
    ConnectivityWorkBuffer work;
    ASSERT(connectivity_work_init(&work, vol));

    edit_box(vol, vec3_create(-16.0f, 0.0f, -16.0f), vec3_create(16.0f, 2.0f, 16.0f), MAT_STONE);
    volume_consume_pending_analysis(vol);

    /* A layer of "snow" on the slab, spread over all four ground chunks */
    float vs = vol->voxel_size;
    volume_edit_begin(vol);
    for (int32_t i = 0; i < 16; i++)
        volume_edit_set(vol, vec3_create(-15.75f + 8.0f * (float)(i % 4), 2.0f + 0.5f * vs, -15.75f + 8.0f * (float)(i / 4)),
                        MAT_SNOW);
    volume_edit_end(vol);
    printf("(snow: %d chunks pending) ", vol->pending_analysis_count);
    ASSERT(vol->pending_analysis_count == 0);
    ASSERT(vol->pending_cut_boundary.count == 0 && !vol->pending_cut_boundary.overflow);

    /* Two voxels placed in mid-air, one against the other */
    volume_edit_begin(vol);
    volume_edit_set(vol, vec3_create(0.25f, 12.25f, 0.25f), MAT_STONE);
    volume_edit_set(vol, vec3_create(0.75f, 12.25f, 0.25f), MAT_STONE);
    volume_edit_end(vol);
    ASSERT(vol->pending_analysis_count == 1);

    ConnectivityResult result;
    analyze(vol, &work, &result);
    ASSERT(result.island_count == 1);
    ASSERT(result.islands[0].voxel_count == 2);

    connectivity_work_destroy(&work);
    volume_destroy(vol);
    return 1;
}

TEST(island_extraction)
{
    VoxelVolume *vol = fixture_volume_create(2, 2, 2, TEST_BOUNDS);
    ASSERT(vol != NULL);
    ConnectivityWorkBuffer work;
    ASSERT(connectivity_work_init(&work, vol));

    edit_box(vol, vec3_create(0.0f, 10.0f, 0.0f), vec3_create(2.0f, 12.0f, 2.0f), MAT_STONE);

    ConnectivityResult result;
    analyze(vol, &work, &result);
    ASSERT(result.island_count == 1);

    const IslandInfo *island = &result.islands[0];
    int32_t size_x = island->voxel_max_x - island->voxel_min_x + 1;
    int32_t size_y = island->voxel_max_y - island->voxel_min_y + 1;
    int32_t size_z = island->voxel_max_z - island->voxel_min_z + 1;

    static uint8_t voxels[64 * 64 * 64];
    Vec3 origin;
    int32_t extracted = connectivity_extract_island_with_ids(vol, island, &work,
                                                             voxels, size_x, size_y, size_z, &origin);

    ASSERT(extracted == 4 * 4 * 4);
    ASSERT(extracted == island->voxel_count);
    ASSERT(voxels[0] == MAT_STONE);
    ASSERT_NEAR(origin.x, 0.0f, 0.001f);
    ASSERT_NEAR(origin.y, 10.0f, 0.001f);

    /* Too small an output grid is refused */
    ASSERT(connectivity_extract_island_with_ids(vol, island, &work, voxels, size_x - 1, size_y, size_z,
                                                &origin) == 0);

    connectivity_work_destroy(&work);
    volume_destroy(vol);
    return 1;
}

TEST(island_removal)
{
    VoxelVolume *vol = fixture_volume_create(2, 2, 2, TEST_BOUNDS);
    ASSERT(vol != NULL);
    ConnectivityWorkBuffer work;
    ASSERT(connectivity_work_init(&work, vol));

    edit_box(vol, vec3_create(0.0f, 10.0f, 0.0f), vec3_create(2.0f, 12.0f, 2.0f), MAT_STONE);
    Vec3 check_pos = {1.0f, 11.0f, 1.0f};
    ASSERT(volume_get_at(vol, check_pos) == MAT_STONE);

    ConnectivityResult result;
    analyze(vol, &work, &result);
    ASSERT(result.island_count == 1);

    uint32_t removals = vol->removal_count;
    connectivity_remove_island(vol, &result.islands[0], &work);
    ASSERT(volume_get_at(vol, check_pos) == MAT_AIR);
    ASSERT(vol->total_solid_voxels == 0);

    /* Removing a whole component cannot free anything else: nothing to re-analyse */
    ASSERT(vol->pending_analysis_count == 0);
    ASSERT(vol->pending_cut_boundary.count == 0);

    /* Logged as one removal with the island's bounds, for bodies resting on it */
    ASSERT(vol->removal_count == removals + 1);
    const VoxelBox *box = &vol->removal_log[removals % VOLUME_REMOVAL_LOG_SIZE];
    ASSERT(box->min_x == result.islands[0].voxel_min_x && box->max_x == result.islands[0].voxel_max_x);
    ASSERT(box->min_y == result.islands[0].voxel_min_y && box->max_y == result.islands[0].voxel_max_y);
    ASSERT(box->min_z == result.islands[0].voxel_min_z && box->max_z == result.islands[0].voxel_max_z);

    connectivity_work_destroy(&work);
    volume_destroy(vol);
    return 1;
}

TEST(stack_overflow_counts_as_anchored)
{
    /* A floating component bigger than the flood-fill stack is never reported:
     * an incompletely explored island must not be detached */
    VoxelVolume *vol = fixture_volume_create(2, 2, 2, TEST_BOUNDS);
    ASSERT(vol != NULL);
    ConnectivityWorkBuffer work;
    ASSERT(connectivity_work_init(&work, vol));
    work.stack_capacity = 16;

    edit_box(vol, vec3_create(-4.0f, 10.0f, -4.0f), vec3_create(4.0f, 18.0f, 4.0f), MAT_STONE);

    ConnectivityResult result;
    analyze(vol, &work, &result);
    ASSERT(result.island_count == 0);

    connectivity_work_destroy(&work);
    volume_destroy(vol);
    return 1;
}

TEST(island_removal_occupancy_exact_and_fast)
{
    /* Removal went voxel -> world -> voxel through volume_edit_set, rescanned an 8^3
     * region after every voxel and rebuilt each touched chunk again at the end: 21 ms
     * for this island. It writes by voxel index now and rescans each emptied region
     * once; occupancy must still equal a full rebuild. */
    VoxelVolume *vol = volume_create_dims(8, 4, 8, vec3_zero(), 0.1f);
    ASSERT(vol != NULL);
    ConnectivityWorkBuffer work;
    ASSERT(connectivity_work_init(&work, vol));

    /* A floating 128x16x128 slab, a solid voxel layer left under it, cut free by one
     * removed voxel at a corner */
    fixture_fill_box(vol, vec3_create(0.05f, 0.05f, 0.05f), vec3_create(25.55f, 0.05f, 25.55f), MAT_STONE);
    fixture_fill_box(vol, vec3_create(0.05f, 2.05f, 0.05f), vec3_create(12.75f, 3.55f, 12.75f), MAT_DIRT);
    volume_edit_begin(vol);
    fixture_edit_voxel(vol, 0, 20, 0, MATERIAL_EMPTY);
    volume_edit_end(vol);

    ConnectivityResult result;
    volume_consume_pending_analysis(vol);
    connectivity_analyze_dirty(vol, vol->bounds.min_y + 0.1f, &work, &result);
    ASSERT(result.island_count == 1);
    ASSERT(result.islands[0].voxel_count == 128 * 16 * 128 - 1);

    int64_t t0 = platform_get_ticks();
    connectivity_remove_island(vol, &result.islands[0], &work);
    float ms = (float)(platform_get_ticks() - t0) * 1000.0f / (float)platform_get_frequency();
    printf("(%d voxels removed in %.2f ms) ", result.islands[0].voxel_count, ms);

    static Chunk rebuilt;
    int32_t solid = 0;
    for (int32_t c = 0; c < vol->total_chunks; c++)
    {
        rebuilt = vol->chunks[c];
        chunk_rebuild_occupancy(&rebuilt);
        ASSERT(memcmp(&vol->chunks[c].occupancy, &rebuilt.occupancy, sizeof(ChunkOccupancy)) == 0);
        solid += rebuilt.occupancy.solid_count;
    }
    ASSERT(solid == 256 * 256 && vol->total_solid_voxels == solid);
    ASSERT_PERF(ms < 10.0f);

    connectivity_work_destroy(&work);
    volume_destroy(vol);
    return 1;
}

TEST(voxel_index_past_int32)
{
    /* Per-voxel arrays are indexed chunk-major. At VOLUME_MAX_CHUNKS (131072 chunks of
     * 32768 voxels) the last index is 2^32 - 1; int32 arithmetic overflowed from 65536
     * chunks on (UBSan: connectivity_work_init, global_voxel_index). */
    static VoxelVolume vol;
    memset(&vol, 0, sizeof(vol));
    vol.chunks_x = VOLUME_MAX_CHUNKS_X;
    vol.chunks_y = VOLUME_MAX_CHUNKS_Y;
    vol.chunks_z = VOLUME_MAX_CHUNKS_Z;
    vol.total_chunks = VOLUME_MAX_CHUNKS;

    int32_t last_x = VOLUME_MAX_CHUNKS_X * CHUNK_SIZE - 1;
    int32_t last_y = VOLUME_MAX_CHUNKS_Y * CHUNK_SIZE - 1;
    int32_t last_z = VOLUME_MAX_CHUNKS_Z * CHUNK_SIZE - 1;
    ASSERT(volume_voxel_linear_index(&vol, last_x, last_y, last_z) == (size_t)VOLUME_MAX_CHUNKS * CHUNK_VOXEL_COUNT - 1);
    /* First voxel of chunk 65536, where the 32-bit product wrapped to INT32_MIN */
    ASSERT(volume_voxel_linear_index(&vol, 0, 0, 32 * CHUNK_SIZE) == (size_t)65536 * CHUNK_VOXEL_COUNT);
    ASSERT(sizeof(volume_voxel_linear_index(&vol, 0, 0, 0)) == sizeof(size_t));
    return 1;
}

TEST(analysis_golden)
{
    /* Two floating boxes and two anchored ones: the analysis finds exactly the two
     * floating ones, with these bounds, sizes, ids and centres of mass */
    VoxelVolume *vol = fixture_volume_create(2, 2, 2, TEST_BOUNDS);
    ASSERT(vol != NULL);
    const Vec3 positions[] = {
        {-6.0f, 0.0f, -2.0f},
        {-4.0f, 0.0f, -2.0f},
        {4.0f, 10.0f, 0.0f},
        {7.0f, 10.0f, 0.0f},
    };
    volume_edit_begin(vol);
    for (int i = 0; i < 4; i++)
        fixture_fill_box(vol, positions[i], vec3_add(positions[i], vec3_create(2.0f, 4.0f, 4.0f)), MAT_STONE);
    volume_edit_end(vol);

    ConnectivityWorkBuffer work;
    ASSERT(connectivity_work_init(&work, vol));
    ConnectivityResult result;
    analyze(vol, &work, &result);

    /* 4x8x8 voxels of 0.5 m each, at voxel x 40 and 46, y 20, z 32 */
    static const int32_t min_x[2] = {40, 46};
    ASSERT(result.island_count == 2 && !result.truncated);
    for (int i = 0; i < 2; i++)
    {
        const IslandInfo *island = &result.islands[i];
        ASSERT(island->island_id == 2 + i);
        ASSERT(island->voxel_count == 4 * 8 * 8);
        ASSERT(island->voxel_min_x == min_x[i] && island->voxel_max_x == min_x[i] + 3);
        ASSERT(island->voxel_min_y == 20 && island->voxel_max_y == 27);
        ASSERT(island->voxel_min_z == 32 && island->voxel_max_z == 39);
        ASSERT_NEAR(island->center_of_mass.x, -16.0f + (float)min_x[i] * 0.5f + 1.0f, 1e-4f);
        ASSERT_NEAR(island->center_of_mass.y, 12.0f, 1e-4f);
        ASSERT_NEAR(island->center_of_mass.z, 2.0f, 1e-4f);
    }

    connectivity_work_destroy(&work);
    volume_destroy(vol);
    return 1;
}

int main(void)
{
    printf("=== Connectivity Tests ===\n");

    RUN_TEST(work_buffer_init_destroy);
    RUN_TEST(anchored_block_not_reported);
    RUN_TEST(floating_block_reported);
    RUN_TEST(only_floating_islands_reported);
    RUN_TEST(cut_detaches_top);
    RUN_TEST(stale_cut_boundary_seeds_through_neighbours);
    RUN_TEST(fill_cut_pending_with_edit_set_cut);
    RUN_TEST(placing_against_solid_needs_no_analysis);
    RUN_TEST(island_extraction);
    RUN_TEST(island_removal);
    RUN_TEST(stack_overflow_counts_as_anchored);
    RUN_TEST(analysis_golden);
    RUN_TEST(island_removal_occupancy_exact_and_fast);
    RUN_TEST(voxel_index_past_int32);

    printf("\nResults: %d/%d passed\n", g_tests_passed, g_tests_run);
    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
