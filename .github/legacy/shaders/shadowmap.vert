#version 450

#if defined(PATCH_VULKAN)
#define PUSH_CONSTANT layout(push_constant)
#define SET_BINDING(set_, binding_) set = set_, binding = binding_
#else
#define PUSH_CONSTANT
#define SET_BINDING(set_, binding_) binding = binding_
#endif

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

PUSH_CONSTANT uniform PushConstants {
    mat4 model;
    mat4 view;
    mat4 projection;
    vec4 color_alpha;
    vec4 params;
} pc;

layout(std140, SET_BINDING(0, 1)) uniform ShadowUniforms {
    mat4 light_view_proj;
    vec4 light_dir;
} u_shadow;

void main() {
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    gl_Position = u_shadow.light_view_proj * worldPos;
}
