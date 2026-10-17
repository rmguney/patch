#ifndef PATCH_RENDER_VOXEL_BRICKS_H
#define PATCH_RENDER_VOXEL_BRICKS_H

#include "engine/voxel/voxel_object.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace patch
{

    /*
     * Voxel object atlas layout (no Vulkan here: tested without a GPU). The atlas is one
     * BRICK_ATLAS_SIZE^3 R8_UINT image of VOBJ_BRICK_SIZE^3 bricks; an object's grid is
     * VOBJ_BRICKS_PER_AXIS^3 bricks from its brick_offset on, and only its non-empty ones take
     * atlas bricks. 256 is the smallest maxImageDimension3D Vulkan allows, so every device
     * creates the full atlas.
     */
    constexpr uint32_t BRICK_ATLAS_SIZE = 256;
    constexpr uint32_t BRICK_ATLAS_BRICKS_PER_AXIS = BRICK_ATLAS_SIZE / VOBJ_BRICK_SIZE;
    constexpr uint32_t BRICK_ATLAS_CAPACITY =
        BRICK_ATLAS_BRICKS_PER_AXIS * BRICK_ATLAS_BRICKS_PER_AXIS * BRICK_ATLAS_BRICKS_PER_AXIS;
    constexpr uint32_t VOBJ_BRICK_VOXELS = VOBJ_BRICK_SIZE * VOBJ_BRICK_SIZE * VOBJ_BRICK_SIZE;
    constexpr uint32_t VULKAN_MIN_IMAGE_DIMENSION_3D = 256; /* lowest maxImageDimension3D the spec allows */
    static_assert(BRICK_ATLAS_SIZE <= VULKAN_MIN_IMAGE_DIMENSION_3D, "the atlas must fit every Vulkan device");
    static_assert(BRICK_ATLAS_CAPACITY <= UINT16_MAX, "atlas bricks are 16-bit table entries");
    static_assert(VOBJ_BRICK_COUNT == 64, "brick masks are 64-bit");
    static_assert(sizeof(VObjVoxel) == 1, "grid rows are copied as bytes");

    /* GPU metadata for raymarched voxel objects (mirrors VoxelObjectGPU in shaders/include/voxobj_types.glsl) */
    struct alignas(16) VoxelObjectGPU
    {
        float world_to_local[16]; /* mat4: rotation + translation, no scale */
        float local_to_world[16]; /* mat4: rotation * voxel_size + translation */
        float bounds_min[4];      /* Object AABB min (xyz), voxel_size (w) */
        float bounds_max[4];      /* Object AABB max (xyz), grid_size (w) */
        float position[4];        /* World position (xyz), 1 = active and uploaded (w) */
        uint32_t brick_mask[2];   /* Bit b (vobj_brick_index): brick b is in the atlas */
        uint32_t brick_offset;    /* VoxelObject::brick_offset: x | y << 8 | z << 16 */
        uint32_t pad;
        /* Atlas brick of each brick in brick_mask (other entries undefined). The shader reads
         * the pairs as uint brick_table[32]: entry b in bits (b & 1) * 16 of uint b / 2. */
        uint16_t brick_table[VOBJ_BRICK_COUNT];
        float prev_local_to_world[16]; /* mat4: local_to_world of the previous frame (motion vectors) */
    };
    static_assert(sizeof(VoxelObjectGPU) == 384, "VoxelObjectGPU must be 384 bytes");
    /* std430 offsets of the GLSL struct (tests/test_gpu_layout.cpp checks them against the GLSL) */
    static_assert(offsetof(VoxelObjectGPU, local_to_world) == 64, "VoxelObjectGPU.local_to_world offset");
    static_assert(offsetof(VoxelObjectGPU, bounds_min) == 128, "VoxelObjectGPU.bounds_min offset");
    static_assert(offsetof(VoxelObjectGPU, position) == 160, "VoxelObjectGPU.position offset");
    static_assert(offsetof(VoxelObjectGPU, brick_mask) == 176, "VoxelObjectGPU.brick_mask offset");
    static_assert(offsetof(VoxelObjectGPU, brick_offset) == 184, "VoxelObjectGPU.brick_offset offset");
    static_assert(offsetof(VoxelObjectGPU, brick_table) == 192, "VoxelObjectGPU.brick_table offset");
    static_assert(offsetof(VoxelObjectGPU, prev_local_to_world) == 320, "VoxelObjectGPU.prev_local_to_world offset");

    /* Atlas texel of the first voxel of atlas brick `brick` (x fastest, then y, then z) */
    inline void brick_atlas_origin(uint32_t brick, uint32_t origin[3])
    {
        origin[0] = brick % BRICK_ATLAS_BRICKS_PER_AXIS * VOBJ_BRICK_SIZE;
        origin[1] = brick / BRICK_ATLAS_BRICKS_PER_AXIS % BRICK_ATLAS_BRICKS_PER_AXIS * VOBJ_BRICK_SIZE;
        origin[2] = brick / (BRICK_ATLAS_BRICKS_PER_AXIS * BRICK_ATLAS_BRICKS_PER_AXIS) * VOBJ_BRICK_SIZE;
    }

    /* Atlas bricks not in use. A LIFO free list: the same calls hand out the same bricks. */
    class BrickAllocator
    {
    public:
        static constexpr uint16_t NONE = UINT16_MAX;

        /* All of [0, capacity) free, handed out in ascending order */
        void reset(uint32_t capacity);
        /* NONE when every brick is in use */
        uint16_t allocate();
        void release(uint16_t brick);

        uint32_t capacity() const { return static_cast<uint32_t>(free_.size()); }
        uint32_t free_count() const { return free_count_; }

    private:
        std::vector<uint16_t> free_; /* stack: [0, free_count_) are free, the top is next */
        uint32_t free_count_ = 0;
    };

    /*
     * Which bricks of every object slot are in the atlas, where, and which still need an upload.
     * sync() picks up the bricks the world changed (brick_revision stamps it has not seen), and
     * stage() packs them for upload a whole object at a time: an object is drawn only when
     * every changed brick of it is uploaded.
     *
     * Out of atlas bricks (an object's new bricks do not fit): the object gives back all of its
     * bricks and is not drawn, a warning is printed once, and it is retried every frame until
     * enough bricks are free. With VOBJ_MAX_OBJECTS full grids fitting the atlas (512 * 64 =
     * BRICK_ATLAS_CAPACITY) the renderer cannot run out.
     */
    class ObjectBricks
    {
    public:
        struct Slot
        {
            uint64_t resident = 0;                 /* bricks in the atlas */
            uint64_t pending = 0;                  /* bricks changed since their last upload */
            uint32_t revision = 0;                 /* voxel_revision last synced */
            uint32_t seen[VOBJ_BRICK_COUNT] = {};  /* brick_revision stamps last synced */
            uint16_t table[VOBJ_BRICK_COUNT] = {}; /* atlas brick of each resident brick */
        };

        /* Every slot empty, every brick free */
        void reset(uint32_t slot_count, uint32_t brick_capacity);

        /* Changes of slots [0, count): removed objects give their bricks back, new and edited
         * ones get their changed bricks pending */
        void sync(const VoxelObjectWorld *world, uint32_t count);

        /*
         * Pending bricks of whole objects, in slot order, while they fit max_bricks: staged
         * brick i (VOBJ_BRICK_VOXELS bytes at staging + i * VOBJ_BRICK_VOXELS, x fastest) goes
         * to atlas brick out_bricks[i]. Bricks that became empty go back to the allocator.
         * Returns the number of bricks staged.
         */
        uint32_t stage(const VoxelObjectWorld *world, uint32_t count, uint32_t max_bricks, uint8_t *staging,
                       uint16_t *out_bricks);

        /* Every changed brick of the object is uploaded (an active object has at least one brick) */
        bool drawable(uint32_t slot) const { return slots_[slot].pending == 0 && slots_[slot].resident != 0; }
        const Slot &slot(uint32_t slot) const { return slots_[slot]; }
        uint32_t used_bricks() const { return allocator_.capacity() - allocator_.free_count(); }

    private:
        void release(Slot &slot, uint64_t bricks);

        BrickAllocator allocator_;
        std::vector<Slot> slots_;
        bool warned_full_ = false;
    };

}

#endif
