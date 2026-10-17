#ifndef GBUFFER_SAMPLE_GLSL
#define GBUFFER_SAMPLE_GLSL

/* RAY REUSE CONTRACT: Downstream passes must sample G-buffer via these functions,
   never re-cast primary rays. Primary rays are cast once in raymarch_gbuffer.comp. */

/*
 * G-buffer layout (RenderTargets::RASTER_COLOR_FORMATS):
 *   albedo        RGBA8     rgb albedo, a = 0 on sky pixels
 *   normal        RGB10A2   normal * 0.5 + 0.5
 *   material      RG32UI    x: roughness, metallic, emissive (8-bit unorm each, as packUnorm4x8
 *                              packs them) and in the top byte the material id of the water the
 *                              ray crossed; y: water depth in metres (float bits, 0 = none)
 *   linear depth  R32F      distance from the camera along the pixel's primary ray (sky: 1e10)
 * The world position is not stored: it is the camera position plus the pixel's primary ray
 * (camera_generate_ray, the ray raymarch_gbuffer.comp traced) times the linear depth.
 *
 * The including shader declares pc (voxel_push_constants.glsl).
 */

#include "camera.glsl"

struct GBufferSample {
    vec3 world_pos;
    vec3 normal;
    vec3 albedo;
    float linear_depth;
    float roughness;
    float metallic;
    float emissive;
    float water_depth;
    uint water_material;
    bool is_sky;
};

uvec2 gbuffer_pack_material(float roughness, float metallic, float emissive, uint water_material, float water_depth) {
    uint properties = packUnorm4x8(vec4(roughness, metallic, emissive, 0.0)) & 0x00FFFFFFu;
    return uvec2(properties | (water_material << 24), floatBitsToUint(water_depth));
}

/* World position of the surface a pixel shows, from its linear depth */
vec3 gbuffer_world_pos(ivec2 pixel, ivec2 size, float linear_depth) {
    vec3 ray_origin, ray_dir;
    camera_generate_ray(pixel, size, pc.inv_view, pc.inv_projection, pc.camera_pos, ray_origin, ray_dir);
    return ray_origin + ray_dir * linear_depth;
}

GBufferSample sample_gbuffer(
    ivec2 pixel,
    sampler2D gbuffer_albedo,
    sampler2D gbuffer_normal,
    usampler2D gbuffer_material,
    sampler2D gbuffer_depth
) {
    GBufferSample s;

    vec4 albedo_sample = texelFetch(gbuffer_albedo, pixel, 0);
    s.is_sky = (albedo_sample.a < 0.01);
    s.albedo = albedo_sample.rgb;

    s.normal = texelFetch(gbuffer_normal, pixel, 0).rgb * 2.0 - 1.0;

    uvec2 material = texelFetch(gbuffer_material, pixel, 0).xy;
    vec4 properties = unpackUnorm4x8(material.x);
    s.roughness = properties.r;
    s.metallic = properties.g;
    s.emissive = properties.b;
    s.water_material = material.x >> 24;
    s.water_depth = uintBitsToFloat(material.y);

    s.linear_depth = texelFetch(gbuffer_depth, pixel, 0).r;
    s.world_pos = gbuffer_world_pos(pixel, textureSize(gbuffer_depth, 0), s.linear_depth);

    return s;
}

/* Shadow and AO variant: depth, normal and world position only */
GBufferSample sample_gbuffer_shadow(
    ivec2 pixel,
    sampler2D gbuffer_depth,
    sampler2D gbuffer_normal
) {
    GBufferSample s;

    s.linear_depth = texelFetch(gbuffer_depth, pixel, 0).r;
    s.is_sky = (s.linear_depth > 1e9);

    s.normal = texelFetch(gbuffer_normal, pixel, 0).rgb * 2.0 - 1.0;
    s.world_pos = gbuffer_world_pos(pixel, textureSize(gbuffer_depth, 0), s.linear_depth);

    s.albedo = vec3(0.0);
    s.roughness = 0.0;
    s.metallic = 0.0;
    s.emissive = 0.0;
    s.water_depth = 0.0;
    s.water_material = 0u;

    return s;
}

#endif /* GBUFFER_SAMPLE_GLSL */
