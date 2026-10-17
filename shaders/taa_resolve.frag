#version 450

/*
 * taa_resolve.frag - temporal anti-aliasing. Each frame is rendered with another sub-pixel
 * jitter of the projection (Renderer::jitter_), so the frames sample every pixel at different
 * points; this blends the frame with the history reprojected by the motion vectors (which
 * carry no jitter), resampled with a Catmull-Rom filter and clipped toward the variance box
 * of the frame's 3x3 neighbourhood in YCoCg. Writes the display image and the next history.
 */

#include "include/voxel_push_constants.glsl"

layout(location = 0) out vec4 out_display;
layout(location = 1) out vec4 out_history;

layout(set = 0, binding = 0) uniform sampler2D current_color;  /* nearest */
layout(set = 0, binding = 1) uniform sampler2D history_color;  /* bilinear */
layout(set = 0, binding = 2) uniform sampler2D motion_vectors; /* nearest */
layout(set = 0, binding = 3) uniform sampler2D linear_depth;   /* nearest */

/* History weight when still: about ten frames, more than the jitter's eight phases. Motion
 * lowers it, since a history resampled every frame softens. */
const float HISTORY_WEIGHT_STILL = 0.9;
const float HISTORY_WEIGHT_MOVING = 0.75;
const float MOVING_PIXELS = 8.0; /* motion per frame that counts as fully moving */
const float VARIANCE_CLIP_GAMMA = 1.0;

vec3 rgb_to_ycocg(vec3 rgb) {
    return vec3(
        0.25 * rgb.r + 0.5 * rgb.g + 0.25 * rgb.b,
        0.5 * rgb.r - 0.5 * rgb.b,
        -0.25 * rgb.r + 0.5 * rgb.g - 0.25 * rgb.b
    );
}

vec3 ycocg_to_rgb(vec3 ycocg) {
    return vec3(
        ycocg.x + ycocg.y - ycocg.z,
        ycocg.x + ycocg.z,
        ycocg.x - ycocg.y - ycocg.z
    );
}

/* Motion of the nearest surface around the pixel: along an object's edge the jitter puts the
 * pixel on the object in some frames and behind it in others, and both follow the object */
vec2 nearest_motion(ivec2 pixel, ivec2 size) {
    ivec2 nearest = pixel;
    float nearest_depth = texelFetch(linear_depth, pixel, 0).r;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            ivec2 p = clamp(pixel + ivec2(dx, dy), ivec2(0), size - 1);
            float d = texelFetch(linear_depth, p, 0).r;
            if (d < nearest_depth) {
                nearest_depth = d;
                nearest = p;
            }
        }
    }
    return texelFetch(motion_vectors, nearest, 0).xy;
}

/* Catmull-Rom resampling of the history in five bilinear taps (the corner taps of the 4x4
 * footprint carry little weight and are left out); keeps the reprojected history sharp */
vec3 sample_history(vec2 uv, vec2 size) {
    vec2 position = uv * size;
    vec2 center = floor(position - 0.5) + 0.5;
    vec2 f = position - center;
    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    vec2 w3 = f * f * (-0.5 + 0.5 * f);
    vec2 w12 = w1 + w2;
    vec2 uv0 = (center - 1.0) / size;
    vec2 uv3 = (center + 2.0) / size;
    vec2 uv12 = (center + w2 / w12) / size;

    vec3 sum = texture(history_color, vec2(uv12.x, uv0.y)).rgb * (w12.x * w0.y) +
               texture(history_color, vec2(uv0.x, uv12.y)).rgb * (w0.x * w12.y) +
               texture(history_color, uv12).rgb * (w12.x * w12.y) +
               texture(history_color, vec2(uv3.x, uv12.y)).rgb * (w3.x * w12.y) +
               texture(history_color, vec2(uv12.x, uv3.y)).rgb * (w12.x * w3.y);
    float weight = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
    return max(sum / weight, vec3(0.0));
}

/* The history moved toward the box centre until it lies inside the box: unlike a per-channel
 * clamp it keeps its hue */
vec3 clip_to_box(vec3 history, vec3 center, vec3 extents) {
    vec3 offset = history - center;
    vec3 units = abs(offset) / max(extents, vec3(1e-5));
    float outside = max(units.x, max(units.y, units.z));
    return outside > 1.0 ? center + offset / outside : history;
}

void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    ivec2 size = textureSize(current_color, 0);
    vec3 current = texelFetch(current_color, pixel, 0).rgb;

    out_display = vec4(current, 1.0);
    out_history = vec4(current, 1.0);
    if (pc.history_valid == 0) {
        return;
    }

    vec2 motion = nearest_motion(pixel, size);
    vec2 prev_uv = (vec2(pixel) + 0.5) / vec2(size) + motion;
    if (any(lessThan(prev_uv, vec2(0.0))) || any(greaterThan(prev_uv, vec2(1.0)))) {
        return;
    }

    /* Mean and standard deviation of the 3x3 neighbourhood */
    vec3 m1 = vec3(0.0);
    vec3 m2 = vec3(0.0);
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            ivec2 p = clamp(pixel + ivec2(dx, dy), ivec2(0), size - 1);
            vec3 s = rgb_to_ycocg(texelFetch(current_color, p, 0).rgb);
            m1 += s;
            m2 += s * s;
        }
    }
    m1 /= 9.0;
    m2 /= 9.0;
    vec3 sigma = sqrt(max(m2 - m1 * m1, vec3(0.0)));

    vec3 history = rgb_to_ycocg(sample_history(prev_uv, vec2(size)));
    history = ycocg_to_rgb(clip_to_box(history, m1, sigma * VARIANCE_CLIP_GAMMA));

    float moving = clamp(length(motion * vec2(size)) / MOVING_PIXELS, 0.0, 1.0);
    vec3 resolved = mix(current, history, mix(HISTORY_WEIGHT_STILL, HISTORY_WEIGHT_MOVING, moving));
    out_display = vec4(resolved, 1.0);
    out_history = vec4(resolved, 1.0);
}
