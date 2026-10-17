#ifndef CAMERA_GLSL
#define CAMERA_GLSL

/*
 * camera.glsl - perspective camera ray generation and depth conversion.
 * Linear depth is the distance along the primary ray from the camera position.
 */

/* Primary ray through the center of a pixel */
void camera_generate_ray(
    ivec2 pixel,
    ivec2 screen_size,
    mat4 inv_view,
    mat4 inv_projection,
    vec3 camera_pos,
    out vec3 ray_origin,
    out vec3 ray_dir
) {
    vec2 ndc = (vec2(pixel) + 0.5) / vec2(screen_size) * 2.0 - 1.0;

    vec4 ray_view = inv_projection * vec4(ndc, -1.0, 1.0);
    ray_dir = normalize((inv_view * vec4(ray_view.xy, -1.0, 0.0)).xyz);
    ray_origin = camera_pos;
}

/* Linear depth to the [0, 1] depth-buffer value of the projection (standard, not reversed Z) */
float camera_linear_depth_to_ndc(float linear_depth, float near, float far) {
    return (far - near * far / linear_depth) / (far - near);
}

/* Clip w (view distance) below which a point counts as at or behind the previous camera */
const float CAMERA_MIN_PREV_W = 1e-4;

/*
 * Motion vector (previous uv - current uv) of a point from where it is on screen this frame
 * without the TAA jitter (the pixel centre minus the jitter, both in uv) and its clip position
 * last frame. A point at or behind the previous camera had no screen position: the result lies
 * off screen, so every temporal pass rejects its history.
 */
vec2 camera_motion_vector(vec2 curr_uv, vec4 prev_clip) {
    if (prev_clip.w <= CAMERA_MIN_PREV_W) {
        return vec2(2.0);
    }
    vec2 prev_uv = (prev_clip.xy / prev_clip.w) * 0.5 + 0.5;
    return prev_uv - curr_uv;
}

#endif /* CAMERA_GLSL */
