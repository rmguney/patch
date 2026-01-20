#ifndef SPATIAL_GRID_GLSL
#define SPATIAL_GRID_GLSL

/*
 * spatial_grid.glsl - GPU spatial hash grid for accelerated object lookups
 *
 * REQUIREMENTS: The including shader must define (before including this file):
 * 1. The SET_BINDING macro for descriptor set/binding
 * 2. SPATIAL_GRID_SET and SPATIAL_GRID_BINDING for the buffer location
 *
 * Example:
 *   #define SPATIAL_GRID_SET 1
 *   #define SPATIAL_GRID_BINDING 2
 *   #include "spatial_grid.glsl"
 */

#ifndef SPATIAL_GRID_SET
#define SPATIAL_GRID_SET 1
#endif

#ifndef SPATIAL_GRID_BINDING
#define SPATIAL_GRID_BINDING 2
#endif

#define GRID_MAX_CELLS 4096
#define GRID_MAX_ENTRIES 16384
#define GRID_MAX_PER_CELL 32
#define GRID_MAX_CANDIDATES 24

struct SpatialGridParams {
    float cell_size;
    float inv_cell_size;
    vec2 _pad_pre_bounds;
    vec4 bounds_min;
    ivec4 grid_dims;
    int total_cells;
    int total_entries;
    ivec2 _pad1;
};

struct SpatialCell {
    uint cell_start;
    uint cell_count;
};

layout(std430, SET_BINDING(SPATIAL_GRID_SET, SPATIAL_GRID_BINDING)) readonly buffer SpatialGridBuffer {
    SpatialGridParams grid_params;
    SpatialCell grid_cells[GRID_MAX_CELLS];
    uint grid_entries[GRID_MAX_ENTRIES];
};

ivec3 grid_world_to_cell(vec3 world_pos) {
    vec3 local = (world_pos - grid_params.bounds_min.xyz) * grid_params.inv_cell_size;
    return ivec3(floor(local));
}

int grid_cell_hash(ivec3 cell) {
    return cell.x + cell.y * grid_params.grid_dims.x +
           cell.z * grid_params.grid_dims.x * grid_params.grid_dims.y;
}

bool grid_cell_valid(ivec3 cell) {
    return cell.x >= 0 && cell.x < grid_params.grid_dims.x &&
           cell.y >= 0 && cell.y < grid_params.grid_dims.y &&
           cell.z >= 0 && cell.z < grid_params.grid_dims.z;}

ivec3 grid_get_dims() {
    return grid_params.grid_dims.xyz;
}

/*
 * Collect candidate object indices along a ray using 3D DDA traversal.
 * Returns unique candidates (no duplicates) up to GRID_MAX_CANDIDATES.
 *
 * Parameters:
 *   ray_origin - World-space ray origin
 *   ray_dir    - Normalized world-space ray direction
 *   max_t      - Maximum ray distance to search
 *   candidates - Output array of object indices
 *   count      - Output count of candidates found
 */
void grid_collect_candidates(
    vec3 ray_origin, vec3 ray_dir, float max_t,
    out int candidates[GRID_MAX_CANDIDATES], out int count
) {
    count = 0;

    if (grid_params.total_cells <= 0 || grid_params.total_entries <= 0) {
        return;
    }

    ivec3 start_cell = grid_world_to_cell(ray_origin);
    ivec3 end_cell = grid_world_to_cell(ray_origin + ray_dir * max_t);

    ivec3 step_dir = ivec3(sign(ray_dir));
    vec3 delta_t = abs(vec3(grid_params.cell_size) / ray_dir);

    start_cell = clamp(start_cell, ivec3(0), grid_params.grid_dims.xyz - 1);

    vec3 cell_min = grid_params.bounds_min.xyz + vec3(start_cell) * grid_params.cell_size;
    vec3 cell_max = cell_min + grid_params.cell_size;

    vec3 t_next;
    t_next.x = (step_dir.x > 0) ? (cell_max.x - ray_origin.x) / ray_dir.x
                                 : (cell_min.x - ray_origin.x) / ray_dir.x;
    t_next.y = (step_dir.y > 0) ? (cell_max.y - ray_origin.y) / ray_dir.y
                                 : (cell_min.y - ray_origin.y) / ray_dir.y;
    t_next.z = (step_dir.z > 0) ? (cell_max.z - ray_origin.z) / ray_dir.z
                                 : (cell_min.z - ray_origin.z) / ray_dir.z;

    if (abs(ray_dir.x) < 0.0001) t_next.x = 1e10;
    if (abs(ray_dir.y) < 0.0001) t_next.y = 1e10;
    if (abs(ray_dir.z) < 0.0001) t_next.z = 1e10;

    ivec3 current_cell = start_cell;
    uint visited_mask[8] = uint[8](0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u);

    const int MAX_GRID_STEPS = 32;

    for (int step = 0; step < MAX_GRID_STEPS && count < GRID_MAX_CANDIDATES; step++) {
        if (!grid_cell_valid(current_cell)) {
            break;
        }

        int cell_idx = grid_cell_hash(current_cell);
        if (cell_idx >= 0 && cell_idx < grid_params.total_cells) {
            SpatialCell cell = grid_cells[cell_idx];

            for (uint i = 0; i < cell.cell_count && count < GRID_MAX_CANDIDATES; i++) {
                uint entry_idx = cell.cell_start + i;
                if (entry_idx >= uint(grid_params.total_entries)) break;

                uint obj_idx = grid_entries[entry_idx];

                uint mask_word = obj_idx >> 5;
                uint mask_bit = 1u << (obj_idx & 31u);

                if (mask_word < 8u && (visited_mask[mask_word] & mask_bit) == 0u) {
                    visited_mask[mask_word] |= mask_bit;
                    candidates[count++] = int(obj_idx);
                }
            }
        }

        float t_min = min(min(t_next.x, t_next.y), t_next.z);
        if (t_min > max_t) {
            break;
        }

        if (t_next.x <= t_next.y && t_next.x <= t_next.z) {
            current_cell.x += step_dir.x;
            t_next.x += delta_t.x;
        } else if (t_next.y <= t_next.z) {
            current_cell.y += step_dir.y;
            t_next.y += delta_t.y;
        } else {
            current_cell.z += step_dir.z;
            t_next.z += delta_t.z;
        }
    }
}

#endif /* SPATIAL_GRID_GLSL */
