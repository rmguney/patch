#version 450

/* depth_prime.frag - converts the compute G-buffer's linear depth into hardware depth so
 * the rasterized objects and particles depth-test against the terrain. */

#include "include/camera.glsl"

layout(location = 0) in vec2 in_uv;

layout(set = 0, binding = 0) uniform sampler2D gbuffer_linear_depth;

layout(push_constant) uniform Constants {
    float near_plane;
    float far_plane;
} pc;

void main() {
    float linear_depth = texture(gbuffer_linear_depth, in_uv).r;

    /* Sky pixels hold 1e10 */
    if (linear_depth > 1e9) {
        gl_FragDepth = 1.0;
        return;
    }

    gl_FragDepth = clamp(camera_linear_depth_to_ndc(linear_depth, pc.near_plane, pc.far_plane), 0.0, 1.0);
}
