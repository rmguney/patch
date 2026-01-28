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
 *        uint occupancy_mask; // 8-bit region occupancy (2×2×2 regions of grid_size/2)
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

float vobj_get_max_half_extent(int object_idx) {
    vec3 ext = objects[object_idx].bounds_max.xyz;
    return max(ext.x, max(ext.y, ext.z));
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
 * Uses sqrt(3) * max_half_extent for the diagonal of the bounding cube.
 */
float vobj_get_bounding_radius(int object_idx) {
    float half_ext = vobj_get_max_half_extent(object_idx);
    return half_ext * 1.732051;  // sqrt(3)
}

/*
 * Calculate minimum possible t-value for ray-sphere intersection.
 * Used for depth rejection before expensive marching.
 * Returns large value (1e10) if ray cannot hit the sphere.
 */
float vobj_get_min_t(vec3 ray_origin, vec3 ray_dir, int object_idx) {
    vec3 center = vobj_get_world_position(object_idx);
    float radius = vobj_get_bounding_radius(object_idx);
    vec3 oc = ray_origin - center;
    float b = dot(oc, ray_dir);
    float c = dot(oc, oc) - radius * radius;
    float disc = b * b - c;
    if (disc < 0.0) return 1e10;
    return max(0.0, -b - sqrt(disc));
}

/*
 * Quick ray-sphere rejection test.
 * Returns true if the ray could possibly hit the object's bounding sphere.
 */
bool vobj_ray_could_hit(vec3 ray_origin, vec3 ray_dir, int object_idx) {
    if (!vobj_is_active(object_idx)) {
        return false;
    }
    vec3 center = vobj_get_world_position(object_idx);
    float radius = vobj_get_bounding_radius(object_idx);
    float dist_sq = vobj_ray_sphere_dist_sq(ray_origin, ray_dir, center);
    return dist_sq <= radius * radius;
}

/*
 * Calculate LOD-adjusted max steps based on distance and lod_quality.
 *
 * Distance thresholds (scaled by lod_quality 0-2):
 *   Near:  < 40/60/80 units  -> full steps (52)
 *   Mid:   < 80/120/160 units -> medium (36)
 *   Far:   >= threshold      -> reduced (28)
 *
 * Minimum 56 steps to ensure full grid diagonal traversal (sqrt(32^2*3) ~ 55.4).
 */
int vobj_calc_distance_lod_steps(float distance, int lod_quality, int base_steps) {
    float quality_scale = float(max(lod_quality + 1, 1));
    float near_thresh = 40.0 * quality_scale;
    float far_thresh = 80.0 * quality_scale;

    int steps;
    if (distance < near_thresh) {
        steps = 96;                   // Full detail
    } else if (distance < far_thresh) {
        steps = 72;                   // Medium detail
    } else {
        steps = 56;                   // Reduced detail
    }
    return max(steps, 56);            // Minimum for grid diagonal traversal
}

/*
 * Calculate LOD-adjusted max steps based on screen coverage.
 * Large objects covering more screen need fewer steps (voxels span multiple pixels).
 *
 * Coverage thresholds (minimum 28 steps for diagonal traversal):
 *   > 50% screen -> reduced (56 steps minimum)
 *   > 30% screen -> reduced (56 steps minimum)
 *   > 15% screen -> reduced (56 steps minimum)
 *   > 5% screen  -> medium (56 steps)
 *   <= 5%        -> high detail (64 steps)
 */
int vobj_calc_coverage_lod_steps(float coverage, int base_steps) {
    int steps;
    if (coverage > 0.5) {
        steps = base_steps / 6;
    } else if (coverage > 0.3) {
        steps = base_steps / 4;
    } else if (coverage > 0.15) {
        steps = base_steps / 3;
    } else if (coverage > 0.05) {
        steps = base_steps / 2;
    } else {
        steps = (base_steps * 2) / 3;
    }
    return max(steps, 56);            // Minimum for grid diagonal traversal
}

/*
 * Combined LOD: take minimum of distance and coverage LOD.
 * This ensures large close objects get reduced steps.
 */
int vobj_calc_combined_lod_steps(float distance, float coverage, int lod_quality, int base_steps) {
    int dist_steps = vobj_calc_distance_lod_steps(distance, lod_quality, base_steps);
    int cov_steps = vobj_calc_coverage_lod_steps(coverage, base_steps);
    return min(dist_steps, cov_steps);
}

HitInfo vobj_march_object(
    int object_idx,
    vec3 world_origin,
    vec3 world_dir,
    int max_steps,
    float max_t
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
    float half_grid = grid_size * 0.5;

    vec3 local_min = obj.bounds_min.xyz;
    vec3 local_max = obj.bounds_max.xyz;

    vec2 box_t = hdda_intersect_aabb(local_origin, local_dir, local_min, local_max);
    if (box_t.x > box_t.y || box_t.y < 0.0) {
        return info;
    }

    /* AABB entry is behind current best hit — entire object is occluded */
    if (box_t.x > max_t) {
        return info;
    }

    float t_start = max(box_t.x, 0.001);
    vec3 start_local = local_origin + local_dir * t_start;
    vec3 grid_pos = start_local / voxel_size + vec3(half_grid);
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

    uint atlas_z_base = obj.atlas_slice * uint(grid_size);
    uint occupancy = obj.occupancy_mask;
    ivec3 grid_size_i = ivec3(int(grid_size));
    ivec3 last_region = ivec3(-1);

    for (int i = 0; i < max_steps; i++) {
        if (!hdda_in_bounds(dda.map_pos, grid_size_i)) {
            break;
        }

        /* Region occupancy check (2×2×2 regions of grid_size/2 voxels) */
        int region_size = grid_size_i.x / 2;
        ivec3 region = dda.map_pos / region_size;
        if (region != last_region) {
            last_region = region;
            int region_idx = region.x + region.y * 2 + region.z * 4;
            if ((occupancy & (1u << region_idx)) == 0u) {
                /* Skip to region boundary along the DDA exit axis */
                ivec3 region_min = region * region_size;
                ivec3 region_max = region_min + (region_size - 1);
                ivec3 cells_to_exit;
                cells_to_exit.x = (dda.step_dir.x > 0) ? (region_max.x - dda.map_pos.x + 1) : (dda.map_pos.x - region_min.x + 1);
                cells_to_exit.y = (dda.step_dir.y > 0) ? (region_max.y - dda.map_pos.y + 1) : (dda.map_pos.y - region_min.y + 1);
                cells_to_exit.z = (dda.step_dir.z > 0) ? (region_max.z - dda.map_pos.z + 1) : (dda.map_pos.z - region_min.z + 1);
                vec3 t_exit = dda.side_dist + vec3(cells_to_exit - 1) * dda.delta_dist;
                bvec3 mask = lessThanEqual(t_exit.xyz, min(t_exit.yzx, t_exit.zxy));
                dda.last_mask = mask;
                ivec3 skip_per_axis = ivec3(mask) * cells_to_exit;
                int skip = skip_per_axis.x + skip_per_axis.y + skip_per_axis.z;
                dda.map_pos += ivec3(mask) * dda.step_dir * cells_to_exit;
                dda.side_dist += vec3(skip_per_axis) * dda.delta_dist;
                i += max(skip - 1, 0);
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

            /* Local transform is pure rotation+translation (no scale in world_to_local),
               so hit_t_local equals world t-value. Compute world pos directly on world ray. */
            vec3 hit_world = world_origin + world_dir * hit_t_local;

            /* Transform normal from local to world space */
            vec3 local_normal = hdda_compute_normal(dda.last_mask, local_dir);
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
