#version 450

#if defined(PATCH_VULKAN)
#define SET_BINDING(set_, binding_) set = set_, binding = binding_
#else
#define SET_BINDING(set_, binding_) binding = binding_
#endif

layout(location = 0) in vec3 in_world_pos;
layout(location = 1) in vec3 in_ray_origin;
layout(location = 2) in flat int in_object_index;

layout(location = 0) out vec4 out_albedo;
layout(location = 1) out vec4 out_normal;
layout(location = 2) out vec4 out_material;
layout(location = 3) out float out_linear_depth;

struct VoxelObjectGPU {
    mat4 world_to_local;
    mat4 local_to_world;
    vec4 bounds_min;
    vec4 bounds_max;
    vec4 position;
    uint atlas_slice;
    uint material_base;
    uint flags;
    uint pad;
};

layout(SET_BINDING(0, 0)) uniform usampler3D vobj_atlas;

layout(std430, SET_BINDING(0, 1)) readonly buffer ObjectMetadata {
    VoxelObjectGPU objects[];
};

layout(std140, SET_BINDING(0, 2)) uniform MaterialPalette {
    vec4 materials[512];
};

layout(push_constant) uniform Constants {
    mat4 view_proj;
    vec3 camera_pos;
    float pad1;
    int object_count;
    int atlas_dim;
    float near_plane;
    float far_plane;
} pc;

vec3 get_material_color(uint material_id) {
    return materials[material_id * 2u].rgb;
}

float get_material_emissive(uint material_id) {
    return materials[material_id * 2u].a;
}

float get_material_roughness(uint material_id) {
    return materials[material_id * 2u + 1u].r;
}

float get_material_metallic(uint material_id) {
    return materials[material_id * 2u + 1u].g;
}

vec2 intersect_box(vec3 origin, vec3 dir, vec3 box_min, vec3 box_max) {
    vec3 inv_dir = 1.0 / dir;
    vec3 t0 = (box_min - origin) * inv_dir;
    vec3 t1 = (box_max - origin) * inv_dir;
    vec3 tmin = min(t0, t1);
    vec3 tmax = max(t0, t1);
    float t_near = max(max(tmin.x, tmin.y), tmin.z);
    float t_far = min(min(tmax.x, tmax.y), tmax.z);
    return vec2(t_near, t_far);
}

void main() {
    if (in_object_index < 0) {
        discard;
    }

    VoxelObjectGPU obj = objects[in_object_index];

    vec3 world_ray_dir = normalize(in_world_pos - in_ray_origin);

    vec3 local_origin = (obj.world_to_local * vec4(in_ray_origin, 1.0)).xyz;
    vec3 local_dir = normalize((obj.world_to_local * vec4(world_ray_dir, 0.0)).xyz);

    float voxel_size = obj.bounds_min.w;
    float grid_size = obj.bounds_max.w;
    float half_extent = obj.bounds_max.x;

    vec3 grid_min = vec3(0.0);
    vec3 grid_max = vec3(grid_size);

    // Convert to grid space - both position AND direction
    vec3 local_pos_grid = (local_origin + vec3(half_extent)) / voxel_size;
    vec3 local_dir_grid = local_dir / voxel_size;  // Scale direction to grid space

    vec2 box_t = intersect_box(local_pos_grid, local_dir_grid, grid_min, grid_max);

    if (box_t.x > box_t.y || box_t.y < 0.0) {
        discard;
    }

    float t_start = max(box_t.x, 0.001);
    vec3 start_pos = local_pos_grid + local_dir_grid * t_start;

    ivec3 map_pos = ivec3(floor(start_pos));
    map_pos = clamp(map_pos, ivec3(0), ivec3(int(grid_size) - 1));

    // DDA setup with epsilon to avoid division by zero
    vec3 inv_dir = 1.0 / (abs(local_dir_grid) + vec3(0.0001));
    ivec3 step_dir = ivec3(sign(local_dir_grid));
    vec3 dir_sign = sign(local_dir_grid);
    vec3 step_sign = step(vec3(0.0), dir_sign);  // 0 if dir<0, 1 if dir>=0
    vec3 side_dist = inv_dir * (step_sign + dir_sign * (vec3(map_pos) - start_pos));

    uint atlas_z_base = obj.atlas_slice * uint(grid_size);

    const int MAX_STEPS = 48;
    for (int i = 0; i < MAX_STEPS; i++) {
        if (map_pos.x < 0 || map_pos.x >= int(grid_size) ||
            map_pos.y < 0 || map_pos.y >= int(grid_size) ||
            map_pos.z < 0 || map_pos.z >= int(grid_size)) {
            break;
        }

        ivec3 texel = ivec3(map_pos.x, map_pos.y, int(atlas_z_base) + map_pos.z);
        uint mat = texelFetch(vobj_atlas, texel, 0).r;

        if (mat != 0u) {
            // Compute voxel bounds in world units (local object space)
            // map_pos is [0, grid_size], convert to centered world coords
            vec3 voxel_min_world = (vec3(map_pos) - vec3(grid_size * 0.5)) * voxel_size;
            vec3 voxel_max_world = voxel_min_world + vec3(voxel_size);

            vec2 voxel_t = intersect_box(local_origin, local_dir, voxel_min_world, voxel_max_world);
            float hit_t_local = max(voxel_t.x, 0.0);
            vec3 hit_local = local_origin + local_dir * hit_t_local;

            vec3 hit_world = (obj.local_to_world * vec4(hit_local, 1.0)).xyz;

            vec3 voxel_center = voxel_min_world + vec3(voxel_size * 0.5);
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

            vec3 world_normal = normalize((obj.local_to_world * vec4(local_normal, 0.0)).xyz);

            float linear_depth = length(hit_world - in_ray_origin);

            out_albedo = vec4(get_material_color(mat), 1.0);
            out_normal = vec4(world_normal * 0.5 + 0.5, 1.0);
            out_material = vec4(get_material_roughness(mat), get_material_metallic(mat), get_material_emissive(mat), 0.0);
            out_linear_depth = linear_depth;

            gl_FragDepth = (pc.far_plane - pc.near_plane * pc.far_plane / linear_depth) / (pc.far_plane - pc.near_plane);

            return;
        }

        bvec3 mask = lessThanEqual(side_dist.xyz, min(side_dist.yzx, side_dist.zxy));
        side_dist += vec3(mask) * inv_dir;
        map_pos += ivec3(mask) * step_dir;
    }

    discard;
}
