#ifndef CAMERA_GLSL
#define CAMERA_GLSL

/*
 * camera.glsl - Unified camera utilities for ray generation and depth handling
 *
 * All functions take explicit parameters to work with any push constant naming.
 * Supports both perspective and orthographic projection.
 */

/*
 * Generate a ray for a given pixel coordinate.
 *
 * For perspective: rays originate from camera_pos, direction varies per pixel
 * For orthographic: ray origin varies per pixel, direction is constant (forward)
 */
void camera_generate_ray(
    ivec2 pixel,
    ivec2 screen_size,
    mat4 inv_view,
    mat4 inv_projection,
    vec3 camera_pos,
    bool is_orthographic,
    out vec3 ray_origin,
    out vec3 ray_dir
) {
    vec2 uv = (vec2(pixel) + 0.5) / vec2(screen_size);
    vec2 ndc = uv * 2.0 - 1.0;

    if (is_orthographic) {
        /* Orthographic: parallel rays from near plane */
        vec4 near_clip = vec4(ndc.x, ndc.y, 0.0, 1.0);
        vec4 far_clip = vec4(ndc.x, ndc.y, 1.0, 1.0);
        vec4 near_view = inv_projection * near_clip;
        vec4 far_view = inv_projection * far_clip;
        vec3 near_world = (inv_view * vec4(near_view.xyz, 1.0)).xyz;
        vec3 far_world = (inv_view * vec4(far_view.xyz, 1.0)).xyz;
        ray_origin = near_world;
        ray_dir = normalize(far_world - near_world);
    } else {
        /* Perspective: rays from camera position */
        vec4 ray_clip = vec4(ndc.x, ndc.y, -1.0, 1.0);
        vec4 ray_view = inv_projection * ray_clip;
        ray_view.z = -1.0;
        ray_view.w = 0.0;
        ray_dir = normalize((inv_view * ray_view).xyz);
        ray_origin = camera_pos;
    }
}

/*
 * Generate a ray from UV coordinates (for fragment shaders).
 */
void camera_generate_ray_uv(
    vec2 uv,
    mat4 inv_view,
    mat4 inv_projection,
    vec3 camera_pos,
    bool is_orthographic,
    out vec3 ray_origin,
    out vec3 ray_dir
) {
    vec2 ndc = uv * 2.0 - 1.0;

    if (is_orthographic) {
        vec4 near_clip = vec4(ndc.x, ndc.y, 0.0, 1.0);
        vec4 far_clip = vec4(ndc.x, ndc.y, 1.0, 1.0);
        vec4 near_view = inv_projection * near_clip;
        vec4 far_view = inv_projection * far_clip;
        vec3 near_world = (inv_view * vec4(near_view.xyz, 1.0)).xyz;
        vec3 far_world = (inv_view * vec4(far_view.xyz, 1.0)).xyz;
        ray_origin = near_world;
        ray_dir = normalize(far_world - near_world);
    } else {
        vec4 ray_clip = vec4(ndc.x, ndc.y, -1.0, 1.0);
        vec4 ray_view = inv_projection * ray_clip;
        ray_view.z = -1.0;
        ray_view.w = 0.0;
        ray_dir = normalize((inv_view * ray_view).xyz);
        ray_origin = camera_pos;
    }
}

/*
 * Reconstruct world position from UV and linear depth.
 *
 * This is the inverse of the ray generation - given where a ray hit,
 * compute the 3D world position.
 */
vec3 camera_reconstruct_world_pos(
    vec2 uv,
    float depth,
    mat4 inv_view,
    mat4 inv_projection,
    vec3 camera_pos,
    bool is_orthographic
) {
    vec2 ndc = uv * 2.0 - 1.0;

    if (is_orthographic) {
        vec4 near_clip = vec4(ndc.x, ndc.y, 0.0, 1.0);
        vec4 far_clip = vec4(ndc.x, ndc.y, 1.0, 1.0);
        vec4 near_view = inv_projection * near_clip;
        vec4 far_view = inv_projection * far_clip;
        vec3 near_world = (inv_view * vec4(near_view.xyz, 1.0)).xyz;
        vec3 far_world = (inv_view * vec4(far_view.xyz, 1.0)).xyz;
        vec3 ray_dir = normalize(far_world - near_world);
        return near_world + ray_dir * depth;
    } else {
        vec4 ray_clip = vec4(ndc.x, ndc.y, -1.0, 1.0);
        vec4 ray_view = inv_projection * ray_clip;
        ray_view.z = -1.0;
        ray_view.w = 0.0;
        vec3 ray_dir = normalize((inv_view * ray_view).xyz);
        return camera_pos + ray_dir * depth;
    }
}

/*
 * Convert linear depth to NDC depth for gl_FragDepth.
 *
 * Perspective uses reverse-Z mapping for better depth precision.
 * Orthographic uses linear mapping since depth is distance from near plane.
 */
float camera_linear_depth_to_ndc_ortho(float linear_depth, float near, float far, bool is_orthographic) {
    if (is_orthographic) {
        /* Ortho: depth is distance from near plane, maps linearly to [0,1] */
        return clamp(linear_depth / (far - near), 0.0, 1.0);
    } else {
        /* Perspective: reverse-Z for better precision */
        return (far - near * far / linear_depth) / (far - near);
    }
}

/* Legacy overload for shaders without orthographic flag - assumes perspective */
float camera_linear_depth_to_ndc(float linear_depth, float near, float far) {
    return (far - near * far / linear_depth) / (far - near);
}

/*
 * Get camera forward direction in world space.
 * For orthographic projection, this is the ray direction (same for all pixels).
 */
vec3 camera_get_forward(mat4 inv_view) {
    /* Camera looks down -Z in view space, transform to world space */
    return normalize((inv_view * vec4(0.0, 0.0, -1.0, 0.0)).xyz);
}

#endif /* CAMERA_GLSL */
