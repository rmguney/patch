#version 450

#if defined(PATCH_VULKAN)
#define PUSH_CONSTANT layout(push_constant)
#else
#define PUSH_CONSTANT
#endif

PUSH_CONSTANT uniform PushConstants {
    mat4 model;
    mat4 view;
    mat4 projection;
    vec4 color_alpha;
    vec4 params;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out float fragAlpha;

void main() {
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    gl_Position = pc.projection * pc.view * worldPos;
    fragColor = pc.color_alpha.xyz;
    fragAlpha = pc.color_alpha.w;
}
