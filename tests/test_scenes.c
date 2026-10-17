/* setenv and unsetenv are POSIX, hidden under strict -std=c23 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "engine/core/types.h"
#include "engine/core/math.h"
#include "engine/core/rng.h"
#include "engine/sim/scene.h"
#include "engine/sim/loading.h"
#include "engine/platform/platform.h"
#include "engine/platform/thread.h"
#include "game/world_scene_internal.h"
#include "game/biome.h"
#include "game/day_night.h"
#include "game/destruction_script.h"
#include "game/scatter_gen.h"
#include "game/shrub_gen.h"
#include "game/terrain_gen.h"
#include "game/tree_gen.h"
#include "game/weather.h"
#include "content/materials.h"
#include "content/scenes.h"
#include "test_common.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define FRAME_BUDGET_MS 16.667f

static void test_set_env(const char *name, const char *value)
{
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

static void test_unset_env(const char *name)
{
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

TEST(world_scene_create_destroy)
{
    const SceneDescriptor *desc = scene_get_descriptor(SCENE_TYPE_WORLD);
    ASSERT(desc != NULL);

    Scene *scene = world_scene_create(desc->bounds, desc->voxel_size, NULL);
    ASSERT(scene != NULL);
    ASSERT(scene->user_data != NULL);

    scene_destroy(scene);
    return 1;
}

TEST(world_scene_init)
{
    const SceneDescriptor *desc = scene_get_descriptor(SCENE_TYPE_WORLD);
    Scene *scene = world_scene_create(desc->bounds, desc->voxel_size, NULL);
    ASSERT(scene != NULL);

    rng_seed(&scene->rng, 12345);
    scene_init(scene);

    WorldSceneData *data = (WorldSceneData *)scene->user_data;
    ASSERT(data->stats.spawn_count == 0);

    scene_destroy(scene);
    return 1;
}

TEST(world_scene_tick_increments_stats)
{
    const SceneDescriptor *desc = scene_get_descriptor(SCENE_TYPE_WORLD);
    Scene *scene = world_scene_create(desc->bounds, desc->voxel_size, NULL);
    rng_seed(&scene->rng, 12345);
    scene_init(scene);

    WorldSceneData *data = (WorldSceneData *)scene->user_data;
    int32_t initial_tick_count = data->stats.tick_count;

    scene_update(scene, 1.0f / 60.0f);

    ASSERT(data->stats.tick_count > initial_tick_count);

    scene_destroy(scene);
    return 1;
}

TEST(world_scene_stress_env_override)
{
    /* PATCH_STRESS_OBJECTS drops that many shapes in, and spawning stops at capacity */
    const SceneDescriptor *desc = scene_get_descriptor(SCENE_TYPE_WORLD);
    Scene *scene = world_scene_create(desc->bounds, desc->voxel_size, NULL);
    ASSERT(scene != NULL);
    rng_seed(&scene->rng, 12345);

    test_set_env("PATCH_STRESS_OBJECTS", "100000");
    scene_init(scene);
    test_unset_env("PATCH_STRESS_OBJECTS");

    WorldSceneData *data = (WorldSceneData *)scene->user_data;
    ASSERT(data->objects != NULL);
    ASSERT_EQ(data->objects->object_count, VOBJ_MAX_OBJECTS);
    ASSERT_EQ(data->stats.spawn_count, VOBJ_MAX_OBJECTS - 2); /* minus the two fixed props */

    scene_destroy(scene);
    return 1;
}

TEST(world_scene_ray_setting)
{
    const SceneDescriptor *desc = scene_get_descriptor(SCENE_TYPE_WORLD);
    Scene *scene = world_scene_create(desc->bounds, desc->voxel_size, NULL);

    Vec3 origin = vec3_create(1.0f, 2.0f, 3.0f);
    Vec3 dir = vec3_create(0.0f, -1.0f, 0.0f);
    world_scene_set_ray(scene, origin, dir);

    WorldSceneData *data = (WorldSceneData *)scene->user_data;
    ASSERT_NEAR(data->ray_origin.x, 1.0f, 0.001f);
    ASSERT_NEAR(data->ray_origin.y, 2.0f, 0.001f);
    ASSERT_NEAR(data->ray_origin.z, 3.0f, 0.001f);
    ASSERT_NEAR(data->ray_dir.y, -1.0f, 0.001f);

    scene_destroy(scene);
    return 1;
}

TEST(world_scene_performance)
{
    platform_time_init();

    const SceneDescriptor *desc = scene_get_descriptor(SCENE_TYPE_WORLD);
    Scene *scene = world_scene_create(desc->bounds, desc->voxel_size, NULL);
    rng_seed(&scene->rng, 12345);
    scene_init(scene);

    for (int32_t i = 0; i < 10; i++)
        scene_update(scene, 1.0f / 60.0f);

    float total_ms = 0.0f;
    float max_ms = 0.0f;
    const int32_t FRAMES = 60;

    for (int32_t i = 0; i < FRAMES; i++)
    {
        PlatformTime t0 = platform_time_now();
        scene_update(scene, 1.0f / 60.0f);
        PlatformTime t1 = platform_time_now();

        float ms = (float)(platform_time_delta_seconds(t0, t1) * 1000.0);
        total_ms += ms;
        if (ms > max_ms)
            max_ms = ms;
    }

    float avg_ms = total_ms / FRAMES;
    float budget_pct = (avg_ms / FRAME_BUDGET_MS) * 100.0f;

    printf("\n    Blank scene tick: avg=%.2fms max=%.2fms budget=%.1f%%", avg_ms, max_ms, budget_pct);

    ASSERT_PERF(avg_ms < FRAME_BUDGET_MS * 0.1f);

    scene_destroy(scene);
    return 1;
}

TEST(scene_interp_alpha_clamps)
{
    Scene scene = {0};
    scene.sim_accumulator = SIM_TIMESTEP * 0.25f;
    ASSERT_NEAR(scene_interp_alpha(&scene), 0.25f, 1e-6f);
    scene.sim_accumulator = -SIM_TIMESTEP;
    ASSERT_EQ(scene_interp_alpha(&scene), 0.0f);
    scene.sim_accumulator = SIM_TIMESTEP * 2.0f; /* scene_update's cap: more than one tick behind */
    ASSERT_EQ(scene_interp_alpha(&scene), 1.0f);
    return 1;
}

/* The centre of the first empty voxel above the ground straight below (x, z), when nothing
 * (water included) lies within `radius` voxels sideways and `height` voxels up from it */
static bool find_open_ground(const VoxelVolume *terrain, float x, float z, int32_t radius, int32_t height,
                             Vec3 *out_base)
{
    Vec3 hit;
    if (volume_raycast(terrain, vec3_create(x, terrain->bounds.max_y, z), vec3_create(0.0f, -1.0f, 0.0f),
                       terrain->bounds.max_y - terrain->bounds.min_y, &hit, NULL, NULL, MAT_FLAG_LIQUID) < 0.0f)
        return false;

    const float vs = terrain->voxel_size;
    Vec3 base = vec3_create(terrain->bounds.min_x + (floorf((x - terrain->bounds.min_x) / vs) + 0.5f) * vs,
                            hit.y + 0.5f * vs,
                            terrain->bounds.min_z + (floorf((z - terrain->bounds.min_z) / vs) + 0.5f) * vs);
    for (int32_t dx = -radius; dx <= radius; dx++)
        for (int32_t dz = -radius; dz <= radius; dz++)
            for (int32_t dy = 0; dy < height; dy++)
            {
                Vec3 offset = vec3_scale(vec3_create((float)dx, (float)dy, (float)dz), vs);
                if (volume_get_at(terrain, vec3_add(base, offset)) != MAT_AIR)
                    return false;
            }
    *out_base = base;
    return true;
}

/* Through the public scene API only: a 3x3 stone column built on open ground, cut by a
 * radius-2 destruction, bursts the cut into debris and, once the left button is up, its
 * top leaves the terrain as a falling body */
static int check_destroy_sphere_cuts_column_loose(Scene *scene)
{
    VoxelVolume *terrain = world_scene_get_terrain(scene);
    VoxelObjectWorld *objects = world_scene_get_objects(scene);
    ParticleSystem *particles = world_scene_get_particles(scene);
    ASSERT(terrain && objects && particles);

    const int32_t COLUMN_HEIGHT = 40;
    const float vs = terrain->voxel_size;
    Vec3 base = vec3_zero();
    bool found = false;
    for (int32_t i = 0; i < 256 && !found; i++)
        found = find_open_ground(terrain, -20.0f + 2.5f * (float)(i % 16), -20.0f + 2.5f * (float)(i / 16), 3,
                                 COLUMN_HEIGHT + 4, &base);
    ASSERT(found);

    volume_edit_begin(terrain);
    for (int32_t y = 0; y < COLUMN_HEIGHT; y++)
        for (int32_t x = -1; x <= 1; x++)
            for (int32_t z = -1; z <= 1; z++)
                volume_edit_set(terrain, vec3_add(base, vec3_scale(vec3_create((float)x, (float)y, (float)z), vs)),
                                MAT_STONE);
    volume_edit_end(terrain);

    const Vec3 cut = vec3_add(base, vec3_create(0.0f, 20.0f * vs, 0.0f));
    const Vec3 top = vec3_add(base, vec3_create(0.0f, 35.0f * vs, 0.0f));
    const Vec3 stump = vec3_add(base, vec3_create(0.0f, 5.0f * vs, 0.0f));
    const int32_t particles_before = particles->count;
    const int32_t objects_before = objects->object_count;

    /* Centre column 5 cells, the other eight 3 each: the whole cross section */
    WorldSceneDestruction destruction = world_scene_destroy_sphere(scene, cut, 2);
    ASSERT_EQ(destruction.voxels_removed, 29);
    ASSERT_EQ(destruction.debris_spawned, 29);
    ASSERT_EQ(particles->count, particles_before + 29);
    ASSERT_EQ(volume_get_at(terrain, top), MAT_STONE); /* the check waits for the next tick */

    scene_handle_input(scene, 0.0f, 0.0f, false, false);
    scene_update(scene, SIM_TIMESTEP);
    ASSERT_EQ(volume_get_at(terrain, top), MAT_AIR);
    ASSERT_EQ(volume_get_at(terrain, stump), MAT_STONE);
    ASSERT_EQ(objects->object_count, objects_before + 1);
    return 1;
}

/* The script's two target draws (x, z) come first, then one per debris particle and
 * nothing else: the sequence a seed replays depends only on what each destruction carved */
static int check_script_draws_target_before_debris(Scene *scene)
{
    int32_t hits = 0;
    for (int32_t i = 0; i < 8; i++)
    {
        RngState expected = scene->rng;
        WorldSceneDestruction destruction;
        bool hit = destruction_script_step(scene, &destruction);
        rng_next(&expected);
        rng_next(&expected);
        if (hit)
        {
            hits++;
            ASSERT(destruction.debris_spawned <= destruction.voxels_removed);
            for (int32_t d = 0; d < destruction.debris_spawned; d++)
                rng_next(&expected);
        }
        ASSERT_EQ(scene->rng.state, expected.state);
    }
    ASSERT(hits > 0);

    /* Frames 3, 6 and 9 of 1..10 destroy; frame 0 (before counting starts) never does */
    DestructionScript script = {.interval_frames = 3};
    for (int32_t frame = 0; frame <= 10; frame++)
        destruction_script_frame(&script, scene, frame);
    ASSERT_EQ(script.destructions, 3);
    ASSERT(script.voxels_removed > 0);
    return 1;
}

TEST(world_scene_destruction)
{
    /* One generated world for both checks, as generation dominates their cost */
    const SceneDescriptor *desc = scene_get_descriptor(SCENE_TYPE_WORLD);
    Scene *scene = world_scene_create(desc->bounds, desc->voxel_size, NULL);
    ASSERT(scene != NULL);
    rng_seed(&scene->rng, 12345);
    scene_init(scene);

    int ok = check_destroy_sphere_cuts_column_loose(scene) && check_script_draws_target_before_debris(scene);
    scene_destroy(scene);
    return ok;
}

/* ---- World generation ---- */

/* A one-cell-wide channel that snakes from an exit on the far edge back and forth across
 * the map, walled in everywhere else. The exit drains it, so no channel cell may hold
 * water. Most of the path runs against a raster scan's direction. */
TEST(flood_drains_serpentine_channel)
{
    enum { N = 64, ROW_SPACING = 3 };
    const float wall = 10.0f;
    float *terrain = (float *)malloc(N * N * sizeof(float));
    float *water = (float *)malloc(N * N * sizeof(float));
    ASSERT(terrain && water);

    for (int i = 0; i < N * N; i++)
        terrain[i] = wall;
    terrain[(N - 1) * N + (N - 2)] = 0.0f; /* exit on the +z edge */
    terrain[(N - 2) * N + (N - 2)] = 0.0f;
    int row = N - 3;
    for (int k = 0; row >= 1; k++, row -= ROW_SPACING)
    {
        for (int gx = 1; gx <= N - 2; gx++)
            terrain[row * N + gx] = 0.0f;
        int connector_x = (k % 2 == 0) ? 1 : N - 2;
        for (int gz = row - ROW_SPACING; gz < row && gz >= 1; gz++)
            terrain[gz * N + connector_x] = 0.0f;
    }

    terrain_flood_water_levels(terrain, water, N, N, -1.0f, NULL);

    for (int i = 0; i < N * N; i++)
        ASSERT_EQ(water[i], terrain[i]);

    free(terrain);
    free(water);
    return 1;
}

TEST(flood_fills_closed_basin_to_rim)
{
    enum { N = 8 };
    float terrain[N * N];
    float water[N * N];
    for (int i = 0; i < N * N; i++)
        terrain[i] = 2.0f;
    for (int gz = 2; gz <= 5; gz++)
        for (int gx = 2; gx <= 5; gx++)
            terrain[gz * N + gx] = 4.0f; /* rim */
    terrain[3 * N + 3] = 1.0f;           /* basin floor */
    terrain[3 * N + 4] = 0.5f;

    terrain_flood_water_levels(terrain, water, N, N, 0.0f, NULL);

    ASSERT_EQ(water[3 * N + 3], 4.0f);
    ASSERT_EQ(water[3 * N + 4], 4.0f);
    ASSERT_EQ(water[0], 2.0f);
    ASSERT_EQ(water[1 * N + 1], 2.0f);
    return 1;
}

static int count_reachable_branches(const ProceduralTree *tree, int32_t idx, int depth)
{
    int count = 0;
    for (; idx >= 0; idx = tree->branches[idx].sibling_idx)
    {
        if (idx >= tree->branch_count || depth > 64)
            return -1000000;
        count += 1 + count_reachable_branches(tree, tree->branches[idx].child_idx, depth + 1);
    }
    return count;
}

TEST(tree_generate_overflow_keeps_trunk)
{
    /* Far more branches than TREE_MAX_BRANCHES: the trunk must survive and every stored
     * branch must hang off it */
    RngState rng;
    rng_seed(&rng, 7);
    TreeConfig config = tree_config_create(TREE_OAK, &rng, 1.0f);
    config.max_depth = 6;
    config.splits_min = 8.0f;
    config.splits_max = 8.0f;

    ProceduralTree *tree = (ProceduralTree *)malloc(sizeof(ProceduralTree));
    ASSERT(tree != NULL);
    tree_generate(tree, &config, &rng);

    ASSERT(tree->branch_count == TREE_MAX_BRANCHES);
    ASSERT(tree->trunk_idx >= 0);
    ASSERT_EQ(count_reachable_branches(tree, tree->trunk_idx, 0), tree->branch_count);

    free(tree);
    return 1;
}

TEST(tree_voxelize_keeps_siblings_of_culled_branch)
{
    /* Trunk with two children: one entirely outside the volume, its sibling inside */
    const float vs = 0.1f;
    VoxelVolume *vol = volume_create_dims(1, 1, 1, vec3_zero(), vs);
    ASSERT(vol != NULL);

    RngState rng;
    rng_seed(&rng, 1);
    TreeConfig config = tree_config_create(TREE_OAK, &rng, 1.0f);

    ProceduralTree *tree = (ProceduralTree *)calloc(1, sizeof(ProceduralTree));
    ASSERT(tree != NULL);
    TreeBranch *inside = &tree->branches[0];
    TreeBranch *outside = &tree->branches[1];
    TreeBranch *trunk = &tree->branches[2];

    inside->start = vec3_create(2.5f, 0.5f, 2.5f);
    inside->end = vec3_create(2.5f, 1.5f, 2.5f);
    inside->wood_radius = 0.15f;
    inside->aabb = (Bounds3D){.min_x = 2.35f, .max_x = 2.65f, .min_y = 0.35f, .max_y = 1.65f, .min_z = 2.35f, .max_z = 2.65f};
    inside->sibling_idx = -1;
    inside->child_idx = -1;

    outside->start = vec3_create(50.0f, 0.5f, 50.0f);
    outside->end = vec3_create(50.0f, 1.5f, 50.0f);
    outside->wood_radius = 0.15f;
    outside->aabb = (Bounds3D){.min_x = 49.85f, .max_x = 50.15f, .min_y = 0.35f, .max_y = 1.65f, .min_z = 49.85f, .max_z = 50.15f};
    outside->sibling_idx = 0;
    outside->child_idx = -1;

    trunk->start = vec3_create(0.5f, 0.0f, 0.5f);
    trunk->end = vec3_create(0.5f, 1.0f, 0.5f);
    trunk->wood_radius = 0.15f;
    trunk->aabb = (Bounds3D){.min_x = 0.35f, .max_x = 50.15f, .min_y = -0.15f, .max_y = 1.65f, .min_z = 0.35f, .max_z = 50.15f};
    trunk->sibling_idx = -1;
    trunk->child_idx = 1;

    tree->branch_count = 3;
    tree->trunk_idx = 2;

    const VoxelBox whole = {0, 0, 0, CHUNK_SIZE - 1, CHUNK_SIZE - 1, CHUNK_SIZE - 1};
    tree_voxelize_clipped(tree, &config, vol, vec3_zero(), &whole, 1);

    ASSERT_EQ(volume_get_at(vol, vec3_create(0.55f, 0.55f, 0.55f)), config.bark_material);
    ASSERT_EQ(volume_get_at(vol, vec3_create(2.55f, 1.05f, 2.55f)), config.bark_material);

    free(tree);
    volume_destroy(vol);
    return 1;
}

TEST(weather_intensity_settles)
{
    /* A steady state eases its intensity to the target and holds it there */
    WeatherSystem weather = weather_create(42);
    weather.current = WEATHER_CLOUDY;
    weather.target = WEATHER_CLOUDY;
    weather.transition = 1.0f;
    weather.state_timer = 1e9f;

    for (int i = 0; i < 600; i++)
        weather_update(&weather, SIM_TIMESTEP);

    float settled = weather.intensity;
    ASSERT(settled > 0.0f);
    for (int i = 0; i < 60; i++)
    {
        weather_update(&weather, SIM_TIMESTEP);
        ASSERT_EQ(weather.intensity, settled);
    }
    return 1;
}

/* ---- Trees ---- */

TEST(tree_configs_scale_with_world_scale)
{
    /* A world twice the scale grows each tree twice as large: lengths and radii double,
     * branch counts and length ratios stay. (Dead trees grow no crown: their twig ends
     * are a fixed size of bark.) */
    for (int32_t type = 0; type < TREE_TYPE_COUNT; type++)
    {
        for (uint64_t seed = 1; seed <= 8; seed++)
        {
            RngState ra, rb;
            rng_seed(&ra, seed);
            rng_seed(&rb, seed);
            TreeConfig a = tree_config_create((TreeType)type, &ra, 0.4f);
            TreeConfig b = tree_config_create((TreeType)type, &rb, 0.8f);
            if (a.leaf_material == a.bark_material)
                continue;
            ASSERT_NEAR(b.trunk_len, 2.0f * a.trunk_len, 1e-4f * b.trunk_len);
            ASSERT_NEAR(b.trunk_radius, 2.0f * a.trunk_radius, 1e-4f * b.trunk_radius);
            ASSERT_NEAR(b.leaf_radius_min, 2.0f * a.leaf_radius_min, 1e-4f * fabsf(b.leaf_radius_min));
            ASSERT_NEAR(b.leaf_radius_max, 2.0f * a.leaf_radius_max, 1e-4f * fabsf(b.leaf_radius_max));
            ASSERT_NEAR(b.leaf_radius_scaled, 2.0f * a.leaf_radius_scaled, 1e-4f * fabsf(b.leaf_radius_scaled));
            ASSERT_EQ(b.splits_min, a.splits_min);
            ASSERT_EQ(b.splits_max, a.splits_max);
            ASSERT_EQ(b.branch_child_len, a.branch_child_len);
        }
    }
    return 1;
}

static uint64_t volume_voxel_digest(const VoxelVolume *vol)
{
    uint64_t h = 1469598103934665603ULL;
    for (int32_t c = 0; c < vol->total_chunks; c++)
        for (int32_t v = 0; v < CHUNK_VOXEL_COUNT; v++)
        {
            h ^= vol->chunks[c].voxels[v].material;
            h *= 1099511628211ULL;
        }
    return h;
}

TEST(leaf_noise_follows_world_seed)
{
    /* The crowns' radius noise comes from the world seed: another world grows the same
     * tree with other crowns, the same world with the same ones */
    ProceduralTree *tree = (ProceduralTree *)malloc(sizeof(ProceduralTree));
    ASSERT(tree != NULL);
    RngState rng;
    rng_seed(&rng, 3);
    TreeConfig config = tree_config_create(TREE_OAK, &rng, 0.2f);
    tree_generate(tree, &config, &rng);

    const uint32_t seeds[3] = {1, 2, 1};
    uint64_t digests[3] = {0};
    for (int32_t k = 0; k < 3; k++)
    {
        VoxelVolume *vol = volume_create_dims(2, 2, 2, vec3_create(-3.2f, 0.0f, -3.2f), 0.1f);
        ASSERT(vol != NULL);
        const VoxelBox whole = {0, 0, 0, 2 * CHUNK_SIZE - 1, 2 * CHUNK_SIZE - 1, 2 * CHUNK_SIZE - 1};
        tree_voxelize_clipped(tree, &config, vol, vec3_create(0.0f, 0.5f, 0.0f), &whole, seeds[k]);
        digests[k] = volume_voxel_digest(vol);
        volume_destroy(vol);
    }
    free(tree);
    ASSERT(digests[0] != digests[1]);
    ASSERT_EQ(digests[0], digests[2]);
    return 1;
}

/* ---- Terrain ---- */

TEST(cliffs_show_rock_whichever_way_they_face)
{
    /* A 2 m step across grassland, facing each of the four ways: the columns on both sides
     * of the step show rock, the ones a few columns off it their grass */
    const BiomeDesc *grassland = &g_biomes[BIOME_GRASSLAND];
    TerrainColumns cols;
    cols.seed = 1;
    cols.nx = cols.nz = CHUNK_SIZE;
    cols.height = (float *)malloc(CHUNK_SIZE * CHUNK_SIZE * sizeof(float));
    cols.biome = (uint8_t *)malloc(CHUNK_SIZE * CHUNK_SIZE);
    cols.surface = (uint8_t *)malloc(CHUNK_SIZE * CHUNK_SIZE);
    ASSERT(cols.height && cols.biome && cols.surface);
    memset(cols.biome, BIOME_GRASSLAND, CHUNK_SIZE * CHUNK_SIZE);
    memset(cols.surface, grassland->surface, CHUNK_SIZE * CHUNK_SIZE);

    const int32_t step = CHUNK_SIZE / 2;
    for (int32_t facing = 0; facing < 4; facing++)
    {
        const bool along_x = facing < 2;
        const bool rising = facing % 2 == 0;
        for (int32_t gz = 0; gz < CHUNK_SIZE; gz++)
            for (int32_t gx = 0; gx < CHUNK_SIZE; gx++)
            {
                const bool high = ((along_x ? gx : gz) >= step) == rising;
                cols.height[terrain_column_index(&cols, gx, gz)] = high ? 4.05f : 2.05f;
            }
        VoxelVolume *vol = volume_create_dims(1, 1, 1, vec3_zero(), 0.2f);
        ASSERT(vol != NULL);
        terrain_gen_heightmap(vol, &cols, NULL);

        /* Columns at offsets -1 and 0 from the step touch it, -4 and +3 do not */
        const int32_t offsets[4] = {-1, 0, -4, 3};
        for (int32_t k = 0; k < 4; k++)
        {
            const int32_t a = step + offsets[k];
            const int32_t gx = along_x ? a : CHUNK_SIZE / 2;
            const int32_t gz = along_x ? CHUNK_SIZE / 2 : a;
            const int32_t i = terrain_column_index(&cols, gx, gz);
            const uint8_t top = terrain_voxel_get(vol, gx, terrain_surface_voxel(vol, cols.height[i]) - 1, gz);
            ASSERT_EQ(top, k < 2 ? grassland->rock : grassland->surface);
        }
        volume_destroy(vol);
    }
    terrain_columns_release(&cols);
    return 1;
}

TEST(generation_without_columns_writes_nothing)
{
    /* terrain_columns_build out of memory leaves the columns empty: the stages after it
     * then place nothing (they once indexed the freed arrays) */
    VoxelVolume *vol = volume_create_dims(2, 2, 2, vec3_zero(), 0.1f);
    ASSERT(vol != NULL);
    TerrainColumns cols;
    memset(&cols, 0, sizeof(cols));
    terrain_gen_heightmap(vol, &cols, NULL);
    terrain_gen_water(vol, &cols, NULL);
    ASSERT_EQ(terrain_gen_trees(vol, &cols, 1.0f, NULL), 0);
    ASSERT_EQ(shrub_gen_place(vol, &cols), 0);
    ASSERT_EQ(scatter_gen_apply(vol, &cols, NULL), 0);
    for (int32_t c = 0; c < vol->total_chunks; c++)
        ASSERT(!vol->chunks[c].occupancy.has_any);
    volume_destroy(vol);
    return 1;
}

/* ---- Day, weather and ambient particles ---- */

TEST(sun_rises_and_sets_on_opposite_sides)
{
    DayNightCycle cycle = day_night_create(300.0f);
    SceneLighting dawn, noon, dusk;
    cycle.time_of_day = 0.27f;
    day_night_apply_lighting(&cycle, &dawn);
    cycle.time_of_day = 0.5f;
    day_night_apply_lighting(&cycle, &noon);
    cycle.time_of_day = 0.73f;
    day_night_apply_lighting(&cycle, &dusk);

    /* Low in the sky at dawn and dusk, from opposite directions; high at noon */
    Vec3 east = vec3_normalize(vec3_create(dawn.sun_direction.x, 0.0f, dawn.sun_direction.z));
    Vec3 west = vec3_normalize(vec3_create(dusk.sun_direction.x, 0.0f, dusk.sun_direction.z));
    printf("(dawn . dusk = %.3f) ", vec3_dot(east, west));
    ASSERT(vec3_dot(east, west) < -0.9f);
    ASSERT(dawn.sun_direction.y > 0.0f && dawn.sun_direction.y < 0.3f);
    ASSERT(dusk.sun_direction.y > 0.0f && dusk.sun_direction.y < 0.3f);
    ASSERT(noon.sun_direction.y > 0.85f);
    return 1;
}

/* Particles of a type a system has spawned, while none of its slots was reused */
static int32_t spawned_of_type(const EnvParticleSystem *sys, uint8_t type)
{
    int32_t n = 0;
    for (int32_t i = 0; i < sys->count; i++)
        n += sys->particles[i].type == type;
    return n;
}

TEST(fireflies_by_night_bees_by_day)
{
    int32_t fireflies[2] = {0}, bees[2] = {0};
    for (int32_t night = 0; night < 2; night++)
    {
        EnvParticleSystem *sys = env_particles_create(5);
        ASSERT(sys != NULL);
        sys->emitters[0] = (EnvEmitter){.position = vec3_create(0.0f, 1.0f, 0.0f), .type = ENV_PARTICLE_FIREFLY};
        sys->emitters[1] = (EnvEmitter){.position = vec3_create(1.0f, 1.0f, 0.0f), .type = ENV_PARTICLE_BEE};
        sys->emitter_count = 2;
        for (int32_t tick = 0; tick < 60 * 120; tick++)
            env_particles_update(sys, NULL, night ? 0.0f : 1.0f, SIM_TIMESTEP, (double)tick * SIM_TIMESTEP);
        fireflies[night] = spawned_of_type(sys, ENV_PARTICLE_FIREFLY);
        bees[night] = spawned_of_type(sys, ENV_PARTICLE_BEE);
        env_particles_destroy(sys);
    }
    printf("(day: %d fireflies %d bees, night: %d fireflies %d bees) ", fireflies[0], bees[0], fireflies[1],
           bees[1]);
    ASSERT_EQ(fireflies[0], 0);
    ASSERT(fireflies[1] > 0);
    ASSERT(bees[0] > 0);
    ASSERT_EQ(bees[1], 0);
    return 1;
}

TEST(emitter_stops_when_its_voxel_goes)
{
    /* A dust emitter on a grass voxel fires while the grass is there; once it is dug out
     * the emitter is dropped the next time it fires, and nothing more comes from it */
    VoxelVolume *vol = volume_create_dims(1, 1, 1, vec3_zero(), 0.1f);
    EnvParticleSystem *sys = env_particles_create(9);
    ASSERT(vol && sys);
    const Vec3 grass = vec3_create(1.55f, 1.55f, 1.55f); /* voxel (15, 15, 15) */
    volume_set_at(vol, grass, MAT_GRASS);
    sys->emitters[0] = (EnvEmitter){.position = vec3_create(1.55f, 1.6f, 1.55f), .gx = 15, .gy = 15, .gz = 15,
                                    .type = ENV_PARTICLE_DUST, .source_material = MAT_GRASS};
    sys->emitter_count = 1;

    int32_t tick = 0;
    for (; tick < 60 * 60; tick++)
        env_particles_update(sys, vol, 1.0f, SIM_TIMESTEP, (double)tick * SIM_TIMESTEP);
    const int32_t spawned = sys->count;
    ASSERT(spawned > 0);
    ASSERT_EQ(sys->emitter_count, 1);

    volume_set_at(vol, grass, MAT_AIR);
    for (; tick < 60 * 240; tick++)
        env_particles_update(sys, vol, 1.0f, SIM_TIMESTEP, (double)tick * SIM_TIMESTEP);
    ASSERT_EQ(sys->emitter_count, 0);
    ASSERT_EQ(sys->count, spawned);

    env_particles_destroy(sys);
    volume_destroy(vol);
    return 1;
}

/* A spot of the world whose climate stays on one side of the snow line over the square
 * the precipitation around it lands in */
static bool find_climate(const BiomeField *field, bool cold, float *out_x, float *out_z)
{
    for (float z = -70.0f; z <= 70.0f; z += 5.0f)
        for (float x = -70.0f; x <= 70.0f; x += 5.0f)
        {
            bool all = true;
            for (float dz = -18.0f; dz <= 18.0f && all; dz += 2.0f)
                for (float dx = -18.0f; dx <= 18.0f && all; dx += 2.0f)
                {
                    float t = biome_field_temperature(field, x + dx, z + dz);
                    all = cold ? t < WEATHER_SNOW_BELOW_TEMPERATURE - 0.02f : t > WEATHER_SNOW_BELOW_TEMPERATURE + 0.2f;
                }
            if (all)
            {
                *out_x = x;
                *out_z = z;
                return true;
            }
        }
    return false;
}

typedef struct
{
    int32_t alive;
    int32_t of_type;
    int32_t over_landing_square; /* passing the viewer's height within 17 m of it, each way */
    int32_t near_viewer;         /* within 8 m of it sideways, below the spawn height */
    int32_t in_ground;
    float lowest; /* metres above the ground */
} PrecipitationStats;

#define PRECIPITATION_TEST_GROUND 10.0f

/* A storm `seconds` long over flat ground (one grass layer, its top PRECIPITATION_TEST_GROUND
 * metres up), seen from 2 m above it at (x, z) */
static bool precipitation_stats(uint32_t seed, float x, float z, float seconds, uint8_t type,
                                PrecipitationStats *out)
{
    const float vs = 0.25f;
    VoxelVolume *vol = volume_create_dims(12, 6, 12, vec3_create(x - 48.0f, 0.0f, z - 48.0f), vs);
    EnvParticleSystem *env = env_particles_create(seed);
    if (!vol || !env)
    {
        env_particles_destroy(env);
        if (vol)
            volume_destroy(vol);
        return false;
    }
    const int32_t ground_gy = (int32_t)(PRECIPITATION_TEST_GROUND / vs) - 1;
    for (int32_t gz = 0; gz < 12 * CHUNK_SIZE; gz++)
        for (int32_t gx = 0; gx < 12 * CHUNK_SIZE; gx++)
            terrain_voxel_set(vol, gx, ground_gy, gz, MAT_GRASS);

    WeatherSystem weather = weather_create(seed);
    weather_set_state(&weather, WEATHER_STORM);
    const Vec3 viewer = vec3_create(x, PRECIPITATION_TEST_GROUND + 2.0f, z);
    const int32_t ticks = (int32_t)(seconds / SIM_TIMESTEP);
    for (int32_t t = 0; t < ticks; t++)
    {
        weather_update(&weather, SIM_TIMESTEP);
        weather_apply_env_particles(&weather, env, viewer, SIM_TIMESTEP);
        env_particles_update(env, vol, 1.0f, SIM_TIMESTEP, (double)t * SIM_TIMESTEP);
    }

    memset(out, 0, sizeof(*out));
    out->lowest = 1e9f;
    const Vec3 steady = env_particles_steady_velocity(env, type);
    for (int32_t i = 0; i < env->count; i++)
    {
        const EnvParticle *p = &env->particles[i];
        if (!p->active)
            continue;
        out->alive++;
        out->of_type += p->type == type;
        /* Where it passes (or passed) the viewer's height, falling at the steady velocity
         * (the flutter of snow moves a flake less than a voxel off that line) */
        float fall = (p->position.y - viewer.y) / -steady.y;
        float cross_x = p->position.x + steady.x * fall - viewer.x;
        float cross_z = p->position.z + steady.z * fall - viewer.z;
        out->over_landing_square += fabsf(cross_x) <= 17.0f && fabsf(cross_z) <= 17.0f;
        float dx = p->position.x - viewer.x, dz = p->position.z - viewer.z;
        out->near_viewer += dx * dx + dz * dz < 64.0f;
        float height = p->position.y - PRECIPITATION_TEST_GROUND;
        out->in_ground += height < 0.0f;
        out->lowest = fminf(out->lowest, height);
    }
    env_particles_destroy(env);
    volume_destroy(vol);
    return true;
}

TEST(precipitation_reaches_the_ground_around_the_viewer)
{
    /* Rain where the climate is warm and snow where it is cold, falling around the viewer
     * (the weather spawns it above it, upwind by its drift) all the way to the ground,
     * where it ends */
    const uint32_t seed = scene_get_descriptor(SCENE_TYPE_WORLD)->rng_seed;
    BiomeField field = biome_field_create(seed);
    float warm_x, warm_z, cold_x, cold_z;
    ASSERT(find_climate(&field, false, &warm_x, &warm_z));
    ASSERT(find_climate(&field, true, &cold_x, &cold_z));

    PrecipitationStats rain, snow;
    ASSERT(precipitation_stats(seed, warm_x, warm_z, 10.0f, ENV_PARTICLE_RAIN, &rain));
    ASSERT(precipitation_stats(seed, cold_x, cold_z, 40.0f, ENV_PARTICLE_SNOW, &snow));
    printf("(rain: %d alive, %d within 8 m, lowest %.2f m; snow: %d alive, %d within 8 m, lowest %.2f m) ",
           rain.alive, rain.near_viewer, rain.lowest, snow.alive, snow.near_viewer, snow.lowest);

    ASSERT_EQ(rain.of_type, rain.alive);
    ASSERT_EQ(snow.of_type, snow.alive);
    ASSERT(rain.alive > 1000 && rain.alive < 10000);
    ASSERT(snow.alive > 1000 && snow.alive < 10000);
    ASSERT(rain.over_landing_square * 100 >= rain.alive * 99);
    ASSERT(snow.over_landing_square * 100 >= snow.alive * 99);
    ASSERT(rain.near_viewer > 100 && snow.near_viewer > 100);
    ASSERT(rain.lowest < 0.5f && snow.lowest < 0.5f);
    ASSERT_EQ(rain.in_ground + snow.in_ground, 0);
    return 1;
}

TEST(snow_builds_up_only_where_cold)
{
    /* Grass over the whole world and a long storm: snow settles on the grass of the cold
     * climates only */
    const SceneDescriptor *desc = scene_get_descriptor(SCENE_TYPE_WORLD);
    const float vs = 0.4f;
    VoxelVolume *vol = volume_create_dims(12, 2, 12, vec3_create(desc->bounds.min_x, 0.0f, desc->bounds.min_z), vs);
    ASSERT(vol != NULL);
    const int32_t n = 12 * CHUNK_SIZE;
    const int32_t ground = 20;
    for (int32_t gz = 0; gz < n; gz++)
        for (int32_t gx = 0; gx < n; gx++)
            terrain_voxel_set(vol, gx, ground, gz, MAT_GRASS);
    volume_recount_solid(vol);

    WeatherSystem weather = weather_create(desc->rng_seed);
    weather_set_state(&weather, WEATHER_STORM);
    int32_t placed = 0;
    for (int32_t i = 0; i < 200; i++)
        placed += weather_accumulate_snow(&weather, vol, 2.0f);

    int32_t snow = 0, warm = 0;
    for (int32_t gz = 0; gz < n; gz++)
        for (int32_t gx = 0; gx < n; gx++)
        {
            if (terrain_voxel_get(vol, gx, ground + 1, gz) != MAT_SNOW)
                continue;
            snow++;
            Vec3 c = volume_voxel_to_world(vol, gx, ground + 1, gz);
            /* The voxel centre may lie a little off the point the snow fell at */
            warm += biome_field_temperature(&weather.climate, c.x, c.z) >= WEATHER_SNOW_BELOW_TEMPERATURE + 0.05f;
        }
    printf("(%d snow voxels) ", snow);
    volume_destroy(vol);
    ASSERT(snow > 0);
    ASSERT_EQ(snow, placed);
    ASSERT_EQ(warm, 0);
    return 1;
}

/* ---- Background loading ---- */

typedef struct
{
    PlatformMutex *mutex;
    int32_t started[16];  /* (mutex) */
    int32_t finished[16]; /* (mutex) */
    int32_t started_count;
    int32_t finished_count;
} StageLog;

/* Logs the stage around 20 ms of work, so a load can be abandoned while one runs */
static void log_stage(void *ctx, int32_t stage)
{
    StageLog *log = (StageLog *)ctx;
    platform_mutex_lock(log->mutex);
    log->started[log->started_count++] = stage;
    platform_mutex_unlock(log->mutex);

    const int64_t end = platform_get_ticks() + platform_get_frequency() / 50;
    while (platform_get_ticks() < end)
    {
    }

    platform_mutex_lock(log->mutex);
    log->finished[log->finished_count++] = stage;
    platform_mutex_unlock(log->mutex);
}

static const LoadingStage s_test_stages[] = {
    {"one", 1.0f}, {"two", 2.0f}, {"three", 1.0f}, {"four", 1.0f},
    {"five", 1.0f}, {"six", 1.0f}, {"seven", 1.0f}, {"eight", 1.0f},
};
#define TEST_STAGE_COUNT (int32_t)(sizeof(s_test_stages) / sizeof(s_test_stages[0]))

TEST(loading_runs_every_stage_in_order)
{
    StageLog log = {0};
    log.mutex = platform_mutex_create();
    ASSERT(log.mutex != NULL);
    LoadingState state;
    loading_state_init(&state, TEST_STAGE_COUNT);
    BackgroundLoad *load = loading_start(s_test_stages, TEST_STAGE_COUNT, log_stage, &log);
    ASSERT(load != NULL);

    /* The progress only grows, and reaches 1 with the last stage */
    float progress = 0.0f;
    bool progress_ok = true;
    while (!loading_poll(load, &state, 50))
    {
        progress_ok &= state.progress >= progress && state.progress < 1.0f;
        progress = state.progress;
    }
    loading_finish(load);
    platform_mutex_destroy(log.mutex);

    ASSERT(progress_ok);
    ASSERT_EQ(log.finished_count, TEST_STAGE_COUNT);
    for (int32_t i = 0; i < TEST_STAGE_COUNT; i++)
    {
        ASSERT_EQ(log.started[i], i);
        ASSERT_EQ(log.finished[i], i);
    }
    ASSERT_EQ(state.stage_index, TEST_STAGE_COUNT);
    ASSERT_EQ(state.progress, 1.0f);
    ASSERT(strcmp(state.stage_name, s_test_stages[TEST_STAGE_COUNT - 1].name) == 0);
    return 1;
}

TEST(loading_abandoned_runs_whole_stages_only)
{
    /* Abandoned at once: the stage in progress finishes, in order after those before it,
     * and no later stage starts */
    StageLog log = {0};
    log.mutex = platform_mutex_create();
    ASSERT(log.mutex != NULL);
    BackgroundLoad *load = loading_start(s_test_stages, TEST_STAGE_COUNT, log_stage, &log);
    ASSERT(load != NULL);
    loading_finish(load);
    platform_mutex_destroy(log.mutex);

    printf("(%d of %d stages ran) ", log.finished_count, TEST_STAGE_COUNT);
    ASSERT_EQ(log.started_count, log.finished_count);
    ASSERT(log.finished_count < TEST_STAGE_COUNT);
    for (int32_t i = 0; i < log.finished_count; i++)
        ASSERT_EQ(log.finished[i], i);
    return 1;
}

/* ---- The world scene scene: loading and input ---- */

TEST(world_scene_init_stages_survive_missing_columns)
{
    /* The scene's stages after terrain_columns_build ran out of memory: no terrain but the
     * pillars, and a scene that runs */
    const SceneDescriptor *desc = scene_get_descriptor(SCENE_TYPE_WORLD);
    Scene *scene = world_scene_create(desc->bounds, desc->voxel_size, NULL);
    ASSERT(scene != NULL);
    rng_seed(&scene->rng, 12345);
    WorldSceneData *data = (WorldSceneData *)scene->user_data;
    const WorldSceneParams *p = &data->params;

    world_scene_run_init_stage(scene, 0);
    ASSERT(data->terrain != NULL);
    /* Stage 1 as it ends after the failed build: empty columns, then the pillars */
    terrain_gen_pillars(data->terrain, p->num_pillars, p->terrain_amplitude, p->terrain_frequency, desc->rng_seed);
    ASSERT_EQ(data->columns.nx, 0);
    int64_t pillar_voxels = 0;
    for (int32_t c = 0; c < data->terrain->total_chunks; c++)
        pillar_voxels += data->terrain->chunks[c].occupancy.solid_count;
    ASSERT(pillar_voxels > 0);

    for (int32_t stage = 2; stage < WORLD_SCENE_INIT_STAGES; stage++)
        world_scene_run_init_stage(scene, stage);
    ASSERT_EQ((int64_t)data->terrain->total_solid_voxels, pillar_voxels);
    ASSERT(data->objects && data->physics && data->env_particles);

    for (int32_t tick = 0; tick < 30; tick++)
        scene_update(scene, SIM_TIMESTEP);
    scene_destroy(scene);
    return 1;
}

TEST(world_scene_destroyed_while_loading)
{
    /* Leaving the loading screen half way: destroying the scene lets the stage in progress
     * finish and frees whatever the stages built (the sanitizer builds check the rest) */
    const SceneDescriptor *desc = scene_get_descriptor(SCENE_TYPE_WORLD);
    Scene *scene = world_scene_create(desc->bounds, desc->voxel_size, NULL);
    ASSERT(scene != NULL);
    rng_seed(&scene->rng, 12345);
    LoadingState state;
    loading_state_init(&state, WORLD_SCENE_INIT_STAGES);
    ASSERT(!world_scene_init_step(scene, &state));
    ASSERT(((WorldSceneData *)scene->user_data)->loader != NULL);
    scene_destroy(scene);
    return 1;
}

/* A world scene world: through world_scene_init_step (the app's loading screen) or scene_init */
static Scene *load_world_scene(bool background)
{
    const SceneDescriptor *desc = scene_get_descriptor(SCENE_TYPE_WORLD);
    Scene *scene = world_scene_create(desc->bounds, desc->voxel_size, NULL);
    if (!scene)
        return NULL;
    rng_seed(&scene->rng, 12345);
    if (background)
    {
        LoadingState state;
        loading_state_init(&state, WORLD_SCENE_INIT_STAGES);
        while (!world_scene_init_step(scene, &state))
        {
        }
    }
    else
    {
        scene_init(scene);
    }
    return scene;
}

typedef struct
{
    int32_t solid_before;
    int32_t solid_after;
    int32_t particles;
    uint64_t digest; /* Voxels of the 2 x 2 m column around the spot */
} DigOutcome;

/* Holds the left button for 60 ticks on a ray straight down onto the first open ground
 * found, in frames of frame_dt (SIM_TIMESTEP times a power of two, so the ticks come out
 * exact) */
static bool dig_for_a_second(Scene *scene, float frame_dt, DigOutcome *out)
{
    WorldSceneData *data = (WorldSceneData *)scene->user_data;
    VoxelVolume *terrain = data->terrain;
    Vec3 spot = vec3_zero();
    bool found = false;
    for (int32_t i = 0; i < 256 && !found; i++)
        found = find_open_ground(terrain, -20.0f + 2.5f * (float)(i % 16), -20.0f + 2.5f * (float)(i / 16), 3, 4,
                                 &spot);
    if (!found)
        return false;

    out->solid_before = terrain->total_solid_voxels;
    world_scene_set_ray(scene, vec3_create(spot.x, scene->bounds.max_y, spot.z), vec3_create(0.0f, -1.0f, 0.0f));
    const int32_t end = data->stats.tick_count + 60;
    while (data->stats.tick_count < end)
    {
        scene_handle_input(scene, 0.0f, 0.0f, true, false);
        scene_update(scene, frame_dt);
    }
    out->solid_after = terrain->total_solid_voxels;
    out->particles = data->particles->count;

    uint64_t h = 1469598103934665603ULL;
    const float vs = terrain->voxel_size;
    for (float y = 0.5f * vs; y < scene->bounds.max_y; y += vs)
        for (float z = spot.z - 1.0f; z <= spot.z + 1.0f; z += vs)
            for (float x = spot.x - 1.0f; x <= spot.x + 1.0f; x += vs)
            {
                h ^= volume_get_at(terrain, vec3_create(x, y, z));
                h *= 1099511628211ULL;
            }
    out->digest = h;
    return true;
}

TEST(world_scene_dig_rate_independent_of_frame_rate)
{
    /* The loading screen's world is the one scene_init builds. On it a held left button
     * carves once per tick: a second of holding carves the same at 30 frames per second
     * as at 240. */
    DigOutcome slow, fast;
    Scene *scene = load_world_scene(true);
    ASSERT(scene != NULL);
    bool dug = dig_for_a_second(scene, 2.0f * SIM_TIMESTEP, &slow);
    scene_destroy(scene);
    ASSERT(dug);

    scene = load_world_scene(false);
    ASSERT(scene != NULL);
    dug = dig_for_a_second(scene, 0.25f * SIM_TIMESTEP, &fast);
    scene_destroy(scene);
    ASSERT(dug);

    printf("(removed %d voxels at 30 fps, %d at 240 fps) ", slow.solid_before - slow.solid_after,
           fast.solid_before - fast.solid_after);
    ASSERT_EQ(slow.solid_before, fast.solid_before);
    ASSERT(slow.solid_after < slow.solid_before);
    ASSERT_EQ(slow.solid_after, fast.solid_after);
    ASSERT_EQ(slow.particles, fast.particles);
    ASSERT_EQ(slow.digest, fast.digest);
    return 1;
}

int main(void)
{
    printf("=== Scene Tests ===\n");

    RUN_TEST(flood_drains_serpentine_channel);
    RUN_TEST(flood_fills_closed_basin_to_rim);
    RUN_TEST(tree_generate_overflow_keeps_trunk);
    RUN_TEST(tree_voxelize_keeps_siblings_of_culled_branch);
    RUN_TEST(weather_intensity_settles);
    RUN_TEST(tree_configs_scale_with_world_scale);
    RUN_TEST(leaf_noise_follows_world_seed);
    RUN_TEST(cliffs_show_rock_whichever_way_they_face);
    RUN_TEST(generation_without_columns_writes_nothing);
    RUN_TEST(sun_rises_and_sets_on_opposite_sides);
    RUN_TEST(fireflies_by_night_bees_by_day);
    RUN_TEST(emitter_stops_when_its_voxel_goes);
    RUN_TEST(precipitation_reaches_the_ground_around_the_viewer);
    RUN_TEST(snow_builds_up_only_where_cold);
    RUN_TEST(loading_runs_every_stage_in_order);
    RUN_TEST(loading_abandoned_runs_whole_stages_only);

    RUN_TEST(world_scene_create_destroy);
    RUN_TEST(world_scene_init);
    RUN_TEST(world_scene_tick_increments_stats);
    RUN_TEST(world_scene_stress_env_override);
    RUN_TEST(world_scene_ray_setting);
    RUN_TEST(world_scene_performance);
    RUN_TEST(scene_interp_alpha_clamps);
    RUN_TEST(world_scene_destruction);
    RUN_TEST(world_scene_init_stages_survive_missing_columns);
    RUN_TEST(world_scene_destroyed_while_loading);
    RUN_TEST(world_scene_dig_rate_independent_of_frame_rate);

    printf("\nResults: %d/%d passed\n", g_tests_passed, g_tests_run);
    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
