#version 450

/*
 * spatial_denoise.frag - edge-aware (depth / normal) Poisson blur of the lit image, before the
 * TAA (on the jittered frame its depth and normals belong to), or with TAA off straight into
 * the swapchain image.
 */

#include "include/voxel_push_constants.glsl"
#include "include/debug_views.glsl"

layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D gbuffer_linear_depth;
layout(set = 0, binding = 1) uniform sampler2D gbuffer_normal;
layout(set = 0, binding = 2) uniform sampler2D lit_color_input;

const float GOLDEN_ANGLE = 2.399963229728653;
const float TWO_PI = 6.283185307179586;
const int SAMPLE_COUNT = 8;
const float SAMPLE_RADIUS = 3.0;
const float DEPTH_THRESHOLD = 0.05;
const float DEBUG_DENOISE_DIFF_SCALE = 4.0;

vec2 get_poisson_sample(int i, float rotation) {
    float r = sqrt(float(i + 1) / float(SAMPLE_COUNT + 1));
    float theta = float(i) * GOLDEN_ANGLE + rotation;
    return vec2(cos(theta), sin(theta)) * r * SAMPLE_RADIUS;
}

/*
 * Rotation of the pattern in [0, 2 pi): interleaved gradient noise of the pixel, the same every
 * frame: with TAA off a rotation that changed per frame would show as shimmer.
 */
float pixel_rotation(vec2 pixel) {
    return TWO_PI * fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

void main() {
    vec2 size = vec2(textureSize(lit_color_input, 0));
    vec2 uv = gl_FragCoord.xy / size;

    float depth_center = texture(gbuffer_linear_depth, uv).r;
    vec3 normal_center = texture(gbuffer_normal, uv).rgb * 2.0 - 1.0;
    vec4 color_center = texture(lit_color_input, uv);

    /* DEBUG: Bypass denoise and show raw lit color */
    if (pc.debug_mode == DEBUG_VIEW_DENOISE_RAW) {
        out_color = color_center;
        return;
    }

    /* Sky pixels (linear depth 1e10) pass through unchanged */
    if (depth_center > 1e9) {
        out_color = color_center;
        return;
    }

    float rotation = pixel_rotation(floor(gl_FragCoord.xy));

    vec3 color_sum = color_center.rgb;
    float weight_sum = 1.0;

    vec2 texel_size = 1.0 / size;

    for (int i = 0; i < SAMPLE_COUNT; i++) {
        vec2 offset = get_poisson_sample(i, rotation);
        vec2 sample_uv = uv + offset * texel_size;

        /* Clamp to valid range */
        sample_uv = clamp(sample_uv, vec2(0.0), vec2(1.0));

        float depth_sample = texture(gbuffer_linear_depth, sample_uv).r;
        vec3 normal_sample = texture(gbuffer_normal, sample_uv).rgb * 2.0 - 1.0;
        vec3 color_sample = texture(lit_color_input, sample_uv).rgb;

        /* Bilateral weights */
        float depth_diff = abs(depth_sample - depth_center);
        float depth_weight = exp(-depth_diff / DEPTH_THRESHOLD);

        float normal_dot = max(0.0, dot(normal_sample, normal_center));
        float normal_weight = step(0.9, normal_dot);

        float weight = depth_weight * normal_weight;

        color_sum += color_sample * weight;
        weight_sum += weight;
    }

    vec3 result = color_sum / weight_sum;

    /* DEBUG: Visualize denoise delta */
    if (pc.debug_mode == DEBUG_VIEW_DENOISE_DIFF) {
        vec3 diff = abs(result - color_center.rgb) * DEBUG_DENOISE_DIFF_SCALE;
        out_color = vec4(diff, 1.0);
        return;
    }

    out_color = vec4(result, color_center.a);
}
