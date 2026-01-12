#ifndef MATERIALS_GLSL
#define MATERIALS_GLSL

/*
 * materials.glsl - Shared material property accessors
 *
 * Materials are stored in a MaterialPalette uniform buffer with 2 vec4s per material:
 *   [id * 2 + 0] = vec4(r, g, b, emissive)
 *   [id * 2 + 1] = vec4(roughness, metallic, reserved, reserved)
 *
 * Include this file and define the materials uniform before using these functions.
 */

const uint MATERIAL_STRIDE = 2u;

vec3 get_material_color(uint material_id) {
    return materials[material_id * MATERIAL_STRIDE].rgb;
}

float get_material_emissive(uint material_id) {
    return materials[material_id * MATERIAL_STRIDE].a;
}

float get_material_roughness(uint material_id) {
    return materials[material_id * MATERIAL_STRIDE + 1u].r;
}

float get_material_metallic(uint material_id) {
    return materials[material_id * MATERIAL_STRIDE + 1u].g;
}

#endif /* MATERIALS_GLSL */
