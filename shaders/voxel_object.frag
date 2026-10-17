#version 450

/* voxel_object.frag - raymarches one voxel object inside its proxy cube and writes the
 * G-buffer (with depth, so objects intersect the terrain and each other correctly). */

layout(location = 0) in vec3 in_world_pos;
layout(location = 1) in flat int in_object_index;

layout(location = 0) out vec4 out_albedo;
layout(location = 1) out vec4 out_normal;
layout(location = 2) out uvec2 out_material;
layout(location = 3) out float out_linear_depth;
layout(location = 4) out vec2 out_motion_vector;

/* The rasterized depth is at most the hit's: front faces of the proxy cube lie before the
 * voxels, and back faces (camera inside) are drawn on the near plane (voxel_object.vert) */
layout(depth_greater) out float gl_FragDepth;

#include "include/voxobj_types.glsl"

layout(set = 0, binding = 0) uniform usampler3D vobj_atlas;

layout(std430, set = 0, binding = 1) readonly buffer ObjectMetadata {
    VoxelObjectGPU objects[];
};

layout(std140, set = 0, binding = 2) uniform MaterialPalette {
    vec4 materials[512];
};

layout(std140, set = 0, binding = 3) uniform TemporalUBO {
    mat4 prev_view_proj;
    vec4 prev_camera_pos;
    vec2 jitter_uv;
    vec2 inv_render_size;
};

#include "include/materials.glsl"
#include "include/camera.glsl"
#include "include/data_voxobj.glsl"
#include "include/debug_views.glsl"

/* Mirror of VoxelObjectPushConstants (engine/render/raster_gpu_types.h) */
layout(push_constant) uniform Constants {
    mat4 view_proj;
    vec3 camera_pos;
    float near_plane;
    float far_plane;
    int debug_mode;
    float near_clip_radius;
    int camera_inside;
} pc;

void main() {
    if (in_object_index < 0) {
        discard;
    }

    vec3 ray_origin = pc.camera_pos;
    vec3 ray_dir = normalize(in_world_pos - pc.camera_pos);

    HitInfo hit = vobj_march_object(in_object_index, ray_origin, ray_dir, pc.far_plane);
    if (!hit.hit) {
        discard;
    }

    vec3 color = hit.color;

    /* Collider visualization: orange tint for voxel objects */
    if (pc.debug_mode == DEBUG_VIEW_OBJECTS) {
        color = mix(color, vec3(1.0, 0.6, 0.2), 0.7);
    }

    out_albedo = vec4(color, 1.0);
    out_normal = vec4(hit.normal * 0.5 + 0.5, 1.0);
    /* Objects are not traced through water: no water material or depth */
    out_material = uvec2(packUnorm4x8(vec4(hit.roughness, hit.metallic, hit.emissive, 0.0)), 0u);

    /* Linear depth as in raymarch_gbuffer.comp: distance from the eye along the ray */
    float linear_depth = hit.t;
    out_linear_depth = linear_depth;

    /* Motion vectors: where this point of the object was last frame, under its previous
     * transform (local_to_world takes voxel units, world_to_local gives metres) */
    vec3 local_voxels = (objects[in_object_index].world_to_local * vec4(hit.pos, 1.0)).xyz /
                        objects[in_object_index].bounds_min.w;
    vec3 prev_pos = (objects[in_object_index].prev_local_to_world * vec4(local_voxels, 1.0)).xyz;
    out_motion_vector = camera_motion_vector(gl_FragCoord.xy * inv_render_size - jitter_uv, prev_view_proj * vec4(prev_pos, 1.0));

    float ndc_depth = camera_linear_depth_to_ndc(linear_depth, pc.near_plane, pc.far_plane);
    gl_FragDepth = max(clamp(ndc_depth, 0.0, 1.0), gl_FragCoord.z);
}
