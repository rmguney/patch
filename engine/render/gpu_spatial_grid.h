#ifndef PATCH_ENGINE_RENDER_GPU_SPATIAL_GRID_H
#define PATCH_ENGINE_RENDER_GPU_SPATIAL_GRID_H

#include "engine/core/types.h"
#include "engine/core/math.h"
#include "engine/voxel/voxel_object.h"
#include <cstdint>

namespace patch
{

static constexpr float GPU_GRID_CELL_SIZE = 8.0f;
static constexpr int32_t GPU_GRID_MAX_DIMS = 16;
static constexpr int32_t GPU_GRID_MAX_CELLS = GPU_GRID_MAX_DIMS * GPU_GRID_MAX_DIMS * GPU_GRID_MAX_DIMS;
static constexpr int32_t GPU_GRID_MAX_ENTRIES = 16384;
static constexpr int32_t GPU_GRID_MAX_PER_CELL = 32;

struct alignas(4) GPUSpatialCell
{
    uint32_t cell_start;
    uint32_t cell_count;
};
static_assert(sizeof(GPUSpatialCell) == 8, "GPUSpatialCell must be 8 bytes");

struct alignas(16) GPUSpatialGridParams
{
    float cell_size;
    float inv_cell_size;
    float _pad_pre_bounds[2];
    float bounds_min[4];
    int32_t grid_dims[4];
    int32_t total_cells;
    int32_t total_entries;
    int32_t _pad1[2];
};
static_assert(sizeof(GPUSpatialGridParams) == 64, "GPUSpatialGridParams must be 64 bytes");

struct GPUSpatialGridBuffer
{
    GPUSpatialGridParams params;
    GPUSpatialCell cells[GPU_GRID_MAX_CELLS];
    uint32_t entries[GPU_GRID_MAX_ENTRIES];

    static constexpr size_t params_offset()
    {
        return 0;
    }

    static constexpr size_t cells_offset()
    {
        return sizeof(GPUSpatialGridParams);
    }

    static constexpr size_t entries_offset()
    {
        return sizeof(GPUSpatialGridParams) + sizeof(GPUSpatialCell) * GPU_GRID_MAX_CELLS;
    }

    static constexpr size_t buffer_size()
    {
        return sizeof(GPUSpatialGridBuffer);
    }
};
static_assert(sizeof(GPUSpatialGridBuffer) ==
                  sizeof(GPUSpatialGridParams) +
                      sizeof(GPUSpatialCell) * GPU_GRID_MAX_CELLS +
                      sizeof(uint32_t) * GPU_GRID_MAX_ENTRIES,
              "GPUSpatialGridBuffer layout mismatch");

inline int32_t gpu_grid_cell_hash(int32_t cx, int32_t cy, int32_t cz, int32_t dims_x, int32_t dims_y)
{
    return cx + cy * dims_x + cz * dims_x * dims_y;
}

inline void gpu_grid_cell_coords(Vec3 pos, float inv_cell_size, const float *bounds_min,
                                 int32_t *cx, int32_t *cy, int32_t *cz)
{
    *cx = static_cast<int32_t>((pos.x - bounds_min[0]) * inv_cell_size);
    *cy = static_cast<int32_t>((pos.y - bounds_min[1]) * inv_cell_size);
    *cz = static_cast<int32_t>((pos.z - bounds_min[2]) * inv_cell_size);
}

void gpu_spatial_grid_build(
    GPUSpatialGridBuffer *out,
    const VoxelObjectWorld *objects,
    int32_t object_count,
    Bounds3D world_bounds);

} // namespace patch

#endif // PATCH_ENGINE_RENDER_GPU_SPATIAL_GRID_H
