#ifndef DATA_VOXOBJ_GLSL
#define DATA_VOXOBJ_GLSL

#include "hdda_types.glsl"

struct VoxelObjectGPU {
    mat4 world_to_local;    /* Transform ray to object space */
    mat4 local_to_world;    /* Transform hit back to world */
    vec4 bounds_min;        /* Local AABB min (xyz), voxel_size (w) */
    vec4 bounds_max;        /* Local AABB max (xyz), grid_size (w) */
    vec4 position;          /* World position (xyz), active flag (w) */
    uint atlas_slice;       /* Z-slice in 3D atlas */
    uint material_base;     /* Base material offset */
    uint flags;             /* Object flags */
    uint pad;
};

layout(set = 1, binding = 0) uniform usampler3D vobj_atlas;

layout(std430, set = 1, binding = 1) readonly buffer VoxelObjectMetadata {
    VoxelObjectGPU vobj_objects[];
};

uint sample_vobj_material(int object_idx, ivec3 local_pos, int grid_size) {
    VoxelObjectGPU obj = vobj_objects[object_idx];
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
    VoxelObjectGPU obj = vobj_objects[object_idx];
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
    VoxelObjectGPU obj = vobj_objects[object_idx];
    world_pos = (obj.local_to_world * vec4(local_pos, 1.0)).xyz;
    world_normal = normalize((obj.local_to_world * vec4(local_normal, 0.0)).xyz);
}

float vobj_get_voxel_size(int object_idx) {
    return vobj_objects[object_idx].bounds_min.w;
}

float vobj_get_grid_size(int object_idx) {
    return vobj_objects[object_idx].bounds_max.w;
}

float vobj_get_half_extent(int object_idx) {
    return vobj_objects[object_idx].bounds_max.x;
}

bool vobj_is_active(int object_idx) {
    return vobj_objects[object_idx].position.w > 0.0;
}

vec3 vobj_get_world_position(int object_idx) {
    return vobj_objects[object_idx].position.xyz;
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

    VoxelObjectGPU obj = vobj_objects[object_idx];

    vec3 local_origin = (obj.world_to_local * vec4(world_origin, 1.0)).xyz;
    vec3 local_dir = normalize((obj.world_to_local * vec4(world_dir, 0.0)).xyz);

    float voxel_size = obj.bounds_min.w;
    float grid_size = obj.bounds_max.w;
    float half_extent = obj.bounds_max.x;

    vec3 grid_min = vec3(0.0);
    vec3 grid_max = vec3(grid_size);

    vec3 local_pos_grid = (local_origin + vec3(half_extent)) / voxel_size;
    vec3 local_dir_grid = local_dir / voxel_size;

    vec2 box_t = hdda_intersect_aabb(local_pos_grid, local_dir_grid, grid_min, grid_max);
    if (box_t.x > box_t.y || box_t.y < 0.0) {
        return info;
    }

    float t_start = max(box_t.x, 0.001);
    vec3 start_pos = local_pos_grid + local_dir_grid * t_start;

    ivec3 map_pos = ivec3(floor(start_pos));
    map_pos = clamp(map_pos, ivec3(0), ivec3(int(grid_size) - 1));

    vec3 inv_dir = 1.0 / (abs(local_dir_grid) + vec3(0.0001));
    ivec3 step_dir = ivec3(sign(local_dir_grid));
    vec3 dir_sign = sign(local_dir_grid);
    vec3 step_sign = step(vec3(0.0), dir_sign);
    vec3 side_dist = inv_dir * (step_sign + dir_sign * (vec3(map_pos) - start_pos));

    uint atlas_z_base = obj.atlas_slice * uint(grid_size);

    for (int i = 0; i < max_steps; i++) {
        if (map_pos.x < 0 || map_pos.x >= int(grid_size) ||
            map_pos.y < 0 || map_pos.y >= int(grid_size) ||
            map_pos.z < 0 || map_pos.z >= int(grid_size)) {
            break;
        }

        ivec3 texel = ivec3(map_pos.x, map_pos.y, int(atlas_z_base) + map_pos.z);
        uint mat = texelFetch(vobj_atlas, texel, 0).r;

        if (mat != 0u) {
            vec3 voxel_min_local = (vec3(map_pos) - vec3(grid_size * 0.5)) * voxel_size;
            vec3 voxel_max_local = voxel_min_local + vec3(voxel_size);

            vec2 voxel_t = hdda_intersect_aabb(local_origin, local_dir, voxel_min_local, voxel_max_local);
            float hit_t_local = max(voxel_t.x, 0.0);
            vec3 hit_local = local_origin + local_dir * hit_t_local;

            vec3 voxel_center = voxel_min_local + vec3(voxel_size * 0.5);
            vec3 rel = hit_local - voxel_center;
            vec3 abs_rel = abs(rel);
            float max_comp = max(max(abs_rel.x, abs_rel.y), abs_rel.z);

            vec3 local_normal;
            if (abs_rel.x >= max_comp - 0.0001) {
                local_normal = vec3(sign(rel.x), 0.0, 0.0);
            } else if (abs_rel.y >= max_comp - 0.0001) {
                local_normal = vec3(0.0, sign(rel.y), 0.0);
            } else {
                local_normal = vec3(0.0, 0.0, sign(rel.z));
            }

            vec3 hit_world = (obj.local_to_world * vec4(hit_local, 1.0)).xyz;
            vec3 world_normal = normalize((obj.local_to_world * vec4(local_normal, 0.0)).xyz);

            info.hit = true;
            info.material_id = mat;
            info.pos = hit_world;
            info.normal = world_normal;
            info.t = length(hit_world - world_origin);
            info.voxel_coord = map_pos;

            return info;
        }

        bvec3 mask = lessThanEqual(side_dist.xyz, min(side_dist.yzx, side_dist.zxy));
        side_dist += vec3(mask) * inv_dir;
        map_pos += ivec3(mask) * step_dir;
    }

    return info;
}

#endif /* DATA_VOXOBJ_GLSL */
