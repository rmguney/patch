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

float get_material_flags(uint material_id) {
    return materials[material_id * MATERIAL_STRIDE + 1u].b;
}

bool is_material_transparent(uint material_id) {
    return get_material_flags(material_id) >= 16.0;
}

/* A transparent material is water whose colour is the light a white beam keeps after this many
 * metres of it (content/materials.h) */
const float WATER_COLOUR_DEPTH = 10.0;

/* Beer-Lambert absorption per metre of the water of a material. The floor keeps a colour
 * channel of 0 finite: it absorbs as one at the darkest 8-bit sRGB step above it would. */
vec3 water_absorption(uint material_id) {
    return -log(max(get_material_color(material_id), vec3(0.0003))) / WATER_COLOUR_DEPTH;
}

#endif /* MATERIALS_GLSL */
