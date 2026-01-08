#version 450

#if defined(PATCH_VULKAN)
#define PUSH_CONSTANT layout(push_constant)
#else
#define PUSH_CONSTANT
#endif

layout(location = 0) in vec2 vUV;
layout(location = 1) in float vHeight;

layout(location = 0) out vec4 outColor;

PUSH_CONSTANT uniform PushConstants {
    mat4 model;
    mat4 view;
    mat4 projection;
    vec4 color_alpha;
    vec4 params;
} pc;

void main() {
    vec2 uv = vUV;
    float height = max(vHeight, 0.0);
    
    vec2 lightDir2D = normalize(vec2(-0.65, 0.76));
    vec2 shadowOffset = -lightDir2D * height * 0.14;
    uv -= shadowOffset;
    
    float r = length(uv);
    
    float contactRadius = 0.22;
    float midRadius = 0.55;
    float outerRadius = 1.05;
    
    float heightScale = 1.0 / (1.0 + height * 0.4);
    contactRadius *= heightScale;
    midRadius *= mix(1.0, 1.3, min(height * 0.15, 0.4));
    outerRadius *= mix(1.0, 1.5, min(height * 0.12, 0.5));
    
    float contact = 1.0 - smoothstep(0.0, contactRadius, r);
    float mid = 1.0 - smoothstep(contactRadius, midRadius, r);
    float outer = 1.0 - smoothstep(midRadius, outerRadius, r);
    
    float contactFade = exp(-height * 3.5);
    float midFade = exp(-height * 0.8);
    float outerFade = exp(-height * 0.2);
    
    float shadow = contact * 0.7 * contactFade;
    shadow += mid * 0.5 * midFade;
    shadow += outer * 0.3 * outerFade;
    
    shadow = shadow * shadow * 1.2;
    
    float baseOpacity = 0.85;
    float finalOpacity = shadow * baseOpacity * pc.color_alpha.w;
    
    outColor = vec4(0.0, 0.0, 0.02, clamp(finalOpacity, 0.0, 0.9));
}
