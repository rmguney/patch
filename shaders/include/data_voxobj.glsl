#ifndef DATA_VOXOBJ_GLSL
#define DATA_VOXOBJ_GLSL

#include "hdda_types.glsl"
#include "hdda_core.glsl"

/*
 * data_voxobj.glsl - Voxel object data access functions
 *
 * REQUIREMENTS: The including shader must define (before including this file):
 * 1. VoxelObjectGPU struct:
 *    struct VoxelObjectGPU {
 *        mat4 world_to_local;
 *        mat4 local_to_world;
 *        vec4 bounds_min;    // xyz: min, w: voxel_size
 *        vec4 bounds_max;    // xyz: max, w: grid_size
 *        vec4 position;      // xyz: world pos, w: active flag
 *        uint atlas_slice;
 *        uint material_base;
 *        uint flags;
 *        uint occupancy_mask; // 8-bit region occupancy (2×2×2 regions of 8³)
 *    };
 *
 * 2. Buffer bindings:
 *    uniform usampler3D vobj_atlas;
 *    buffer { VoxelObjectGPU objects[]; };
 *
 * 3. Include materials.glsl before this file (for get_material_* functions)
 */

uint sample_vobj_material(int object_idx, ivec3 local_pos, int grid_size) {
    VoxelObjectGPU obj = objects[object_idx];
    uint atlas_z_base = obj.atlas_slice * uint(grid_size);
    ivec3 texel = ivec3(local_pos.x, local_pos.y, int(atlas_z_base) + local_pos.z);
    return texelFetch(vobj_atlas, texel, 0).r;
}

void vobj_transform_ray(
    int object_idx,
    vec3 world_origin,
    vec3 world_dir,
    out vec3 local_origin,
    out vec3 local_dir
) {
    VoxelObjectGPU obj = objects[object_idx];
    local_origin = (obj.world_to_local * vec4(world_origin, 1.0)).xyz;
    local_dir = normalize((obj.world_to_local * vec4(world_dir, 0.0)).xyz);
}

void vobj_transform_hit(
    int object_idx,
    vec3 local_pos,
    vec3 local_normal,
    out vec3 world_pos,
    out vec3 world_normal
) {
    VoxelObjectGPU obj = objects[object_idx];
    world_pos = (obj.local_to_world * vec4(local_pos, 1.0)).xyz;
    world_normal = normalize((obj.local_to_world * vec4(local_normal, 0.0)).xyz);
}

float vobj_get_voxel_size(int object_idx) {
    return objects[object_idx].bounds_min.w;
}

float vobj_get_grid_size(int object_idx) {
    return objects[object_idx].bounds_max.w;
}

float vobj_get_half_extent(int object_idx) {
    return objects[object_idx].bounds_max.x;
}

bool vobj_is_active(int object_idx) {
    return objects[object_idx].position.w > 0.0;
}

vec3 vobj_get_world_position(int object_idx) {
    return objects[object_idx].position.xyz;
}

/*
 * Calculate squared distance from ray to sphere center.
 * Used for early rejection of objects that a ray cannot possibly hit.
 * Returns squared distance to avoid sqrt in the common case.
 */
float vobj_ray_sphere_dist_sq(vec3 ray_origin, vec3 ray_dir, vec3 sphere_center) {
    vec3 oc = ray_origin - sphere_center;
    float b = dot(oc, ray_dir);
    float c = dot(oc, oc);
    return c - b * b;
}

/*
 * Calculate bounding sphere radius for a voxel object.
 * Uses sqrt(3) * half_extent for the diagonal of the bounding cube.
 */
float vobj_get_bounding_radius(int object_idx) {
    float half_ext = vobj_get_half_extent(object_idx);
    return half_ext * 1.732051;  // sqrt(3)
}

/*
 * Quick ray-sphere rejection test.
 * Returns true if the ray could possibly hit the object's bounding sphere.
 */
bool vobj_ray_could_hit(vec3 ray_origin, vec3 ray_dir, int object_idx) {
    vec3 center = vobj_get_world_position(object_idx);
    float radius = vobj_get_bounding_radius(object_idx);
    float dist_sq = vobj_ray_sphere_dist_sq(ray_origin, ray_dir, center);
    return dist_sq <= radius * radius;
}

/*
 * Calculate LOD-adjusted max steps based on distance and rt_quality.
 * 
 * Distance thresholds (scaled by rt_quality 1-3):
 *   Near:  < 40/60/80 units  -> full steps (48)
 *   Mid:   < 80/120/160 units -> medium (32)
 *   Far:   >= threshold      -> coarse (16)
 */
int vobj_calc_distance_lod_steps(float distance, int rt_quality, int base_steps) {
    float quality_scale = float(max(rt_quality, 1));
    float near_thresh = 40.0 * quality_scale;
    float far_thresh = 80.0 * quality_scale;
    
    if (distance < near_thresh) {
        return base_steps;           // Full detail
    } else if (distance < far_thresh) {
        return (base_steps * 2) / 3; // ~32 steps for base 48
    } else {
        return base_steps / 3;       // ~16 steps for base 48
    }
}

/*
 * Calculate LOD-adjusted max steps based on screen coverage.
 * Large objects covering more screen need fewer steps (voxels span multiple pixels).
 *
 * Aggressive coverage thresholds (optimized for close-up performance):
 *   > 50% screen -> ultra coarse (8 steps)
 *   > 30% screen -> very coarse (12 steps)
 *   > 15% screen -> coarse (16 steps)
 *   > 5% screen  -> medium (24 steps)
 *   <= 5%        -> high detail (32 steps, still reduced from base 48)
 */
int vobj_calc_coverage_lod_steps(float coverage, int base_steps) {
    if (coverage > 0.5) {
        return base_steps / 6;       // ~8 steps
    } else if (coverage > 0.3) {
        return base_steps / 4;       // ~12 steps
    } else if (coverage > 0.15) {
        return base_steps / 3;       // ~16 steps
    } else if (coverage > 0.05) {
        return base_steps / 2;       // ~24 steps
    }
    return (base_steps * 2) / 3;     // ~32 steps (high detail but not full)
}

/*
 * Combined LOD: take minimum of distance and coverage LOD.
 * This ensures large close objects get reduced steps.
 */
int vobj_calc_combined_lod_steps(float distance, float coverage, int rt_quality, int base_steps) {
    int dist_steps = vobj_calc_distance_lod_steps(distance, rt_quality, base_steps);
    int cov_steps = vobj_calc_coverage_lod_steps(coverage, base_steps);
    return min(dist_steps, cov_steps);
}

HitInfo vobj_march_object(
    int object_idx,
    vec3 world_origin,
    vec3 world_dir,
    int max_steps
) {
    HitInfo info;
    info.hit = false;
    info.t = 1e10;

    VoxelObjectGPU obj = objects[object_idx];

    /* Skip inactive objects */
    if (obj.position.w <= 0.0) {
        return info;
    }

    vec3 local_origin = (obj.world_to_local * vec4(world_origin, 1.0)).xyz;
    vec3 local_dir = normalize((obj.world_to_local * vec4(world_dir, 0.0)).xyz);

    float voxel_size = obj.bounds_min.w;
    float grid_size = obj.bounds_max.w;
    float half_extent = obj.bounds_max.x;

    vec3 local_min = vec3(-half_extent);
    vec3 local_max = vec3(half_extent);

    vec2 box_t = hdda_intersect_aabb(local_origin, local_dir, local_min, local_max);
    if (box_t.x > box_t.y || box_t.y < 0.0) {
        return info;
    }

    float t_start = max(box_t.x, 0.001);
    vec3 start_local = local_origin + local_dir * t_start;
    vec3 grid_pos = (start_local + vec3(half_extent)) / voxel_size;
    grid_pos = clamp(grid_pos, vec3(0.0), vec3(grid_size) - 0.001);

    /* Initialize DDA state (manual init due to world-space ray direction) */
    DDAState dda;
    dda.map_pos = ivec3(floor(grid_pos));
    dda.delta_dist = abs(vec3(voxel_size) / local_dir);
    dda.step_dir = ivec3(sign(local_dir));
    dda.side_dist = (sign(local_dir) * (vec3(dda.map_pos) - grid_pos) +
                     sign(local_dir) * 0.5 + 0.5) * dda.delta_dist;
    dda.last_mask = bvec3(false, true, false);

    uint atlas_z_base = obj.atlas_slice * uint(grid_size);
    uint occupancy = obj.occupancy_mask;
    ivec3 grid_size_i = ivec3(int(grid_size));
    ivec3 last_region = ivec3(-1);

    for (int i = 0; i < max_steps; i++) {
        if (!hdda_in_bounds(dda.map_pos, grid_size_i)) {
            break;
        }

        /* Region occupancy check (2×2×2 regions of 8³ voxels) */
        ivec3 region = dda.map_pos / 8;
        if (region != last_region) {
            last_region = region;
            int region_idx = region.x + region.y * 2 + region.z * 4;
            if ((occupancy & (1u << region_idx)) == 0u) {
                /* Skip to region boundary instead of stepping 1 voxel */
                ivec3 region_min = region * 8;
                ivec3 region_max = region_min + 7;
                /* Calculate steps to exit region on each axis */
                ivec3 exit_pos = mix(region_min - 1, region_max + 1, greaterThan(dda.step_dir, ivec3(0)));
                vec3 steps_to_exit = vec3(exit_pos - dda.map_pos) / vec3(dda.step_dir);
                steps_to_exit = max(steps_to_exit, vec3(1.0));
                int skip = int(min(min(steps_to_exit.x, steps_to_exit.y), steps_to_exit.z));
                dda.map_pos += dda.step_dir * skip;
                i += skip - 1; /* Account for loop increment */
                continue;
            }
        }

        ivec3 texel = ivec3(dda.map_pos.x, dda.map_pos.y, int(atlas_z_base) + dda.map_pos.z);
        uint mat = texelFetch(vobj_atlas, texel, 0).r;

        if (mat != 0u) {
            vec3 voxel_min_local = (vec3(dda.map_pos) - vec3(grid_size * 0.5)) * voxel_size;
            vec3 voxel_max_local = voxel_min_local + vec3(voxel_size);

            vec2 voxel_t = hdda_intersect_aabb(local_origin, local_dir, voxel_min_local, voxel_max_local);
            float hit_t_local = max(voxel_t.x, 0.0);

            vec3 local_normal = hdda_compute_normal(dda.last_mask, local_dir);
            vec3 hit_world = world_origin + world_dir * hit_t_local;
            vec3 world_normal = normalize((obj.local_to_world * vec4(local_normal, 0.0)).xyz);

            info.hit = true;
            info.material_id = mat;
            info.pos = hit_world;
            info.normal = world_normal;
            info.t = hit_t_local;
            info.voxel_coord = dda.map_pos;
            info.step_mask = dda.last_mask;
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
