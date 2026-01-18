#ifndef GBUFFER_SAMPLE_GLSL
#define GBUFFER_SAMPLE_GLSL

/* RAY REUSE CONTRACT: Downstream passes must sample G-buffer via these functions,
   never re-cast primary rays. Primary rays are cast once in raymarch_gbuffer.comp. */

struct GBufferSample {
    vec3 world_pos;
    vec3 normal;
    vec3 albedo;
    float linear_depth;
    float roughness;
    float metallic;
    float emissive;
    bool is_sky;
};

GBufferSample sample_gbuffer(
    vec2 uv,
    sampler2D gbuffer_albedo,
    sampler2D gbuffer_normal,
    sampler2D gbuffer_material,
    sampler2D gbuffer_depth,
    sampler2D gbuffer_world_pos
) {
    GBufferSample s;

    vec4 albedo_sample = texture(gbuffer_albedo, uv);
    s.is_sky = (albedo_sample.a < 0.01);
    s.albedo = albedo_sample.rgb;

    s.normal = texture(gbuffer_normal, uv).rgb * 2.0 - 1.0;

    vec4 material = texture(gbuffer_material, uv);
    s.roughness = material.r;
    s.metallic = material.g;
    s.emissive = material.b;

    s.linear_depth = texture(gbuffer_depth, uv).r;
    s.world_pos = texture(gbuffer_world_pos, uv).xyz;

    return s;
}

/* Shadow pass variant using texelFetch with integer pixel coordinates */
GBufferSample sample_gbuffer_shadow(
    ivec2 pixel,
    sampler2D gbuffer_depth,
    sampler2D gbuffer_normal,
    sampler2D gbuffer_world_pos
) {
    GBufferSample s;

    s.linear_depth = texelFetch(gbuffer_depth, pixel, 0).r;
    s.is_sky = (s.linear_depth > 1e9);

    s.normal = texelFetch(gbuffer_normal, pixel, 0).rgb * 2.0 - 1.0;
    s.world_pos = texelFetch(gbuffer_world_pos, pixel, 0).xyz;

    s.albedo = vec3(0.0);
    s.roughness = 0.0;
    s.metallic = 0.0;
    s.emissive = 0.0;

    return s;
}

#endif /* GBUFFER_SAMPLE_GLSL */
