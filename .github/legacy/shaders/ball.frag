
#version 450

#if defined(PATCH_VULKAN)
#define SET_BINDING(set_, binding_) set = set_, binding = binding_
#else
#define SET_BINDING(set_, binding_) binding = binding_
#endif

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragPosition;
layout(location = 2) in vec3 fragColor;
layout(location = 3) in float fragAlpha;
layout(location = 4) flat in float fragShadingMode;

layout(location = 0) out vec4 outColor;

layout(SET_BINDING(0, 0)) uniform sampler2DShadow u_shadow_map;

layout(std140, SET_BINDING(0, 1)) uniform ShadowUniforms {
    mat4 light_view_proj;
    vec4 light_dir;
} u_shadow;

float shadow_pcf(vec3 world_pos, vec3 normal, vec3 light_dir)
{
    vec3 N = normalize(normal);
    vec3 L = normalize(light_dir);
    float ndotl = max(dot(N, L), 0.0);

    float normal_bias = mix(0.02, 0.006, ndotl);
    vec3 biased_pos = world_pos + N * normal_bias;

    vec4 lp = u_shadow.light_view_proj * vec4(biased_pos, 1.0);
    vec3 proj = lp.xyz / max(lp.w, 1e-6);

    vec2 uv = proj.xy * 0.5 + 0.5;
    float z = proj.z;

    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || z < 0.0 || z > 1.0) {
        return 1.0;
    }

    float bias = max(0.0025 * (1.0 - ndotl), 0.00075);

    ivec2 sz = textureSize(u_shadow_map, 0);
    vec2 texel = 1.0 / vec2(max(sz.x, 1), max(sz.y, 1));

    float sum = 0.0;
    float wsum = 0.0;

    const int R = 2;
    for (int y = -R; y <= R; ++y) {
        for (int x = -R; x <= R; ++x) {
            float w = 1.0;
            vec2 o = vec2(float(x), float(y)) * texel;
            sum += w * texture(u_shadow_map, vec3(uv + o, z - bias));
            wsum += w;
        }
    }

    return sum / max(wsum, 1e-6);
}

void main() {
    vec3 N = normalize(fragNormal);
    vec3 albedo = fragColor;
    
    vec3 keyLight = normalize(vec3(-0.6, 0.9, 0.35));
    vec3 keyColor = vec3(1.05, 1.0, 0.96);
    float keyStrength = 1.25;
    
    vec3 fillLight = normalize(vec3(0.45, 0.5, -0.65));
    vec3 fillColor = vec3(0.7, 0.8, 1.0);
    float fillStrength = 0.45;
    
    vec3 backLight = normalize(vec3(-0.35, 0.28, 0.9));
    vec3 backColor = vec3(0.95, 0.9, 0.85);
    float backStrength = 0.25;
    
    float keyDot = max(dot(N, keyLight), 0.0);
    float fillDot = max(dot(N, fillLight), 0.0);
    float backDot = max(dot(N, backLight), 0.0);
    
    float wrapKey = (dot(N, keyLight) + 0.4) / 1.4;
    wrapKey = max(wrapKey, 0.0);
    
    float shadow = shadow_pcf(fragPosition, N, keyLight);

    vec3 diffuse = vec3(0.0);
    diffuse += albedo * keyColor * wrapKey * keyStrength * shadow;
    diffuse += albedo * fillColor * fillDot * fillStrength;
    diffuse += albedo * backColor * backDot * backStrength;
    
    float ambient = 0.4;
    vec3 skyAmbient = vec3(0.62, 0.74, 0.98) * (N.y * 0.5 + 0.5);
    vec3 groundAmbient = vec3(0.42, 0.36, 0.32) * (0.5 - N.y * 0.5);
    vec3 ambientLight = (skyAmbient + groundAmbient) * ambient;
    
    vec3 V = normalize(vec3(1.0, 1.0, 1.0));
    vec3 H = normalize(keyLight + V);
    float spec = pow(max(dot(N, H), 0.0), 32.0) * 0.15;
    vec3 specular = vec3(spec) * keyColor * keyDot * shadow;
    
    float floor_y = -2.5;
    float groundDist = fragPosition.y - floor_y;
    float ao = smoothstep(0.0, 1.5, groundDist) * 0.3 + 0.7;
    
    vec3 color = (albedo * ambientLight + diffuse + specular) * ao;

    if (fragShadingMode < 0.5) {
        float curvature = fwidth(N.x) + fwidth(N.y) + fwidth(N.z);
        float ao_curv = clamp(1.0 - curvature * 8.0, 0.6, 1.0);
        color *= ao_curv;
        
        float rim = 1.0 - max(dot(N, V), 0.0);
        rim = pow(rim, 4.0) * 0.08;
        color += rim * vec3(0.7, 0.8, 1.0);

        float edge = clamp(curvature * 2.0, 0.0, 1.0);
        float aa = 1.0 - edge * 0.35;
        float grain = fract(sin(dot(fragPosition.xy, vec2(12.9898, 78.233))) * 43758.5453);
        color = color * aa + (grain - 0.5) * 0.01;
    }
    
    color = pow(color, vec3(1.0/2.2));
    
    outColor = vec4(clamp(color, 0.0, 1.0), fragAlpha);
}
