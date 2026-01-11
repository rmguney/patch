/*
 * hdda_core.glsl - Core hierarchical DDA raymarching algorithm
 *
 * This module provides the core DDA stepping logic. Users must define:
 *   - bool sample_occupancy_chunk(int chunk_idx)
 *   - bool sample_occupancy_region(int chunk_idx, ivec3 region)
 *   - uint sample_material(ivec3 global_pos)
 *   - ivec3 get_grid_size()
 *   - ivec3 get_chunks_dim()
 *   - int get_total_chunks()
 */

#ifndef HDDA_CORE_GLSL
#define HDDA_CORE_GLSL

#include "hdda_types.glsl"

/* Initialize DDA state for a ray starting at grid_pos */
DDAState hdda_init(vec3 grid_pos, vec3 ray_dir) {
    DDAState state;

    state.map_pos = ivec3(floor(grid_pos));
    state.delta_dist = abs(1.0 / ray_dir);
    state.step_dir = ivec3(sign(ray_dir));

    /* Standard DDA side_dist initialization */
    state.side_dist = (sign(ray_dir) * (vec3(state.map_pos) - grid_pos) +
                       sign(ray_dir) * 0.5 + 0.5) * state.delta_dist;

    state.last_mask = bvec3(false, true, false);

    return state;
}

/* Advance DDA by one voxel step */
void hdda_step(inout DDAState state) {
    bvec3 mask = lessThanEqual(state.side_dist.xyz,
                               min(state.side_dist.yzx, state.side_dist.zxy));
    state.last_mask = mask;
    state.side_dist += vec3(mask) * state.delta_dist;
    state.map_pos += ivec3(mask) * state.step_dir;
}

/* Check if position is within grid bounds */
bool hdda_in_bounds(ivec3 pos, ivec3 grid_size) {
    return pos.x >= 0 && pos.x < grid_size.x &&
           pos.y >= 0 && pos.y < grid_size.y &&
           pos.z >= 0 && pos.z < grid_size.z;
}

/* Calculate chunk index from voxel position */
int hdda_chunk_index(ivec3 pos, ivec3 chunks_dim) {
    ivec3 chunk_pos = pos / CHUNK_SIZE;
    return chunk_pos.x + chunk_pos.y * chunks_dim.x +
           chunk_pos.z * chunks_dim.x * chunks_dim.y;
}

/* Calculate region within chunk from local position */
ivec3 hdda_region_coord(ivec3 local_pos) {
    return local_pos / REGION_SIZE;
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

/* Ray-AABB intersection */
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

/* Compute entry face mask from AABB intersection */
bvec3 hdda_entry_face_mask(vec3 ro, vec3 rd, vec3 box_min, vec3 box_max) {
    vec3 inv_rd = 1.0 / rd;
    vec3 t0 = (box_min - ro) * inv_rd;
    vec3 t1 = (box_max - ro) * inv_rd;
    vec3 tmin = min(t0, t1);
    float max_tmin = max(max(tmin.x, tmin.y), tmin.z);
    return bvec3(
        abs(tmin.x - max_tmin) < 0.0001,
        abs(tmin.y - max_tmin) < 0.0001,
        abs(tmin.z - max_tmin) < 0.0001
    );
}

/*
 * Main HDDA march function with 2-level hierarchy.
 *
 * Requires these functions to be defined by the including shader:
 *   bool sample_occupancy_chunk(int chunk_idx);
 *   bool sample_occupancy_region(int chunk_idx, ivec3 region);
 *   uint sample_material(ivec3 global_pos);
 *
 * Parameters are passed to avoid global state dependencies.
 */
HitInfo hdda_march_terrain(
    vec3 ray_origin,
    vec3 ray_dir,
    vec3 bounds_min,
    vec3 bounds_max,
    float voxel_size,
    ivec3 grid_size,
    ivec3 chunks_dim,
    int total_chunks,
    int max_steps
) {
    HitInfo info;
    info.hit = false;
    info.t = 1e10;

    /* AABB intersection */
    vec2 box_hit = hdda_intersect_aabb(ray_origin, ray_dir, bounds_min, bounds_max);
    if (box_hit.x > box_hit.y || box_hit.y < 0.0) {
        return info;
    }

    /* Convert to grid space */
    float t_start = max(box_hit.x, 0.001);
    vec3 start_world = ray_origin + ray_dir * t_start;
    vec3 grid_pos = (start_world - bounds_min) / voxel_size;
    grid_pos = clamp(grid_pos, vec3(0.0), vec3(grid_size) - 0.001);

    /* Initialize DDA */
    DDAState dda = hdda_init(grid_pos, ray_dir);

    /* Compute initial entry face for correct first normal */
    if (box_hit.x > 0.0) {
        dda.last_mask = hdda_entry_face_mask(ray_origin, ray_dir, bounds_min, bounds_max);
    } else {
        vec3 frac_pos = fract(grid_pos);
        vec3 dist_to_face = mix(frac_pos, 1.0 - frac_pos, greaterThan(ray_dir, vec3(0.0)));
        float min_dist = min(min(dist_to_face.x, dist_to_face.y), dist_to_face.z);
        dda.last_mask = lessThanEqual(dist_to_face, vec3(min_dist + 0.0001));
    }

    /* Cached hierarchy state */
    ivec3 last_chunk = ivec3(-1000);
    ivec3 last_region = ivec3(-1000);
    bool chunk_empty = false;
    bool region_empty = false;
    int current_chunk_idx = -1;

    /* Main DDA loop */
    for (int i = 0; i < max_steps; i++) {
        if (!hdda_in_bounds(dda.map_pos, grid_size)) {
            break;
        }

        /* Level 2: Chunk occupancy check */
        ivec3 current_chunk = dda.map_pos / CHUNK_SIZE;
        if (current_chunk != last_chunk) {
            last_chunk = current_chunk;
            current_chunk_idx = hdda_chunk_index(dda.map_pos, chunks_dim);
            chunk_empty = (current_chunk_idx < 0 || current_chunk_idx >= total_chunks) ||
                          !sample_occupancy_chunk(current_chunk_idx);
            last_region = ivec3(-1000);
        }

        if (chunk_empty) {
            hdda_step(dda);
            continue;
        }

        /* Level 1: Region occupancy check (8x8x8 regions) */
        ivec3 local_pos = dda.map_pos - current_chunk * CHUNK_SIZE;
        ivec3 current_region = hdda_region_coord(local_pos);
        if (current_region != last_region) {
            last_region = current_region;
            region_empty = !sample_occupancy_region(current_chunk_idx, current_region);
        }

        if (region_empty) {
            hdda_step(dda);
            continue;
        }

        /* Level 0: Per-voxel material check */
        uint mat = sample_material(dda.map_pos);
        if (mat != 0u) {
            info.hit = true;
            info.material_id = mat;
            info.voxel_coord = dda.map_pos;

            /* Compute precise hit point via voxel AABB intersection */
            vec3 voxel_min = bounds_min + vec3(dda.map_pos) * voxel_size;
            vec3 voxel_max = voxel_min + voxel_size;
            vec2 voxel_hit = hdda_intersect_aabb(ray_origin, ray_dir, voxel_min, voxel_max);
            info.t = voxel_hit.x;
            info.pos = ray_origin + ray_dir * info.t;
            info.normal = hdda_compute_normal(dda.last_mask, ray_dir);

            return info;
        }

        hdda_step(dda);
    }

    return info;
}

/*
 * Simple DDA march without hierarchy (for voxel objects, etc.)
 * Uses single grid with no chunk/region structure.
 */
HitInfo hdda_march_simple(
    vec3 grid_origin,
    vec3 grid_dir,
    ivec3 grid_size,
    int max_steps
) {
    HitInfo info;
    info.hit = false;
    info.t = 1e10;

    /* AABB intersection in grid space */
    vec2 box_hit = hdda_intersect_aabb(grid_origin, grid_dir, vec3(0.0), vec3(grid_size));
    if (box_hit.x > box_hit.y || box_hit.y < 0.0) {
        return info;
    }

    float t_start = max(box_hit.x, 0.001);
    vec3 start_pos = grid_origin + grid_dir * t_start;

    /* Initialize DDA */
    DDAState dda = hdda_init(start_pos, grid_dir);
    dda.map_pos = clamp(dda.map_pos, ivec3(0), grid_size - 1);

    /* Avoid division by zero */
    vec3 safe_dir = abs(grid_dir) + vec3(0.0001);
    dda.delta_dist = 1.0 / safe_dir;

    /* Main DDA loop */
    for (int i = 0; i < max_steps; i++) {
        if (!hdda_in_bounds(dda.map_pos, grid_size)) {
            break;
        }

        uint mat = sample_material_simple(dda.map_pos);
        if (mat != 0u) {
            info.hit = true;
            info.material_id = mat;
            info.voxel_coord = dda.map_pos;
            info.normal = hdda_compute_normal(dda.last_mask, grid_dir);
            return info;
        }

        hdda_step(dda);
    }

    return info;
}

#endif /* HDDA_CORE_GLSL */
