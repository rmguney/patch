#include "voxel_bricks.h"
#include <bit>
#include <cstdio>
#include <cstring>

namespace patch
{

    void BrickAllocator::reset(uint32_t capacity)
    {
        free_.resize(capacity);
        for (uint32_t i = 0; i < capacity; i++)
            free_[i] = static_cast<uint16_t>(capacity - 1 - i);
        free_count_ = capacity;
    }

    uint16_t BrickAllocator::allocate()
    {
        return free_count_ > 0 ? free_[--free_count_] : NONE;
    }

    void BrickAllocator::release(uint16_t brick)
    {
        free_[free_count_++] = brick;
    }

    /* Grid cell of the first voxel of brick `brick` of obj */
    static void brick_grid_origin(const VoxelObject *obj, uint32_t brick, int32_t origin[3])
    {
        origin[0] = obj->brick_offset[0] + static_cast<int32_t>(brick % VOBJ_BRICKS_PER_AXIS) * VOBJ_BRICK_SIZE;
        origin[1] = obj->brick_offset[1] +
                    static_cast<int32_t>(brick / VOBJ_BRICKS_PER_AXIS % VOBJ_BRICKS_PER_AXIS) * VOBJ_BRICK_SIZE;
        origin[2] = obj->brick_offset[2] +
                    static_cast<int32_t>(brick / (VOBJ_BRICKS_PER_AXIS * VOBJ_BRICKS_PER_AXIS)) * VOBJ_BRICK_SIZE;
    }

    /* A brick's rows are VOBJ_BRICK_SIZE contiguous bytes of the grid: one 64-bit load each */
    static_assert(VOBJ_BRICK_SIZE == sizeof(uint64_t), "brick rows are read as 64-bit words");

    static bool brick_has_voxels(const VoxelObject *obj, uint32_t brick)
    {
        int32_t o[3];
        brick_grid_origin(obj, brick, o);
        /* The bricks past the voxels may reach beyond the grid: no voxels there */
        constexpr int32_t LAST_ORIGIN = VOBJ_GRID_SIZE - VOBJ_BRICK_SIZE;
        if (o[0] > LAST_ORIGIN || o[1] > LAST_ORIGIN || o[2] > LAST_ORIGIN)
            return false;
        uint64_t any = 0;
        for (int32_t z = o[2]; z < o[2] + VOBJ_BRICK_SIZE; z++)
        {
            for (int32_t y = o[1]; y < o[1] + VOBJ_BRICK_SIZE; y++)
            {
                uint64_t row;
                memcpy(&row, &obj->voxels[vobj_index(o[0], y, z)], sizeof(row));
                any |= row;
            }
        }
        return any != 0;
    }

    /* A brick with voxels (brick_has_voxels), so it lies inside the grid */
    static void pack_brick(const VoxelObject *obj, uint32_t brick, uint8_t *out)
    {
        int32_t o[3];
        brick_grid_origin(obj, brick, o);
        for (int32_t z = o[2]; z < o[2] + VOBJ_BRICK_SIZE; z++)
        {
            for (int32_t y = o[1]; y < o[1] + VOBJ_BRICK_SIZE; y++)
            {
                memcpy(out, &obj->voxels[vobj_index(o[0], y, z)], VOBJ_BRICK_SIZE);
                out += VOBJ_BRICK_SIZE;
            }
        }
    }

    void ObjectBricks::reset(uint32_t slot_count, uint32_t brick_capacity)
    {
        allocator_.reset(brick_capacity);
        slots_.assign(slot_count, Slot{});
        warned_full_ = false;
    }

    void ObjectBricks::release(Slot &slot, uint64_t bricks)
    {
        for (uint64_t left = bricks & slot.resident; left != 0; left &= left - 1)
            /* NOLINTNEXTLINE(clang-analyzer-security.ArrayBound): countr_zero of a nonzero 64-bit mask is below 64 */
            allocator_.release(slot.table[std::countr_zero(left)]);
        slot.resident &= ~bricks;
    }

    void ObjectBricks::sync(const VoxelObjectWorld *world, uint32_t count)
    {
        for (uint32_t i = 0; i < count; i++)
        {
            const VoxelObject *obj = &world->objects[i];
            Slot &slot = slots_[i];
            if (!obj->active)
            {
                /* Forget the stamps too: whatever takes the slot next is uploaded whole */
                if (slot.revision != 0)
                {
                    release(slot, VOBJ_ALL_BRICKS);
                    slot = Slot{};
                }
                continue;
            }
            if (obj->voxel_revision == slot.revision)
                continue;

            slot.revision = obj->voxel_revision;
            for (uint32_t b = 0; b < VOBJ_BRICK_COUNT; b++)
            {
                if (obj->brick_revision[b] != slot.seen[b])
                {
                    slot.seen[b] = obj->brick_revision[b];
                    slot.pending |= 1ull << b;
                }
            }
        }
    }

    uint32_t ObjectBricks::stage(const VoxelObjectWorld *world, uint32_t count, uint32_t max_bricks, uint8_t *staging,
                                 uint16_t *out_bricks)
    {
        uint32_t staged = 0;
        for (uint32_t i = 0; i < count; i++)
        {
            Slot &slot = slots_[i];
            if (slot.pending == 0)
                continue;

            const VoxelObject *obj = &world->objects[i];
            uint64_t filled = 0;
            for (uint64_t left = slot.pending; left != 0; left &= left - 1)
            {
                const uint32_t b = static_cast<uint32_t>(std::countr_zero(left));
                if (brick_has_voxels(obj, b))
                    filled |= 1ull << b;
            }

            /* Whole objects only: a half-updated object is never drawn */
            if (static_cast<uint32_t>(std::popcount(filled)) > max_bricks - staged)
                break;

            release(slot, slot.pending & ~filled);
            const uint64_t added = filled & ~slot.resident;
            if (static_cast<uint32_t>(std::popcount(added)) > allocator_.free_count())
            {
                if (!warned_full_)
                {
                    fprintf(stderr, "Warning: the voxel object atlas is full (%u bricks): objects that do not fit are not drawn\n",
                            allocator_.capacity());
                    warned_full_ = true;
                }
                /* Not drawn: its bricks are better used by objects that fit. Retried whole. */
                release(slot, VOBJ_ALL_BRICKS);
                slot.pending = VOBJ_ALL_BRICKS;
                continue;
            }

            for (uint64_t left = filled; left != 0; left &= left - 1)
            {
                const uint32_t b = static_cast<uint32_t>(std::countr_zero(left));
                if (added & (1ull << b))
                    slot.table[b] = allocator_.allocate();
                pack_brick(obj, b, staging + static_cast<size_t>(staged) * VOBJ_BRICK_VOXELS);
                out_bricks[staged++] = slot.table[b];
            }
            slot.resident |= filled;
            slot.pending = 0;
        }
        return staged;
    }

}
