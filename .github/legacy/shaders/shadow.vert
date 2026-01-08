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

layout(location = 0) out vec2 vUV;
layout(location = 1) out float vHeight;

void main() {
    float height = pc.params.x;
    float aspect_x = pc.params.y;
    float aspect_z = pc.params.z;
    
    float spread = 1.0 + height * 0.15;
    
    vec3 flatPos = vec3(inPosition.x * spread * aspect_x, 0.0, inPosition.y * spread * aspect_z);
    vec4 worldPos = pc.model * vec4(flatPos, 1.0);
    gl_Position = pc.projection * pc.view * worldPos;
    
    vUV = inPosition.xy * spread;
    vHeight = height;
}
