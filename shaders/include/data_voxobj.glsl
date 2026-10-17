#ifndef DATA_VOXOBJ_GLSL
#define DATA_VOXOBJ_GLSL

#include "hdda_types.glsl"
#include "hdda_core.glsl"

/*
 * data_voxobj.glsl - voxel object raymarching
 *
 * The including shader must first include voxobj_types.glsl and materials.glsl and declare:
 *   uniform usampler3D vobj_atlas;
 *   buffer { VoxelObjectGPU objects[]; };
 */

bool vobj_is_active(int object_idx) {
    return objects[object_idx].position.w > 0.0;
}

bool vobj_brick_in_atlas(uvec2 brick_mask, int brick) {
    uint bits = brick < 32 ? brick_mask.x : brick_mask.y;
    return (bits & (1u << uint(brick & 31))) != 0u;
}

/* Grid cell where the object's bricks start (VoxelObject::brick_offset) */
ivec3 vobj_brick_offset(int object_idx) {
    uint packed = objects[object_idx].brick_offset;
    return ivec3(uvec3(packed, packed >> 8u, packed >> 16u) & 0xFFu);
}

/* Atlas texel of the first voxel of the object's brick (one that is in the atlas) */
ivec3 vobj_brick_origin(int object_idx, int brick) {
    uint pair = objects[object_idx].brick_table[brick >> 1];
    uint atlas_brick = (pair >> (uint(brick & 1) * 16u)) & 0xFFFFu;
    uvec3 coord = uvec3(atlas_brick, atlas_brick / uint(BRICK_ATLAS_BRICKS_PER_AXIS),
                        atlas_brick / uint(BRICK_ATLAS_BRICKS_PER_AXIS * BRICK_ATLAS_BRICKS_PER_AXIS));
    return ivec3(coord % uint(BRICK_ATLAS_BRICKS_PER_AXIS)) * VOBJ_BRICK_SIZE;
}

/*
 * March the ray through one object's voxel grid; returns the first solid voxel hit with
 * hit.t < max_t. Hierarchical: a brick missing from the brick mask is crossed in one step,
 * inside a brick in the atlas the DDA walks voxel by voxel. No step cap below the grid
 * bound: a DDA ray visits at most 3 * grid_size cells of an N^3 grid, and the walk ends
 * when it leaves the grid, so a lower cap would only drop hits (holes in thin or hollow
 * objects).
 */
HitInfo vobj_march_object(
    int object_idx,
    vec3 world_origin,
    vec3 world_dir,
    float max_t
) {
    HitInfo info;
    info.hit = false;
    info.t = 1e10;
    info.water_depth = 0.0;

    /* Skip inactive objects */
    if (objects[object_idx].position.w <= 0.0) {
        return info;
    }

    /* Members one by one: a copy of the whole struct would take its brick table along */
    mat4 world_to_local = objects[object_idx].world_to_local;
    vec4 bounds_min = objects[object_idx].bounds_min;
    vec4 bounds_max = objects[object_idx].bounds_max;

    vec3 local_origin = (world_to_local * vec4(world_origin, 1.0)).xyz;
    vec3 local_dir = normalize((world_to_local * vec4(world_dir, 0.0)).xyz);

    float voxel_size = bounds_min.w;
    float grid_size = bounds_max.w;
    float half_grid = grid_size * 0.5;

    vec3 local_min = bounds_min.xyz;
    vec3 local_max = bounds_max.xyz;

    vec2 box_t = hdda_intersect_aabb(local_origin, local_dir, local_min, local_max);
    if (box_t.x > box_t.y || box_t.y < 0.0) {
        return info;
    }

    /* AABB entry is behind current best hit — entire object is occluded */
    if (box_t.x > max_t) {
        return info;
    }

    /* The walk runs in brick cells: grid cells counted from the brick offset, an exact shift
     * by whole cells. No voxel lies before the offset, so the start clamps to it. */
    ivec3 brick_offset = vobj_brick_offset(object_idx);
    float t_start = max(box_t.x, 0.001);
    vec3 start_local = local_origin + local_dir * t_start;
    vec3 grid_pos = start_local / voxel_size + vec3(half_grid) - vec3(brick_offset);
    grid_pos = clamp(grid_pos, vec3(0.0), vec3(grid_size) - 0.001);

    /* Initialize DDA state (manual init due to world-space ray direction) */
    DDAState dda;
    dda.map_pos = ivec3(floor(grid_pos));
    dda.delta_dist = abs(vec3(voxel_size) / local_dir);
    dda.step_dir = ivec3(sign(local_dir));
    dda.side_dist = (sign(local_dir) * (vec3(dda.map_pos) - grid_pos) +
                     sign(local_dir) * 0.5 + 0.5) * dda.delta_dist;

    /* Compute initial entry face (same as terrain) */
    if (box_t.x > 0.0) {
        vec3 inv_ld = 1.0 / local_dir;
        vec3 t0 = (local_min - local_origin) * inv_ld;
        vec3 t1 = (local_max - local_origin) * inv_ld;
        vec3 tmin = min(t0, t1);
        float max_tmin = max(max(tmin.x, tmin.y), tmin.z);
        dda.last_mask = bvec3(
            abs(tmin.x - max_tmin) < 0.0001,
            abs(tmin.y - max_tmin) < 0.0001,
            abs(tmin.z - max_tmin) < 0.0001
        );
    } else {
        vec3 frac_pos = fract(grid_pos);
        vec3 dist_to_face = mix(frac_pos, 1.0 - frac_pos, greaterThan(local_dir, vec3(0.0)));
        float min_dist = min(min(dist_to_face.x, dist_to_face.y), dist_to_face.z);
        dda.last_mask = lessThanEqual(dist_to_face, vec3(min_dist + 0.0001));
    }

    uvec2 brick_mask = objects[object_idx].brick_mask;
    ivec3 grid_size_i = ivec3(int(grid_size));
    int last_brick = -1;
    ivec3 brick_origin = ivec3(0);
    int max_steps = 3 * grid_size_i.x;

    for (int i = 0; i < max_steps; i++) {
        if (!hdda_in_bounds(dda.map_pos, grid_size_i)) {
            break;
        }

        /* Brick level: in bounds, so the cell is non-negative and unsigned division is exact */
        uvec3 cell = uvec3(dda.map_pos);
        ivec3 brick_pos = ivec3(cell / uint(VOBJ_BRICK_SIZE));
        int brick = brick_pos.x + (brick_pos.y + brick_pos.z * VOBJ_BRICKS_PER_AXIS) * VOBJ_BRICKS_PER_AXIS;
        if (brick != last_brick) {
            last_brick = brick;
            if (!vobj_brick_in_atlas(brick_mask, brick)) {
                /* Empty brick: jump past it, staying on the ray */
                ivec3 brick_min = brick_pos * VOBJ_BRICK_SIZE;
                int skip = hdda_skip_block(dda.map_pos, dda.side_dist, dda.step_dir, dda.delta_dist,
                                           brick_min, brick_min + (VOBJ_BRICK_SIZE - 1), dda.last_mask);
                i += max(skip - 1, 0);
                continue;
            }
            brick_origin = vobj_brick_origin(object_idx, brick);
        }

        /* Voxel level: the voxel's place in its brick's atlas copy */
        ivec3 texel = brick_origin + ivec3(cell % uint(VOBJ_BRICK_SIZE));
        uint mat = texelFetch(vobj_atlas, texel, 0).r;

        if (mat != 0u) {
            vec3 voxel_min_local = (vec3(dda.map_pos + brick_offset) - vec3(grid_size * 0.5)) * voxel_size;
            vec3 voxel_max_local = voxel_min_local + vec3(voxel_size);

            vec2 voxel_t = hdda_intersect_aabb(local_origin, local_dir, voxel_min_local, voxel_max_local);
            float hit_t_local = max(voxel_t.x, 0.0);

            /* Local transform is pure rotation+translation (no scale in world_to_local),
               so hit_t_local equals world t-value. Compute world pos directly on world ray. */
            vec3 hit_world = world_origin + world_dir * hit_t_local;

            /* Transform normal from local to world space */
            vec3 local_normal = hdda_compute_normal(dda.last_mask, local_dir);
            vec3 world_normal = normalize((objects[object_idx].local_to_world * vec4(local_normal, 0.0)).xyz);

            info.hit = true;
            info.pos = hit_world;
            info.normal = world_normal;
            info.t = hit_t_local;
            info.color = get_material_color(mat);
            info.emissive = get_material_emissive(mat);
            info.roughness = get_material_roughness(mat);
            info.metallic = get_material_metallic(mat);

            return info;
        }

        hdda_step(dda);
    }

    return info;
}

#endif /* DATA_VOXOBJ_GLSL */
