#version 450

#if defined(PATCH_VULKAN)
#define SET_BINDING(set_, binding_) set = set_, binding = binding_
#else
#define SET_BINDING(set_, binding_) binding = binding_
#endif

layout(location = 0) in vec3 in_world_pos;
layout(location = 1) in vec3 in_ray_origin;
layout(location = 2) in flat int in_object_index;
layout(location = 3) in flat float in_screen_coverage;
layout(location = 4) in flat float in_distance;

layout(location = 0) out vec4 out_albedo;
layout(location = 1) out vec4 out_normal;
layout(location = 2) out vec4 out_material;
layout(location = 3) out float out_linear_depth;
layout(location = 4) out vec2 out_motion_vector;

/* Enable early-z: we only write depth >= interpolated bounding box depth */
layout(depth_greater) out float gl_FragDepth;

struct VoxelObjectGPU {
    mat4 world_to_local;
    mat4 local_to_world;
    vec4 bounds_min;
    vec4 bounds_max;
    vec4 position;
    uint atlas_slice;
    uint material_base;
    uint flags;
    uint occupancy_mask;
};

layout(SET_BINDING(0, 0)) uniform usampler3D vobj_atlas;

layout(std430, SET_BINDING(0, 1)) readonly buffer ObjectMetadata {
    VoxelObjectGPU objects[];
};

layout(std140, SET_BINDING(0, 2)) uniform MaterialPalette {
    vec4 materials[512];
};

layout(std140, SET_BINDING(0, 3)) uniform TemporalUBO {
    mat4 prev_view_proj;
};

#include "include/materials.glsl"
#include "include/camera.glsl"
#include "include/data_voxobj.glsl"

layout(push_constant) uniform Constants {
    mat4 view_proj;
    vec3 camera_pos;
    float pad1;
    int object_count;
    int atlas_dim;
    float near_plane;
    float far_plane;
    int debug_mode;
    int rt_quality;
    int pad2[2];
} pc;

const int VOBJ_BASE_STEPS = 48;

void main() {
    if (in_object_index < 0) {
        discard;
    }

    vec3 world_ray_dir = normalize(in_world_pos - in_ray_origin);

    /* Calculate LOD-adjusted step count based on distance AND screen coverage */
    int max_steps = vobj_calc_combined_lod_steps(
        in_distance, 
        in_screen_coverage, 
        pc.rt_quality, 
        VOBJ_BASE_STEPS
    );

    HitInfo hit = vobj_march_object(in_object_index, in_ray_origin, world_ray_dir, max_steps);

    if (!hit.hit) {
        discard;
    }

    vec3 color = hit.color;

    /* Debug mode 8: Collider visualization - orange tint for voxel objects */
    if (pc.debug_mode == 8) {
        color = mix(color, vec3(1.0, 0.6, 0.2), 0.7);
    }

    out_albedo = vec4(color, 1.0);
    out_normal = vec4(hit.normal * 0.5 + 0.5, 1.0);
    out_material = vec4(hit.roughness, hit.metallic, hit.emissive, 0.0);
    out_linear_depth = hit.t;

    /* Motion vectors for temporal effects */
    vec4 curr_clip = pc.view_proj * vec4(hit.pos, 1.0);
    vec4 prev_clip = prev_view_proj * vec4(hit.pos, 1.0);
    vec2 curr_uv = (curr_clip.xy / curr_clip.w) * 0.5 + 0.5;
    vec2 prev_uv = (prev_clip.xy / prev_clip.w) * 0.5 + 0.5;
    out_motion_vector = prev_uv - curr_uv;

    gl_FragDepth = clamp(camera_linear_depth_to_ndc(hit.t, pc.near_plane, pc.far_plane), 0.0, 1.0);
}
