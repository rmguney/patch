#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec4 fragColor;

layout(location = 0) out vec4 outColor;

/* Glyph coverage (engine/render/ui_pass.cpp); untextured quads sample its solid texel */
layout(set = 0, binding = 0) uniform sampler2D glyph_atlas;

void main() {
    outColor = vec4(fragColor.rgb, fragColor.a * texture(glyph_atlas, fragUV).r);
}
