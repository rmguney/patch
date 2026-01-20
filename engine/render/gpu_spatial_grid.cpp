#include "gpu_spatial_grid.h"
#include <cstring>
#include <algorithm>

namespace patch
{

void gpu_spatial_grid_build(
    GPUSpatialGridBuffer *out,
    const VoxelObjectWorld *objects,
    int32_t object_count,
    Bounds3D world_bounds)
{
    if (!out || !objects || object_count <= 0)
    {
        memset(out, 0, sizeof(GPUSpatialGridBuffer));
        out->params.cell_size = GPU_GRID_CELL_SIZE;
        out->params.inv_cell_size = 1.0f / GPU_GRID_CELL_SIZE;
        out->params.grid_dims[0] = 1;
        out->params.grid_dims[1] = 1;
        out->params.grid_dims[2] = 1;
        out->params.total_cells = 1;
        return;
    }

    GPUSpatialGridParams &params = out->params;
    params.cell_size = GPU_GRID_CELL_SIZE;
    params.inv_cell_size = 1.0f / GPU_GRID_CELL_SIZE;
    params._pad_pre_bounds[0] = 0.0f;
    params._pad_pre_bounds[1] = 0.0f;
    params.bounds_min[0] = world_bounds.min_x;
    params.bounds_min[1] = world_bounds.min_y;
    params.bounds_min[2] = world_bounds.min_z;
    params.bounds_min[3] = 0.0f;

    float extent_x = world_bounds.max_x - world_bounds.min_x;
    float extent_y = world_bounds.max_y - world_bounds.min_y;
    float extent_z = world_bounds.max_z - world_bounds.min_z;

    params.grid_dims[0] = std::max(1, std::min(GPU_GRID_MAX_DIMS, static_cast<int32_t>(extent_x / GPU_GRID_CELL_SIZE) + 1));
    params.grid_dims[1] = std::max(1, std::min(GPU_GRID_MAX_DIMS, static_cast<int32_t>(extent_y / GPU_GRID_CELL_SIZE) + 1));
    params.grid_dims[2] = std::max(1, std::min(GPU_GRID_MAX_DIMS, static_cast<int32_t>(extent_z / GPU_GRID_CELL_SIZE) + 1));
    params.grid_dims[3] = 0;

    params.total_cells = params.grid_dims[0] * params.grid_dims[1] * params.grid_dims[2];
    if (params.total_cells > GPU_GRID_MAX_CELLS)
    {
        params.total_cells = GPU_GRID_MAX_CELLS;
    }

    memset(out->cells, 0, sizeof(GPUSpatialCell) * GPU_GRID_MAX_CELLS);

    uint32_t cell_counts[GPU_GRID_MAX_CELLS] = {};
    for (int32_t i = 0; i < object_count && i < static_cast<int32_t>(VOBJ_MAX_OBJECTS); i++)
    {
        const VoxelObject *obj = &objects->objects[i];
        if (!obj->active)
            continue;

        float radius = obj->radius;

        int32_t cx_min, cy_min, cz_min;
        Vec3 obj_min = {obj->position.x - radius, obj->position.y - radius, obj->position.z - radius};
        gpu_grid_cell_coords(obj_min, params.inv_cell_size, params.bounds_min, &cx_min, &cy_min, &cz_min);

        int32_t cx_max, cy_max, cz_max;
        Vec3 obj_max = {obj->position.x + radius, obj->position.y + radius, obj->position.z + radius};
        gpu_grid_cell_coords(obj_max, params.inv_cell_size, params.bounds_min, &cx_max, &cy_max, &cz_max);

        cx_min = std::max(0, std::min(cx_min, params.grid_dims[0] - 1));
        cy_min = std::max(0, std::min(cy_min, params.grid_dims[1] - 1));
        cz_min = std::max(0, std::min(cz_min, params.grid_dims[2] - 1));
        cx_max = std::max(0, std::min(cx_max, params.grid_dims[0] - 1));
        cy_max = std::max(0, std::min(cy_max, params.grid_dims[1] - 1));
        cz_max = std::max(0, std::min(cz_max, params.grid_dims[2] - 1));

        for (int32_t cz = cz_min; cz <= cz_max; cz++)
        {
            for (int32_t cy = cy_min; cy <= cy_max; cy++)
            {
                for (int32_t cx = cx_min; cx <= cx_max; cx++)
                {
                    int32_t cell_idx = gpu_grid_cell_hash(cx, cy, cz, params.grid_dims[0], params.grid_dims[1]);
                    if (cell_idx >= 0 && cell_idx < params.total_cells)
                    {
                        if (cell_counts[cell_idx] < GPU_GRID_MAX_PER_CELL)
                        {
                            cell_counts[cell_idx]++;
                        }
                    }
                }
            }
        }
    }

    uint32_t prefix_sum = 0;
    for (int32_t i = 0; i < params.total_cells; i++)
    {
        out->cells[i].cell_start = prefix_sum;
        out->cells[i].cell_count = 0;
        prefix_sum += cell_counts[i];
    }
    params.total_entries = static_cast<int32_t>(std::min(prefix_sum, static_cast<uint32_t>(GPU_GRID_MAX_ENTRIES)));

    for (int32_t i = 0; i < object_count && i < static_cast<int32_t>(VOBJ_MAX_OBJECTS); i++)
    {
        const VoxelObject *obj = &objects->objects[i];
        if (!obj->active)
            continue;

        float radius = obj->radius;

        int32_t cx_min, cy_min, cz_min;
        Vec3 obj_min = {obj->position.x - radius, obj->position.y - radius, obj->position.z - radius};
        gpu_grid_cell_coords(obj_min, params.inv_cell_size, params.bounds_min, &cx_min, &cy_min, &cz_min);

        int32_t cx_max, cy_max, cz_max;
        Vec3 obj_max = {obj->position.x + radius, obj->position.y + radius, obj->position.z + radius};
        gpu_grid_cell_coords(obj_max, params.inv_cell_size, params.bounds_min, &cx_max, &cy_max, &cz_max);

        cx_min = std::max(0, std::min(cx_min, params.grid_dims[0] - 1));
        cy_min = std::max(0, std::min(cy_min, params.grid_dims[1] - 1));
        cz_min = std::max(0, std::min(cz_min, params.grid_dims[2] - 1));
        cx_max = std::max(0, std::min(cx_max, params.grid_dims[0] - 1));
        cy_max = std::max(0, std::min(cy_max, params.grid_dims[1] - 1));
        cz_max = std::max(0, std::min(cz_max, params.grid_dims[2] - 1));

        for (int32_t cz = cz_min; cz <= cz_max; cz++)
        {
            for (int32_t cy = cy_min; cy <= cy_max; cy++)
            {
                for (int32_t cx = cx_min; cx <= cx_max; cx++)
                {
                    int32_t cell_idx = gpu_grid_cell_hash(cx, cy, cz, params.grid_dims[0], params.grid_dims[1]);
                    if (cell_idx >= 0 && cell_idx < params.total_cells)
                    {
                        GPUSpatialCell &cell = out->cells[cell_idx];
                        uint32_t entry_idx = cell.cell_start + cell.cell_count;
                        if (entry_idx < static_cast<uint32_t>(GPU_GRID_MAX_ENTRIES) &&
                            cell.cell_count < GPU_GRID_MAX_PER_CELL)
                        {
                            out->entries[entry_idx] = static_cast<uint32_t>(i);
                            cell.cell_count++;
                        }
                    }
                }
            }
        }
    }
}

} // namespace patch
