#include "engine/render/voxel_bricks.h"
#include "engine/sim/detach.h"
#include "content/materials.h"
#include "content/voxel_shapes.h"

#include "test_common.h"

#include <bit>
#include <cstdio>
#include <cstring>
#include <vector>

/*
 * The voxel object brick atlas without a GPU: what ObjectBricks stages is copied into a CPU
 * image of the atlas, and every object is read back through its brick mask and table the
 * way data_voxobj.glsl samples it.
 */

using namespace patch;

static const Bounds3D TEST_BOUNDS = {-64.0f, 64.0f, 0.0f, 64.0f, -64.0f, 64.0f};
static const float TEST_VOXEL_SIZE = 0.1f;

/* The atlas as the GPU holds it: brick contents by atlas brick */
struct CpuAtlas
{
    std::vector<uint8_t> bricks;
    std::vector<uint8_t> staging;
    std::vector<uint16_t> targets;
    uint32_t last_staged = 0;
    bool bad_target = false; /* a brick was staged for a brick outside the atlas */

    explicit CpuAtlas(uint32_t capacity, uint32_t max_bricks)
        : bricks(static_cast<size_t>(capacity) * VOBJ_BRICK_VOXELS, 0xCD),
          staging(static_cast<size_t>(max_bricks) * VOBJ_BRICK_VOXELS), targets(max_bricks)
    {
    }

    /* One frame of VoxelObjects::render's bookkeeping, the copies included */
    void frame(ObjectBricks &bricks_state, const VoxelObjectWorld *world)
    {
        const uint32_t count = static_cast<uint32_t>(world->object_count);
        bricks_state.sync(world, count);
        last_staged = bricks_state.stage(world, count, static_cast<uint32_t>(targets.size()), staging.data(),
                                         targets.data());
        for (uint32_t i = 0; i < last_staged; i++)
        {
            if (static_cast<size_t>(targets[i]) * VOBJ_BRICK_VOXELS >= bricks.size())
            {
                bad_target = true;
                continue;
            }
            memcpy(&bricks[static_cast<size_t>(targets[i]) * VOBJ_BRICK_VOXELS],
                   &staging[static_cast<size_t>(i) * VOBJ_BRICK_VOXELS], VOBJ_BRICK_VOXELS);
        }
    }

    /* data_voxobj.glsl's lookup of grid voxel (x, y, z): 0 before the brick offset (the walk
     * starts there) and for a brick outside the mask */
    uint8_t sample(const VoxelObject *obj, const ObjectBricks::Slot &slot, int32_t x, int32_t y, int32_t z) const
    {
        const int32_t c[3] = {x - obj->brick_offset[0], y - obj->brick_offset[1], z - obj->brick_offset[2]};
        if (c[0] < 0 || c[1] < 0 || c[2] < 0)
            return 0;
        const int32_t b = vobj_brick_index(obj, x, y, z);
        if (!(slot.resident & (1ull << b)))
            return 0;
        const int32_t local = c[0] % VOBJ_BRICK_SIZE +
                              (c[1] % VOBJ_BRICK_SIZE + c[2] % VOBJ_BRICK_SIZE * VOBJ_BRICK_SIZE) * VOBJ_BRICK_SIZE;
        return bricks[static_cast<size_t>(slot.table[b]) * VOBJ_BRICK_VOXELS + static_cast<size_t>(local)];
    }
};

/* Bricks of the object with at least one voxel, found voxel by voxel (a voxel before the brick
 * offset has no brick: atlas_matches finds it missing) */
static uint64_t filled_bricks(const VoxelObject *obj)
{
    uint64_t mask = 0;
    for (int32_t z = obj->brick_offset[2]; z < VOBJ_GRID_SIZE; z++)
        for (int32_t y = obj->brick_offset[1]; y < VOBJ_GRID_SIZE; y++)
            for (int32_t x = obj->brick_offset[0]; x < VOBJ_GRID_SIZE; x++)
                if (obj->voxels[vobj_index(x, y, z)].material != 0)
                    mask |= 1ull << vobj_brick_index(obj, x, y, z);
    return mask;
}

/* The fewest bricks the voxels' bounding box fits in: ceil(extent / VOBJ_BRICK_SIZE) per axis */
static int32_t box_bricks(const VoxelObject *obj)
{
    int32_t lo[3] = {VOBJ_GRID_SIZE, VOBJ_GRID_SIZE, VOBJ_GRID_SIZE}, hi[3] = {-1, -1, -1};
    for (int32_t i = 0; i < VOBJ_TOTAL_VOXELS; i++)
    {
        if (obj->voxels[i].material == 0)
            continue;
        int32_t c[3];
        vobj_coords(i, &c[0], &c[1], &c[2]);
        for (int a = 0; a < 3; a++)
        {
            lo[a] = c[a] < lo[a] ? c[a] : lo[a];
            hi[a] = c[a] > hi[a] ? c[a] : hi[a];
        }
    }
    int32_t bricks = 1;
    for (int a = 0; a < 3; a++)
        bricks *= (hi[a] - lo[a]) / VOBJ_BRICK_SIZE + 1;
    return bricks;
}

/* The atlas shows exactly the object's grid, through exactly its non-empty bricks, each in an
 * atlas brick of its own */
static bool atlas_matches(const CpuAtlas &atlas, const ObjectBricks &bricks, const VoxelObjectWorld *world,
                          uint32_t slot_index)
{
    const VoxelObject *obj = &world->objects[slot_index];
    const ObjectBricks::Slot &slot = bricks.slot(slot_index);
    if (atlas.bad_target || !bricks.drawable(slot_index) || slot.resident != filled_bricks(obj))
        return false;
    for (int32_t z = 0; z < VOBJ_GRID_SIZE; z++)
        for (int32_t y = 0; y < VOBJ_GRID_SIZE; y++)
            for (int32_t x = 0; x < VOBJ_GRID_SIZE; x++)
                if (atlas.sample(obj, slot, x, y, z) != obj->voxels[vobj_index(x, y, z)].material)
                    return false;
    return true;
}

/* No atlas brick is used twice, and the allocator counts exactly the bricks in use */
static bool bricks_disjoint(const ObjectBricks &bricks, const VoxelObjectWorld *world)
{
    std::vector<bool> taken(BRICK_ATLAS_CAPACITY, false);
    uint32_t used = 0;
    for (int32_t i = 0; i < world->object_count; i++)
    {
        const ObjectBricks::Slot &slot = bricks.slot(static_cast<uint32_t>(i));
        for (uint64_t left = slot.resident; left != 0; left &= left - 1)
        {
            const uint16_t brick = slot.table[std::countr_zero(left)];
            if (taken[brick])
                return false;
            taken[brick] = true;
            used++;
        }
    }
    return used == bricks.used_bricks();
}

/* A solid box of material over grid cells [lo, hi] of a size^3 shape, as the game spawns
 * shapes (centred in the grid) */
static int32_t add_box(VoxelObjectWorld *world, int32_t size, int32_t lo, int32_t hi, uint8_t material, Vec3 origin)
{
    std::vector<uint8_t> voxels(static_cast<size_t>(size) * size * size, 0);
    for (int32_t z = lo; z <= hi; z++)
        for (int32_t y = lo; y <= hi; y++)
            for (int32_t x = lo; x <= hi; x++)
                voxels[static_cast<size_t>(x + (y + z * size) * size)] = material;
    return voxel_object_world_add_from_voxels(world, voxels.data(), size, size, size, origin, TEST_VOXEL_SIZE);
}

static int32_t add_shape(VoxelObjectWorld *world, int32_t shape_id, Vec3 origin)
{
    const VoxelShape *shape = voxel_shape_get(shape_id);
    return voxel_object_world_add_from_voxels(world, shape->voxels, shape->size_x, shape->size_y, shape->size_z,
                                              origin, TEST_VOXEL_SIZE);
}

/* Grid centre of voxel (x, y, z) of object obj, in world space (identity orientation) */
static Vec3 voxel_center(const VoxelObject *obj, int32_t x, int32_t y, int32_t z)
{
    const float half = static_cast<float>(VOBJ_GRID_SIZE) * 0.5f;
    return vec3_add(obj->position, vec3_create((static_cast<float>(x) + 0.5f - half) * obj->voxel_size,
                                               (static_cast<float>(y) + 0.5f - half) * obj->voxel_size,
                                               (static_cast<float>(z) + 0.5f - half) * obj->voxel_size));
}

TEST(allocator_order_and_reuse)
{
    BrickAllocator allocator;
    allocator.reset(4);
    ASSERT(allocator.capacity() == 4 && allocator.free_count() == 4);
    for (uint16_t expected = 0; expected < 4; expected++)
        ASSERT(allocator.allocate() == expected);
    ASSERT(allocator.free_count() == 0);
    ASSERT(allocator.allocate() == BrickAllocator::NONE);

    /* Last freed, first reused */
    allocator.release(2);
    allocator.release(0);
    ASSERT(allocator.free_count() == 2);
    ASSERT(allocator.allocate() == 0);
    ASSERT(allocator.allocate() == 2);
    ASSERT(allocator.allocate() == BrickAllocator::NONE);
    return 1;
}

TEST(atlas_origin_covers_atlas)
{
    /* Brick indices run x fastest, then y, then z, over the whole BRICK_ATLAS_SIZE^3 image */
    uint32_t origin[3];
    brick_atlas_origin(0, origin);
    ASSERT(origin[0] == 0 && origin[1] == 0 && origin[2] == 0);
    brick_atlas_origin(1, origin);
    ASSERT(origin[0] == VOBJ_BRICK_SIZE && origin[1] == 0 && origin[2] == 0);
    brick_atlas_origin(BRICK_ATLAS_BRICKS_PER_AXIS, origin);
    ASSERT(origin[0] == 0 && origin[1] == VOBJ_BRICK_SIZE && origin[2] == 0);
    brick_atlas_origin(BRICK_ATLAS_CAPACITY - 1, origin);
    for (int a = 0; a < 3; a++)
        ASSERT(origin[a] == BRICK_ATLAS_SIZE - VOBJ_BRICK_SIZE);
    return 1;
}

TEST(capacity_independent_of_device_limit)
{
    /* The old 32 x 32 x (32 * slots) atlas needed maxImageDimension3D = 16384 for 512 objects and
     * fell to 64 objects at 2048. The brick atlas fits the spec minimum of 256 on every axis and
     * still holds every object slot as a full grid. */
    ASSERT(BRICK_ATLAS_SIZE <= VULKAN_MIN_IMAGE_DIMENSION_3D);
    ASSERT(BRICK_ATLAS_CAPACITY >= static_cast<uint32_t>(VOBJ_MAX_OBJECTS) * VOBJ_BRICK_COUNT);

    VoxelObjectWorld *world = voxel_object_world_create(TEST_BOUNDS, TEST_VOXEL_SIZE);
    ASSERT(world != NULL);
    for (int32_t i = 0; i < VOBJ_MAX_OBJECTS; i++)
        ASSERT(add_box(world, VOBJ_GRID_SIZE, 0, VOBJ_GRID_SIZE - 1, MAT_STONE, vec3_create(0.0f, 10.0f, 0.0f)) == i);

    ObjectBricks bricks;
    bricks.reset(VOBJ_MAX_OBJECTS, BRICK_ATLAS_CAPACITY);
    CpuAtlas atlas(BRICK_ATLAS_CAPACITY, 512);
    for (int frame = 0; frame < VOBJ_MAX_OBJECTS * VOBJ_BRICK_COUNT / 512; frame++)
    {
        atlas.frame(bricks, world);
        ASSERT(atlas.last_staged == 512);
    }
    ASSERT(bricks.used_bricks() == BRICK_ATLAS_CAPACITY);
    bool all_drawn = true;
    for (uint32_t i = 0; i < VOBJ_MAX_OBJECTS; i++)
        all_drawn = all_drawn && atlas_matches(atlas, bricks, world, i);
    ASSERT(all_drawn);
    ASSERT(bricks_disjoint(bricks, world));
    voxel_object_world_destroy(world);
    return 1;
}

TEST(packing_matches_grid)
{
    VoxelObjectWorld *world = voxel_object_world_create(TEST_BOUNDS, TEST_VOXEL_SIZE);
    ASSERT(world != NULL);
    const int32_t shapes[] = {SHAPE_SPHERE, SHAPE_CUBE, SHAPE_GARY};
    for (int32_t shape : shapes)
        ASSERT(add_shape(world, shape, vec3_create(static_cast<float>(shape) * 5.0f, 10.0f, 0.0f)) >= 0);
    /* Full grid, one voxel, and an 8^3 box the centring puts across the grid's middle brick border */
    ASSERT(add_box(world, VOBJ_GRID_SIZE, 0, VOBJ_GRID_SIZE - 1, MAT_STONE, vec3_create(20.0f, 10.0f, 0.0f)) >= 0);
    ASSERT(add_box(world, 1, 0, 0, MAT_METAL, vec3_create(25.0f, 10.0f, 0.0f)) >= 0);
    ASSERT(add_box(world, 12, 2, 9, MAT_DIRT, vec3_create(30.0f, 10.0f, 0.0f)) >= 0);
    ASSERT(world->objects[5].voxels[vobj_index(12, 12, 12)].material == MAT_DIRT);

    ObjectBricks bricks;
    bricks.reset(VOBJ_MAX_OBJECTS, BRICK_ATLAS_CAPACITY);
    CpuAtlas atlas(BRICK_ATLAS_CAPACITY, 512);
    atlas.frame(bricks, world);

    uint32_t expected_uploads = 0;
    for (int32_t i = 0; i < world->object_count; i++)
    {
        ASSERT(atlas_matches(atlas, bricks, world, static_cast<uint32_t>(i)));
        expected_uploads += static_cast<uint32_t>(std::popcount(filled_bricks(&world->objects[i])));
    }
    ASSERT(atlas.last_staged == expected_uploads);
    ASSERT(bricks.used_bricks() == expected_uploads);
    ASSERT(bricks_disjoint(bricks, world));
    ASSERT(std::popcount(bricks.slot(3).resident) == VOBJ_BRICK_COUNT);
    ASSERT(std::popcount(bricks.slot(4).resident) == 1);
    ASSERT(std::popcount(bricks.slot(5).resident) == 1);

    /* The content shapes take no more bricks than their bounding boxes need */
    for (int32_t i = 0; i < 3; i++)
        ASSERT(std::popcount(bricks.slot(static_cast<uint32_t>(i)).resident) <= box_bricks(&world->objects[i]));
    printf("[bricks: sphere %d, cube %d, gary %d] ", std::popcount(bricks.slot(0).resident),
           std::popcount(bricks.slot(1).resident), std::popcount(bricks.slot(2).resident));

    /* Nothing changed: nothing to upload */
    atlas.frame(bricks, world);
    ASSERT(atlas.last_staged == 0);
    voxel_object_world_destroy(world);
    return 1;
}

TEST(boxes_take_the_fewest_bricks)
{
    /* Solid boxes of every size, centred in the grid as the game spawns shapes: n voxels a side
     * take ceil(n / 8)^3 bricks wherever the centring puts them against the grid's middle */
    VoxelObjectWorld *world = voxel_object_world_create(TEST_BOUNDS, TEST_VOXEL_SIZE);
    ASSERT(world != NULL);
    for (int32_t n = 1; n <= VOBJ_GRID_SIZE; n++)
        ASSERT(add_box(world, n, 0, n - 1, MAT_STONE, vec3_create(static_cast<float>(n) * 4.0f, 10.0f, 0.0f)) == n - 1);
    /* Different extents per axis: 3 x 17 x 9 voxels in 1 x 3 x 2 bricks */
    const std::vector<uint8_t> slab(3 * 17 * 9, MAT_METAL);
    const int32_t slab_idx = voxel_object_world_add_from_voxels(world, slab.data(), 3, 17, 9, vec3_create(0.0f, 20.0f, 0.0f),
                                                                TEST_VOXEL_SIZE);
    ASSERT(slab_idx == VOBJ_GRID_SIZE);

    ObjectBricks bricks;
    bricks.reset(VOBJ_MAX_OBJECTS, BRICK_ATLAS_CAPACITY);
    CpuAtlas atlas(BRICK_ATLAS_CAPACITY, 512);
    do
        atlas.frame(bricks, world);
    while (atlas.last_staged > 0);

    for (int32_t n = 1; n <= VOBJ_GRID_SIZE; n++)
    {
        const int32_t per_axis = (n - 1) / VOBJ_BRICK_SIZE + 1;
        ASSERT(atlas_matches(atlas, bricks, world, static_cast<uint32_t>(n - 1)));
        ASSERT(std::popcount(bricks.slot(static_cast<uint32_t>(n - 1)).resident) == per_axis * per_axis * per_axis);
    }
    ASSERT(atlas_matches(atlas, bricks, world, static_cast<uint32_t>(slab_idx)));
    ASSERT(std::popcount(bricks.slot(static_cast<uint32_t>(slab_idx)).resident) == 1 * 3 * 2);
    ASSERT(bricks_disjoint(bricks, world));
    voxel_object_world_destroy(world);
    return 1;
}

TEST(carving_uploads_only_touched_bricks)
{
    VoxelObjectWorld *world = voxel_object_world_create(TEST_BOUNDS, TEST_VOXEL_SIZE);
    ASSERT(world != NULL);
    const int32_t idx = add_box(world, 16, 0, 15, MAT_STONE, vec3_create(0.0f, 10.0f, 0.0f));
    ASSERT(idx == 0);
    VoxelObject *obj = &world->objects[idx];

    ObjectBricks bricks;
    bricks.reset(VOBJ_MAX_OBJECTS, BRICK_ATLAS_CAPACITY);
    CpuAtlas atlas(BRICK_ATLAS_CAPACITY, 512);
    atlas.frame(bricks, world);
    ASSERT(atlas.last_staged == 8); /* cells 8..23: 2 bricks per axis */
    const uint32_t used_before = bricks.used_bricks();

    /* A carve at the corner cell (8, 8, 8): the box keeps its bounds (no recentering) */
    uint32_t stamps_before[VOBJ_BRICK_COUNT];
    memcpy(stamps_before, obj->brick_revision, sizeof(stamps_before));
    const int32_t destroyed = detach_object_at_point(world, idx, voxel_center(obj, 9, 9, 9), 2.5f * TEST_VOXEL_SIZE,
                                                     NULL, NULL, 0);
    ASSERT(destroyed > 0);
    int32_t stamped = 0;
    for (int32_t b = 0; b < VOBJ_BRICK_COUNT; b++)
        stamped += obj->brick_revision[b] != stamps_before[b];
    ASSERT(stamped == 1);
    ASSERT(obj->brick_revision[vobj_brick_index(obj, 9, 9, 9)] == obj->voxel_revision);

    /* One brick re-uploaded, in place: the dirty object's other 7 are untouched */
    atlas.frame(bricks, world);
    ASSERT(atlas.last_staged == 1);
    ASSERT(bricks.used_bricks() == used_before);
    ASSERT(atlas_matches(atlas, bricks, world, 0));

    /* The deferred shape recalc keeps the voxels where they are: no upload */
    voxel_object_world_process_recalcs(world);
    voxel_object_world_process_splits(world);
    ASSERT(world->object_count == 1);
    atlas.frame(bricks, world);
    ASSERT(atlas.last_staged == 0);
    ASSERT(atlas_matches(atlas, bricks, world, 0));
    voxel_object_world_destroy(world);
    return 1;
}

TEST(recentering_reuploads_all_bricks)
{
    VoxelObjectWorld *world = voxel_object_world_create(TEST_BOUNDS, TEST_VOXEL_SIZE);
    ASSERT(world != NULL);
    const int32_t idx = add_box(world, 16, 0, 15, MAT_STONE, vec3_create(0.0f, 10.0f, 0.0f));
    VoxelObject *obj = &world->objects[idx];

    ObjectBricks bricks;
    bricks.reset(VOBJ_MAX_OBJECTS, BRICK_ATLAS_CAPACITY);
    CpuAtlas atlas(BRICK_ATLAS_CAPACITY, 512);
    atlas.frame(bricks, world);
    ASSERT(atlas.last_staged == 8); /* cells 8..23: 2 bricks per axis */

    /* Carving off the bottom layer (y = 8) leaves cells 9..23 on y: the recalc moves every
     * voxel down by one to recentre them */
    for (int32_t z = 8; z < 24; z++)
        for (int32_t x = 8; x < 24; x++)
            detach_object_at_point(world, idx, voxel_center(obj, x, 8, z), 0.4f * TEST_VOXEL_SIZE, NULL, NULL, 0);
    atlas.frame(bricks, world);
    ASSERT(atlas_matches(atlas, bricks, world, 0));

    const Vec3 position_before = obj->position;
    voxel_object_world_process_recalcs(world);
    ASSERT(obj->position.y != position_before.y);
    for (int32_t b = 0; b < VOBJ_BRICK_COUNT; b++)
        ASSERT(obj->brick_revision[b] == obj->voxel_revision);
    atlas.frame(bricks, world);
    ASSERT(atlas.last_staged == static_cast<uint32_t>(std::popcount(filled_bricks(obj))));
    ASSERT(atlas_matches(atlas, bricks, world, 0));
    ASSERT(bricks_disjoint(bricks, world));
    voxel_object_world_destroy(world);
    return 1;
}

TEST(brick_offset_move_reuploads_all_bricks)
{
    /* A 19^3 box at cells 6..24, its bricks from cell 6. Carving off the x = 6 face leaves
     * cells 7..24 on x, still centred (the recalc moves no voxel), but the bricks now start at
     * cell 7: every brick holds other voxels than before. */
    VoxelObjectWorld *world = voxel_object_world_create(TEST_BOUNDS, TEST_VOXEL_SIZE);
    ASSERT(world != NULL);
    const int32_t idx = add_box(world, 19, 0, 18, MAT_STONE, vec3_create(0.0f, 10.0f, 0.0f));
    VoxelObject *obj = &world->objects[idx];
    ASSERT(obj->brick_offset[0] == 6);

    ObjectBricks bricks;
    bricks.reset(VOBJ_MAX_OBJECTS, BRICK_ATLAS_CAPACITY);
    CpuAtlas atlas(BRICK_ATLAS_CAPACITY, 512);
    atlas.frame(bricks, world);
    ASSERT(atlas.last_staged == 3 * 3 * 3);

    for (int32_t z = 6; z <= 24; z++)
        for (int32_t y = 6; y <= 24; y++)
            detach_object_at_point(world, idx, voxel_center(obj, 6, y, z), 0.4f * TEST_VOXEL_SIZE, NULL, NULL, 0);
    atlas.frame(bricks, world);
    ASSERT(atlas_matches(atlas, bricks, world, 0));

    const Vec3 position_before = obj->position;
    voxel_object_world_process_recalcs(world);
    ASSERT(obj->position.x == position_before.x && obj->position.y == position_before.y &&
           obj->position.z == position_before.z);
    ASSERT(obj->brick_offset[0] == 7 && obj->brick_offset[1] == 6 && obj->brick_offset[2] == 6);
    for (int32_t b = 0; b < VOBJ_BRICK_COUNT; b++)
        ASSERT(obj->brick_revision[b] == obj->voxel_revision);
    atlas.frame(bricks, world);
    ASSERT(atlas.last_staged == 3 * 3 * 3);
    ASSERT(atlas_matches(atlas, bricks, world, 0));
    ASSERT(bricks_disjoint(bricks, world));
    voxel_object_world_destroy(world);
    return 1;
}

TEST(carving_and_splitting_keep_the_atlas_exact)
{
    /* Carves of varying size at pseudo-random voxels, each frame running the deferred splits and
     * recalcs (recentring, offset moves, new pieces). After every frame each object reads back
     * exactly, and once recalculated takes no more bricks than its bounding box needs. */
    VoxelObjectWorld *world = voxel_object_world_create(TEST_BOUNDS, TEST_VOXEL_SIZE);
    ASSERT(world != NULL);
    const int32_t shapes[] = {SHAPE_SPHERE, SHAPE_CUBE, SHAPE_GARY};
    for (int32_t shape : shapes)
        ASSERT(add_shape(world, shape, vec3_create(static_cast<float>(shape) * 5.0f, 10.0f, 0.0f)) >= 0);
    ASSERT(add_box(world, 20, 0, 19, MAT_STONE, vec3_create(20.0f, 10.0f, 0.0f)) >= 0);
    ASSERT(add_box(world, 27, 0, 26, MAT_DIRT, vec3_create(30.0f, 10.0f, 0.0f)) >= 0);

    ObjectBricks bricks;
    bricks.reset(VOBJ_MAX_OBJECTS, BRICK_ATLAS_CAPACITY);
    CpuAtlas atlas(BRICK_ATLAS_CAPACITY, 512);
    atlas.frame(bricks, world);

    uint32_t rng = 12345;
    const auto next = [&rng](uint32_t range) {
        rng = rng * 1664525u + 1013904223u;
        return (rng >> 8) % range;
    };
    for (int32_t round = 0; round < 48; round++)
    {
        /* An object with voxels, and a solid voxel of it */
        int32_t idx = static_cast<int32_t>(next(static_cast<uint32_t>(world->object_count)));
        while (!world->objects[idx].active)
            idx = (idx + 1) % world->object_count;
        VoxelObject *obj = &world->objects[idx];
        int32_t voxel = static_cast<int32_t>(next(VOBJ_TOTAL_VOXELS));
        while (obj->voxels[voxel].material == 0)
            voxel = (voxel + 1) % VOBJ_TOTAL_VOXELS;
        int32_t x, y, z;
        vobj_coords(voxel, &x, &y, &z);
        const float radius = (0.5f + static_cast<float>(next(4))) * TEST_VOXEL_SIZE;
        ASSERT(detach_object_at_point(world, idx, voxel_center(obj, x, y, z), radius, NULL, NULL, 0) > 0);

        voxel_object_world_process_splits(world);
        voxel_object_world_process_recalcs(world);
        atlas.frame(bricks, world);
        for (int32_t i = 0; i < world->object_count; i++)
        {
            const VoxelObject *o = &world->objects[i];
            if (!o->active)
                continue;
            ASSERT(atlas_matches(atlas, bricks, world, static_cast<uint32_t>(i)));
            ASSERT(o->shape_dirty || std::popcount(bricks.slot(static_cast<uint32_t>(i)).resident) <= box_bricks(o));
        }
        ASSERT(bricks_disjoint(bricks, world));
    }
    voxel_object_world_destroy(world);
    return 1;
}

TEST(emptied_and_removed_bricks_are_freed)
{
    VoxelObjectWorld *world = voxel_object_world_create(TEST_BOUNDS, TEST_VOXEL_SIZE);
    ASSERT(world != NULL);
    /* Two voxels, at grid cells (11, 11, 11) and (19, 19, 19): with the bricks starting at cell
     * 11, bricks (0, 0, 0) and (1, 1, 1) */
    std::vector<uint8_t> voxels(9 * 9 * 9, 0);
    voxels[0] = MAT_STONE;
    voxels[8 + (8 + 8 * 9) * 9] = MAT_METAL;
    const int32_t idx = voxel_object_world_add_from_voxels(world, voxels.data(), 9, 9, 9, vec3_create(0.0f, 10.0f, 0.0f),
                                                           TEST_VOXEL_SIZE);
    ASSERT(idx == 0);
    VoxelObject *obj = &world->objects[idx];
    const int32_t other = add_box(world, 8, 0, 7, MAT_DIRT, vec3_create(5.0f, 10.0f, 0.0f));
    ASSERT(other == 1);

    ObjectBricks bricks;
    bricks.reset(VOBJ_MAX_OBJECTS, BRICK_ATLAS_CAPACITY);
    CpuAtlas atlas(BRICK_ATLAS_CAPACITY, 512);
    atlas.frame(bricks, world);
    ASSERT(std::popcount(bricks.slot(0).resident) == 2);
    ASSERT(bricks.used_bricks() == 2 + 1);

    /* Carve one voxel away: its brick is empty and goes back to the allocator */
    int32_t gx = -1;
    for (int32_t i = 0; i < VOBJ_TOTAL_VOXELS && gx < 0; i++)
        if (obj->voxels[i].material == MAT_STONE)
            gx = i;
    int32_t x, y, z;
    vobj_coords(gx, &x, &y, &z);
    ASSERT(detach_object_at_point(world, idx, voxel_center(obj, x, y, z), 0.4f * TEST_VOXEL_SIZE, NULL, NULL, 0) == 1);
    atlas.frame(bricks, world);
    ASSERT(atlas.last_staged == 0);
    ASSERT(std::popcount(bricks.slot(0).resident) == 1);
    ASSERT(bricks.used_bricks() == 1 + 1);
    ASSERT(atlas_matches(atlas, bricks, world, 0));

    /* Removed objects give back every brick */
    voxel_object_world_free_slot(world, other);
    atlas.frame(bricks, world);
    ASSERT(bricks.used_bricks() == 1);
    ASSERT(bricks.slot(1).resident == 0 && !bricks.drawable(1));

    /* The slot's next occupant is uploaded whole, without its predecessor's bricks */
    ASSERT(add_box(world, 4, 0, 3, MAT_METAL, vec3_create(5.0f, 10.0f, 0.0f)) == other);
    atlas.frame(bricks, world);
    ASSERT(atlas_matches(atlas, bricks, world, 1));
    ASSERT(bricks_disjoint(bricks, world));
    voxel_object_world_destroy(world);
    return 1;
}

TEST(split_moves_bricks_to_new_object)
{
    VoxelObjectWorld *world = voxel_object_world_create(TEST_BOUNDS, TEST_VOXEL_SIZE);
    ASSERT(world != NULL);
    /* Two 6^3 blocks joined by a one-voxel bridge along x */
    const int32_t size = 20;
    std::vector<uint8_t> voxels(static_cast<size_t>(size) * size * size, 0);
    for (int32_t z = 0; z < 6; z++)
        for (int32_t y = 0; y < 6; y++)
            for (int32_t x = 0; x < size; x++)
                if (x < 6 || x >= size - 6 || (y == 2 && z == 2))
                    voxels[static_cast<size_t>(x + (y + z * size) * size)] = MAT_STONE;
    const int32_t idx = voxel_object_world_add_from_voxels(world, voxels.data(), size, size, size,
                                                           vec3_create(0.0f, 10.0f, 0.0f), TEST_VOXEL_SIZE);
    ASSERT(idx == 0);
    VoxelObject *obj = &world->objects[idx];

    ObjectBricks bricks;
    bricks.reset(VOBJ_MAX_OBJECTS, BRICK_ATLAS_CAPACITY);
    CpuAtlas atlas(BRICK_ATLAS_CAPACITY, 512);
    atlas.frame(bricks, world);
    ASSERT(atlas_matches(atlas, bricks, world, 0));

    /* Cut the bridge (the only voxels on x = 16), then let the deferred work split and recentre */
    int32_t cut = -1;
    for (int32_t i = 0; i < VOBJ_TOTAL_VOXELS && cut < 0; i++)
        if (obj->voxels[i].material != 0 && i % VOBJ_GRID_SIZE == 16)
            cut = i;
    ASSERT(cut >= 0);
    int32_t cx, cy, cz;
    vobj_coords(cut, &cx, &cy, &cz);
    ASSERT(detach_object_at_point(world, idx, voxel_center(obj, cx, cy, cz), 0.6f * TEST_VOXEL_SIZE, NULL, NULL, 0) == 1);
    for (int tick = 0; tick < 4; tick++)
    {
        voxel_object_world_process_splits(world);
        voxel_object_world_process_recalcs(world);
        atlas.frame(bricks, world);
        for (int32_t i = 0; i < world->object_count; i++)
            ASSERT(!world->objects[i].active || atlas_matches(atlas, bricks, world, static_cast<uint32_t>(i)));
    }
    int32_t active = 0;
    for (int32_t i = 0; i < world->object_count; i++)
        active += world->objects[i].active;
    ASSERT(active == 2);
    ASSERT(bricks_disjoint(bricks, world));
    voxel_object_world_destroy(world);
    return 1;
}

TEST(split_stamps_the_bricks_it_empties)
{
    /* A hollow 10^3 shell (grid cells 11..20) around a loose voxel at cell 16: splitting the
     * voxel off leaves the shell's bounds, so no recentering re-uploads the shell */
    VoxelObjectWorld *world = voxel_object_world_create(TEST_BOUNDS, TEST_VOXEL_SIZE);
    ASSERT(world != NULL);
    const int32_t size = 10;
    std::vector<uint8_t> voxels(static_cast<size_t>(size) * size * size, 0);
    for (int32_t z = 0; z < size; z++)
        for (int32_t y = 0; y < size; y++)
            for (int32_t x = 0; x < size; x++)
                if (x == 0 || y == 0 || z == 0 || x == size - 1 || y == size - 1 || z == size - 1)
                    voxels[static_cast<size_t>(x + (y + z * size) * size)] = MAT_STONE;
    voxels[static_cast<size_t>(5 + (5 + 5 * size) * size)] = MAT_METAL;
    const int32_t idx = voxel_object_world_add_from_voxels(world, voxels.data(), size, size, size,
                                                           vec3_create(0.0f, 10.0f, 0.0f), TEST_VOXEL_SIZE);
    ASSERT(idx == 0);
    VoxelObject *obj = &world->objects[idx];
    ASSERT(obj->voxels[vobj_index(16, 16, 16)].material == MAT_METAL);

    ObjectBricks bricks;
    bricks.reset(VOBJ_MAX_OBJECTS, BRICK_ATLAS_CAPACITY);
    CpuAtlas atlas(BRICK_ATLAS_CAPACITY, 512);
    atlas.frame(bricks, world);

    /* A carve on a shell face queues the split. The bricks start at cell 11: the carve is in
     * brick (1, 0, 0), the loose voxel in brick (0, 0, 0). */
    ASSERT(obj->brick_offset[0] == 11 && obj->brick_offset[1] == 11 && obj->brick_offset[2] == 11);
    ASSERT(detach_object_at_point(world, idx, voxel_center(obj, 20, 13, 13), 0.4f * TEST_VOXEL_SIZE, NULL, NULL, 0) == 1);
    voxel_object_world_process_splits(world);
    voxel_object_world_process_recalcs(world);
    ASSERT(world->object_count == 2 && world->objects[1].active);
    ASSERT(obj->voxels[vobj_index(16, 16, 16)].material == 0);
    atlas.frame(bricks, world);
    ASSERT(atlas.last_staged == 2 + 1); /* the carved brick, the brick the loose voxel left, the new object's brick */
    ASSERT(atlas_matches(atlas, bricks, world, 0));
    ASSERT(atlas_matches(atlas, bricks, world, 1));
    voxel_object_world_destroy(world);
    return 1;
}

TEST(budget_uploads_whole_objects)
{
    VoxelObjectWorld *world = voxel_object_world_create(TEST_BOUNDS, TEST_VOXEL_SIZE);
    ASSERT(world != NULL);
    for (int32_t i = 0; i < 3; i++)
        ASSERT(add_box(world, 16, 0, 15, MAT_STONE, vec3_create(static_cast<float>(i) * 4.0f, 10.0f, 0.0f)) == i);

    /* 8 bricks each; 20 per frame take two objects, never part of the third */
    ObjectBricks bricks;
    bricks.reset(VOBJ_MAX_OBJECTS, BRICK_ATLAS_CAPACITY);
    CpuAtlas atlas(BRICK_ATLAS_CAPACITY, 20);
    atlas.frame(bricks, world);
    ASSERT(atlas.last_staged == 16);
    ASSERT(bricks.drawable(0) && bricks.drawable(1) && !bricks.drawable(2));
    ASSERT(bricks.slot(2).resident == 0);
    atlas.frame(bricks, world);
    ASSERT(atlas.last_staged == 8);
    for (uint32_t i = 0; i < 3; i++)
        ASSERT(atlas_matches(atlas, bricks, world, i));
    voxel_object_world_destroy(world);
    return 1;
}

TEST(exhausted_atlas_skips_objects_that_do_not_fit)
{
    VoxelObjectWorld *world = voxel_object_world_create(TEST_BOUNDS, TEST_VOXEL_SIZE);
    ASSERT(world != NULL);
    ASSERT(add_box(world, 16, 0, 15, MAT_STONE, vec3_create(0.0f, 10.0f, 0.0f)) == 0); /* 8 bricks */
    ASSERT(add_box(world, 16, 0, 15, MAT_METAL, vec3_create(4.0f, 10.0f, 0.0f)) == 1); /* 8 bricks */
    ASSERT(add_box(world, 1, 0, 0, MAT_DIRT, vec3_create(8.0f, 10.0f, 0.0f)) == 2);    /* 1 brick */

    /* 12 bricks: the second object does not fit, the third still does */
    ObjectBricks bricks;
    bricks.reset(VOBJ_MAX_OBJECTS, 12);
    CpuAtlas atlas(12, 512);
    atlas.frame(bricks, world);
    ASSERT(atlas_matches(atlas, bricks, world, 0));
    ASSERT(!bricks.drawable(1) && bricks.slot(1).resident == 0);
    ASSERT(atlas_matches(atlas, bricks, world, 2));
    ASSERT(bricks.used_bricks() == 9);

    /* Retried every frame; drawn once the first object is gone */
    atlas.frame(bricks, world);
    ASSERT(!bricks.drawable(1));
    voxel_object_world_free_slot(world, 0);
    atlas.frame(bricks, world);
    ASSERT(atlas_matches(atlas, bricks, world, 1));
    ASSERT(atlas_matches(atlas, bricks, world, 2));
    ASSERT(bricks.used_bricks() == 9);
    ASSERT(bricks_disjoint(bricks, world));
    voxel_object_world_destroy(world);
    return 1;
}

/* Table and mask of every slot after a fixed run of spawns, carves and removals */
static bool scripted_run(std::vector<ObjectBricks::Slot> *out)
{
    VoxelObjectWorld *world = voxel_object_world_create(TEST_BOUNDS, TEST_VOXEL_SIZE);
    if (!world)
        return false;
    ObjectBricks bricks;
    bricks.reset(VOBJ_MAX_OBJECTS, BRICK_ATLAS_CAPACITY);
    CpuAtlas atlas(BRICK_ATLAS_CAPACITY, 512);
    for (int32_t i = 0; i < 12; i++)
        add_shape(world, i % 3, vec3_create(static_cast<float>(i) * 4.0f, 10.0f, 0.0f));
    atlas.frame(bricks, world);
    for (int32_t i = 0; i < 12; i += 2)
    {
        VoxelObject *obj = &world->objects[i];
        detach_object_at_point(world, i, voxel_center(obj, 16, 16, 16), 4.0f * TEST_VOXEL_SIZE, NULL, NULL, 0);
    }
    voxel_object_world_free_slot(world, 3);
    atlas.frame(bricks, world);
    voxel_object_world_process_splits(world);
    voxel_object_world_process_recalcs(world);
    add_shape(world, SHAPE_GARY, vec3_create(0.0f, 20.0f, 0.0f));
    atlas.frame(bricks, world);
    out->clear();
    for (int32_t i = 0; i < world->object_count; i++)
        out->push_back(bricks.slot(static_cast<uint32_t>(i)));
    voxel_object_world_destroy(world);
    return true;
}

TEST(deterministic_brick_assignment)
{
    std::vector<ObjectBricks::Slot> first, second;
    ASSERT(scripted_run(&first));
    ASSERT(scripted_run(&second));
    ASSERT(first.size() == second.size());
    for (size_t i = 0; i < first.size(); i++)
    {
        ASSERT(first[i].resident == second[i].resident);
        for (uint64_t left = first[i].resident; left != 0; left &= left - 1)
        {
            const int b = std::countr_zero(left);
            ASSERT(first[i].table[b] == second[i].table[b]);
        }
    }
    return 1;
}

int main()
{
    printf("=== Voxel Object Brick Atlas Tests (Non-GPU) ===\n\n");
    RUN_TEST(allocator_order_and_reuse);
    RUN_TEST(atlas_origin_covers_atlas);
    RUN_TEST(capacity_independent_of_device_limit);
    RUN_TEST(packing_matches_grid);
    RUN_TEST(boxes_take_the_fewest_bricks);
    RUN_TEST(carving_uploads_only_touched_bricks);
    RUN_TEST(recentering_reuploads_all_bricks);
    RUN_TEST(brick_offset_move_reuploads_all_bricks);
    RUN_TEST(carving_and_splitting_keep_the_atlas_exact);
    RUN_TEST(emptied_and_removed_bricks_are_freed);
    RUN_TEST(split_moves_bricks_to_new_object);
    RUN_TEST(split_stamps_the_bricks_it_empties);
    RUN_TEST(budget_uploads_whole_objects);
    RUN_TEST(exhausted_atlas_skips_objects_that_do_not_fit);
    RUN_TEST(deterministic_brick_assignment);

    printf("\nResults: %d/%d passed\n", g_tests_passed, g_tests_run);
    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
