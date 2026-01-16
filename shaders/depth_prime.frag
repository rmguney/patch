#version 450

#if defined(PATCH_VULKAN)
#define SET_BINDING(set_, binding_) set = set_, binding = binding_
#else
#define SET_BINDING(set_, binding_) binding = binding_
#endif

#include "include/camera.glsl"

layout(location = 0) in vec2 in_uv;

layout(SET_BINDING(0, 0)) uniform sampler2D gbuffer_linear_depth;

layout(push_constant) uniform Constants {
    float near_plane;
    float far_plane;
} pc;

void main() {
    float linear_depth = texture(gbuffer_linear_depth, in_uv).r;

    /* Skip sky pixels (very large depth values) */
    if (linear_depth > 1e9) {
        gl_FragDepth = 1.0;
        return;
    }

    /* Clamp to valid range like terrain fragment shader does */
    gl_FragDepth = clamp(camera_linear_depth_to_ndc(linear_depth, pc.near_plane, pc.far_plane), 0.0, 1.0);
}
