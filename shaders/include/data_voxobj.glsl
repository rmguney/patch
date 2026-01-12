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
                hdda_step(dda);
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
