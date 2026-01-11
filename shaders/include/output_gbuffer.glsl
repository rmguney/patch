#ifndef OUTPUT_GBUFFER_GLSL
#define OUTPUT_GBUFFER_GLSL

#include "hdda_types.glsl"

layout(rgba8, set = 2, binding = 0) writeonly uniform image2D gbuffer_albedo;
layout(rgb10_a2, set = 2, binding = 1) writeonly uniform image2D gbuffer_normal;
layout(rgba8, set = 2, binding = 2) writeonly uniform image2D gbuffer_material;
layout(r32f, set = 2, binding = 3) writeonly uniform image2D gbuffer_depth;

layout(std140, set = 0, binding = 2) uniform MaterialPalette {
    vec4 material_palette[512];
};

vec3 get_material_color(uint material_id) {
    return material_palette[material_id * MATERIAL_STRIDE].rgb;
}

float get_material_emissive(uint material_id) {
    return material_palette[material_id * MATERIAL_STRIDE].a;
}

float get_material_roughness(uint material_id) {
    return material_palette[material_id * MATERIAL_STRIDE + 1u].r;
}

float get_material_metallic(uint material_id) {
    return material_palette[material_id * MATERIAL_STRIDE + 1u].g;
}

void emit_gbuffer_hit(ivec2 pixel, HitInfo hit, vec3 color_override) {
    vec3 color = (color_override.x >= 0.0) ? color_override : get_material_color(hit.material_id);
    float emissive = get_material_emissive(hit.material_id);
    float roughness = get_material_roughness(hit.material_id);
    float metallic = get_material_metallic(hit.material_id);

    imageStore(gbuffer_albedo, pixel, vec4(color, 1.0));
    imageStore(gbuffer_normal, pixel, vec4(hit.normal * 0.5 + 0.5, 1.0));
    imageStore(gbuffer_material, pixel, vec4(roughness, metallic, emissive, 0.0));
    imageStore(gbuffer_depth, pixel, vec4(hit.t, 0.0, 0.0, 0.0));
}

void emit_gbuffer(ivec2 pixel, HitInfo hit) {
    emit_gbuffer_hit(pixel, hit, vec3(-1.0));
}

void emit_gbuffer_sky(ivec2 pixel) {
    imageStore(gbuffer_albedo, pixel, vec4(0.0, 0.0, 0.0, 0.0));
    imageStore(gbuffer_normal, pixel, vec4(0.5, 0.5, 1.0, 0.0));
    imageStore(gbuffer_material, pixel, vec4(1.0, 0.0, 0.0, 0.0));
    imageStore(gbuffer_depth, pixel, vec4(1e10, 0.0, 0.0, 0.0));
}

#endif /* OUTPUT_GBUFFER_GLSL */
