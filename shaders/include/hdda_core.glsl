#ifndef HDDA_CORE_GLSL
#define HDDA_CORE_GLSL

#include "hdda_types.glsl"

/* hdda_core.glsl - DDA voxel stepping and ray/box helpers */

/* Advance DDA by one voxel step */
void hdda_step(inout DDAState state) {
    bvec3 mask = lessThanEqual(state.side_dist.xyz,
                               min(state.side_dist.yzx, state.side_dist.zxy));
    state.last_mask = mask;
    state.side_dist += vec3(mask) * state.delta_dist;
    state.map_pos += ivec3(mask) * state.step_dir;
}

/*
 * Jump the DDA across an empty block of cells (block_min..block_max, inclusive) in one go.
 * Every axis advances by the boundaries the ray crosses before it leaves the block, so
 * map_pos / side_dist end exactly where single hdda steps would: in the first cell past the
 * block, still on the ray. exit_mask is the axis (axes) the ray left through; returns the
 * number of cells crossed (for step budgets).
 */
int hdda_skip_block(inout ivec3 map_pos, inout vec3 side_dist, ivec3 step_dir, vec3 delta_dist,
                    ivec3 block_min, ivec3 block_max, out bvec3 exit_mask) {
    ivec3 cells_to_exit = ivec3(
        step_dir.x > 0 ? block_max.x - map_pos.x + 1 : map_pos.x - block_min.x + 1,
        step_dir.y > 0 ? block_max.y - map_pos.y + 1 : map_pos.y - block_min.y + 1,
        step_dir.z > 0 ? block_max.z - map_pos.z + 1 : map_pos.z - block_min.z + 1);

    /* Ray distance at which each axis crosses the block boundary; the selects keep an axis
       the ray runs parallel to (infinite side_dist / delta_dist) free of 0 * inf NaNs */
    vec3 t_axis_exit = mix(side_dist + vec3(cells_to_exit - 1) * delta_dist, side_dist,
                           equal(cells_to_exit, ivec3(1)));
    float t_exit = min(min(t_axis_exit.x, t_axis_exit.y), t_axis_exit.z);
    exit_mask = lessThanEqual(t_axis_exit, vec3(t_exit));

    /* Boundaries crossed up to t_exit: all of them on the exit axes, the rest stay inside */
    vec3 crossed = floor((t_exit - side_dist) / delta_dist) + 1.0;
    crossed = mix(vec3(0.0), min(crossed, vec3(cells_to_exit - 1)), lessThanEqual(side_dist, vec3(t_exit)));
    crossed = mix(crossed, vec3(cells_to_exit), exit_mask);

    ivec3 cells = ivec3(crossed);
    map_pos += cells * step_dir;
    side_dist = mix(side_dist, side_dist + crossed * delta_dist, greaterThan(crossed, vec3(0.0)));
    return cells.x + cells.y + cells.z;
}

/* Check if position is within grid bounds */
bool hdda_in_bounds(ivec3 pos, ivec3 grid_size) {
    return pos.x >= 0 && pos.x < grid_size.x &&
           pos.y >= 0 && pos.y < grid_size.y &&
           pos.z >= 0 && pos.z < grid_size.z;
}

/* Compute normal from DDA step mask and ray direction */
vec3 hdda_compute_normal(bvec3 mask, vec3 ray_dir) {
    vec3 normal = vec3(0.0);
    if (mask.x) {
        normal.x = -sign(ray_dir.x);
    } else if (mask.y) {
        normal.y = -sign(ray_dir.y);
    } else {
        normal.z = -sign(ray_dir.z);
    }
    return normal;
}

/* Ray-AABB intersection: (entry t, exit t); a miss has entry > exit or exit < 0 */
vec2 hdda_intersect_aabb(vec3 ro, vec3 rd, vec3 box_min, vec3 box_max) {
    vec3 inv_rd = 1.0 / rd;
    vec3 t0 = (box_min - ro) * inv_rd;
    vec3 t1 = (box_max - ro) * inv_rd;
    vec3 tmin = min(t0, t1);
    vec3 tmax = max(t0, t1);
    float enter = max(max(tmin.x, tmin.y), tmin.z);
    float exit = min(min(tmax.x, tmax.y), tmax.z);
    return vec2(enter, exit);
}

#endif /* HDDA_CORE_GLSL */
