#version 450

#if defined(PATCH_VULKAN)
#define PUSH_CONSTANT layout(push_constant)
#define SET_BINDING(set_, binding_) set = set_, binding = binding_
#else
#define PUSH_CONSTANT
#define SET_BINDING(set_, binding_) binding = binding_
#endif

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

PUSH_CONSTANT uniform Constants {
    mat4 view;
    mat4 projection;
    vec3 bounds_min;
    float voxel_size;
    vec3 bounds_max;
    float pad1;
    vec3 camera_pos;
    float pad2;
    ivec3 grid_size;
    float pad3;
} pc;

layout(std430, SET_BINDING(0, 0)) readonly buffer VoxelBuffer {
    uint voxels[];
};

uint get_voxel(ivec3 p) {
    if (p.x < 0 || p.x >= pc.grid_size.x ||
        p.y < 0 || p.y >= pc.grid_size.y ||
        p.z < 0 || p.z >= pc.grid_size.z) {
        return 0u;
    }
    int idx = p.x + p.y * pc.grid_size.x + p.z * pc.grid_size.x * pc.grid_size.y;
    return voxels[idx];
}

vec3 unpack_color(uint packed) {
    float r = float((packed >> 0) & 0xFFu) / 255.0;
    float g = float((packed >> 8) & 0xFFu) / 255.0;
    float b = float((packed >> 16) & 0xFFu) / 255.0;
    return vec3(r, g, b);
}

bool is_active(uint packed) {
    return ((packed >> 24) & 0xFFu) != 0u;
}

vec3 world_to_grid(vec3 world_pos) {
    return (world_pos - pc.bounds_min) / pc.voxel_size;
}

vec3 grid_to_world(ivec3 grid_pos) {
    return pc.bounds_min + (vec3(grid_pos) + 0.5) * pc.voxel_size;
}

vec2 intersect_aabb(vec3 ro, vec3 rd, vec3 box_min, vec3 box_max) {
    vec3 inv_rd = 1.0 / rd;
    vec3 t0 = (box_min - ro) * inv_rd;
    vec3 t1 = (box_max - ro) * inv_rd;
    vec3 tmin = min(t0, t1);
    vec3 tmax = max(t0, t1);
    float enter = max(max(tmin.x, tmin.y), tmin.z);
    float exit = min(min(tmax.x, tmax.y), tmax.z);
    return vec2(enter, exit);
}

struct HitInfo {
    bool hit;
    vec3 pos;
    vec3 normal;
    vec3 color;
    float t;
};

uint wang_hash(uint seed)
{
    seed = (seed ^ 61u) ^ (seed >> 16u);
    seed *= 9u;
    seed = seed ^ (seed >> 4u);
    seed *= 0x27d4eb2du;
    seed = seed ^ (seed >> 15u);
    return seed;
}

bool is_occupied(vec3 world_pos)
{
    ivec3 gp = ivec3(floor(world_to_grid(world_pos)));
    return is_active(get_voxel(gp));
}

float trace_shadow_ray(vec3 origin, vec3 dir) {
    vec3 pos = origin;
    if (abs(dir.x) < 1e-4) dir.x = sign(dir.x) * 1e-4;
    if (abs(dir.y) < 1e-4) dir.y = sign(dir.y) * 1e-4;
    if (abs(dir.z) < 1e-4) dir.z = sign(dir.z) * 1e-4;
    ivec3 map_pos = ivec3(floor(world_to_grid(pos)));
    vec3 delta_dist = abs(pc.voxel_size / dir);
    ivec3 step_dir = ivec3(sign(dir));
    vec3 grid_pos = world_to_grid(pos);
    vec3 side_dist = (sign(dir) * (vec3(map_pos) - grid_pos) + sign(dir) * 0.5 + 0.5) * delta_dist;
    
    float visibility = 1.0;
    float penumbra = 1.0;
    float light_size = 0.8;
    float t = 0.0;
    
    const int MAX_STEPS = 48;
    for (int i = 0; i < MAX_STEPS; i++) {
        if (map_pos.x < 0 || map_pos.x >= pc.grid_size.x ||
            map_pos.y < 0 || map_pos.y >= pc.grid_size.y ||
            map_pos.z < 0 || map_pos.z >= pc.grid_size.z) {
            break;
        }
        
        if (is_active(get_voxel(map_pos))) {
            float dist_to_occluder = t * pc.voxel_size;
            float soft = clamp(light_size * dist_to_occluder / pc.voxel_size, 0.0, 1.0);
            penumbra = min(penumbra, soft);
            visibility = 0.0;
            break;
        }
        
        bvec3 mask = lessThanEqual(side_dist.xyz, min(side_dist.yzx, side_dist.zxy));
        float dt = 0.0;
        if (mask.x) dt = side_dist.x;
        else if (mask.y) dt = side_dist.y;
        else dt = side_dist.z;
        t = dt;
        
        side_dist += vec3(mask) * delta_dist;
        map_pos += ivec3(mask) * step_dir;
    }
    
    return mix(0.15, 1.0, visibility * penumbra);
}

float hash1(vec2 p)
{
    uvec2 q = uvec2(floatBitsToUint(p.x), floatBitsToUint(p.y));
    uint h = q.x * 1664525u + 1013904223u;
    h ^= q.y * 22695477u + 1u;
    h = wang_hash(h);
    return float(h) / 4294967295.0;
}

vec2 hash2(vec2 p)
{
    float a = hash1(p);
    float b = hash1(p + vec2(17.0, 59.0));
    return vec2(a, b);
}

vec3 build_orthonormal(vec3 n)
{
    vec3 a = (abs(n.y) < 0.99) ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    return normalize(cross(a, n));
}

float trace_ao_ray(vec3 world_pos, vec3 dir, float max_dist)
{
    vec3 d = dir;
    float dl = length(d);
    if (dl < 1e-4) {
        return 1.0;
    }
    d *= 1.0 / dl;

    vec3 p = world_pos + d * 0.02;

    const int STEPS = 24;
    float step_len = max_dist / float(STEPS);
    for (int i = 0; i < STEPS; ++i) {
        p += d * step_len;
        if (is_occupied(p)) {
            return 0.0;
        }
    }
    return 1.0;
}

float traced_ao(vec3 world_pos, vec3 normal, vec2 noise)
{
    vec3 n = normalize(normal);
    vec3 t = build_orthonormal(n);
    vec3 b = normalize(cross(n, t));

    float ao = 0.0;
    const int RAYS = 4;
    for (int i = 0; i < RAYS; ++i) {
        vec2 u = fract(noise + vec2(float(i) * 0.37, float(i) * 0.73));
        float phi = 6.28318530718 * u.x;
        float cosTheta = sqrt(1.0 - u.y);
        float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
        vec3 hemi = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
        vec3 d = normalize(t * hemi.x + b * hemi.y + n * hemi.z);
        ao += trace_ao_ray(world_pos, d, 0.9);
    }
    return ao / float(RAYS);
}

float filtered_soft_shadow(vec3 world_pos, vec3 light_dir, vec2 noise)
{
    vec3 ld = normalize(light_dir);
    vec3 t = build_orthonormal(ld);
    vec3 b = normalize(cross(ld, t));

    float sum = 0.0;
    const int SAMPLES = 6;
    for (int i = 0; i < SAMPLES; ++i) {
        vec2 u = fract(noise + vec2(float(i) * 0.19, float(i) * 0.61));
        float r = sqrt(u.x);
        float ang = 6.28318530718 * u.y;
        vec2 disk = r * vec2(cos(ang), sin(ang));

        float cone = 0.02 + 0.10 * (float(i) / float(SAMPLES - 1));
        vec3 d = normalize(ld + t * disk.x * cone + b * disk.y * cone);
        sum += trace_shadow_ray(world_pos, d);
    }
    return sum / float(SAMPLES);
}

float compute_ao(vec3 p, vec3 n) {
    float ao = 1.0;
    vec3 grid_p = world_to_grid(p);
    ivec3 base = ivec3(floor(grid_p));

    const ivec3 offsets[6] = ivec3[6](
        ivec3(1, 0, 0), ivec3(-1, 0, 0),
        ivec3(0, 1, 0), ivec3(0, -1, 0),
        ivec3(0, 0, 1), ivec3(0, 0, -1)
    );
    
    for (int i = 0; i < 6; i++) {
        ivec3 neighbor = base + offsets[i];
        if (neighbor.x >= 0 && neighbor.x < pc.grid_size.x &&
            neighbor.y >= 0 && neighbor.y < pc.grid_size.y &&
            neighbor.z >= 0 && neighbor.z < pc.grid_size.z) {
            if (is_active(get_voxel(neighbor))) {
                float weight = max(dot(n, vec3(offsets[i])), 0.0);
                ao -= weight * 0.12;
            }
        }
    }
    
    const ivec3 corners[8] = ivec3[8](
        ivec3(-1,-1,-1), ivec3(1,-1,-1), ivec3(-1,1,-1), ivec3(1,1,-1),
        ivec3(-1,-1,1), ivec3(1,-1,1), ivec3(-1,1,1), ivec3(1,1,1)
    );
    for (int i = 0; i < 8; i++) {
        ivec3 neighbor = base + corners[i];
        if (neighbor.x >= 0 && neighbor.x < pc.grid_size.x &&
            neighbor.y >= 0 && neighbor.y < pc.grid_size.y &&
            neighbor.z >= 0 && neighbor.z < pc.grid_size.z) {
            if (is_active(get_voxel(neighbor))) {
                ao -= 0.04;
            }
        }
    }
    
    return clamp(ao, 0.3, 1.0);
}

HitInfo raymarch_voxels(vec3 ro, vec3 rd) {
    HitInfo info;
    info.hit = false;
    info.t = 1e10;
    
    vec2 box_hit = intersect_aabb(ro, rd, pc.bounds_min, pc.bounds_max);
    if (box_hit.x > box_hit.y || box_hit.y < 0.0) {
        return info;
    }
    
    float t_start = max(box_hit.x, 0.001);
    vec3 start_pos = ro + rd * t_start;
    vec3 grid_pos = world_to_grid(start_pos);
    
    ivec3 map_pos = ivec3(floor(grid_pos));
    vec3 delta_dist = abs(1.0 / rd);
    ivec3 step_dir = ivec3(sign(rd));
    
    vec3 side_dist = (sign(rd) * (vec3(map_pos) - grid_pos) + sign(rd) * 0.5 + 0.5) * delta_dist;
    
    const int MAX_STEPS = 256;
    
    for (int i = 0; i < MAX_STEPS; i++) {
        if (map_pos.x < 0 || map_pos.x >= pc.grid_size.x ||
            map_pos.y < 0 || map_pos.y >= pc.grid_size.y ||
            map_pos.z < 0 || map_pos.z >= pc.grid_size.z) {
            break;
        }
        
        uint voxel = get_voxel(map_pos);
        if (is_active(voxel)) {
            info.hit = true;
            info.color = unpack_color(voxel);
            
            vec3 voxel_min = pc.bounds_min + vec3(map_pos) * pc.voxel_size;
            vec3 voxel_max = voxel_min + pc.voxel_size;
            vec2 voxel_hit = intersect_aabb(ro, rd, voxel_min, voxel_max);
            info.t = voxel_hit.x;
            info.pos = ro + rd * info.t;
            
            vec3 voxel_center = voxel_min + pc.voxel_size * 0.5;
            vec3 local = info.pos - voxel_center;
            vec3 abs_local = abs(local);
            float max_comp = max(max(abs_local.x, abs_local.y), abs_local.z);
            
            if (abs_local.x >= max_comp - 0.001) {
                info.normal = vec3(sign(local.x), 0.0, 0.0);
            } else if (abs_local.y >= max_comp - 0.001) {
                info.normal = vec3(0.0, sign(local.y), 0.0);
            } else {
                info.normal = vec3(0.0, 0.0, sign(local.z));
            }
            
            return info;
        }
        
        bvec3 mask = lessThanEqual(side_dist.xyz, min(side_dist.yzx, side_dist.zxy));
        side_dist += vec3(mask) * delta_dist;
        map_pos += ivec3(mask) * step_dir;
    }
    
    return info;
}

void main() {
    mat4 inv_proj = inverse(pc.projection);
    mat4 inv_view = inverse(pc.view);
    
    vec2 ndc = in_uv * 2.0 - 1.0;
    
    vec4 ray_clip = vec4(ndc.x, -ndc.y, -1.0, 1.0);
    vec4 ray_view = inv_proj * ray_clip;
    ray_view.z = -1.0;
    ray_view.w = 0.0;
    
    vec3 ray_world = normalize((inv_view * ray_view).xyz);
    vec3 ray_origin = pc.camera_pos;
    
    HitInfo hit = raymarch_voxels(ray_origin, ray_world);
    
    if (!hit.hit) {
        out_color = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }
    
    vec3 N = hit.normal;
    vec3 p = hit.pos;
    vec3 V = normalize(vec3(1.0, 1.0, 1.0));
    vec3 albedo = hit.color;
    
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

    vec2 noise = hash2(p.xy * 32.0 + in_uv * 1024.0);
    float keyShadow = filtered_soft_shadow(p + N * pc.voxel_size * 0.6, keyLight, noise);
    float voxelAO = clamp(compute_ao(p, N) * traced_ao(p + N * pc.voxel_size * 0.2, N, noise), 0.0, 1.0);
    
    float wrapKey = (dot(N, keyLight) + 0.4) / 1.4;
    wrapKey = max(wrapKey, 0.0);
    
    vec3 diffuse = vec3(0.0);
    diffuse += albedo * keyColor * wrapKey * keyStrength * keyShadow;
    diffuse += albedo * fillColor * fillDot * fillStrength;
    diffuse += albedo * backColor * backDot * backStrength;
    
    float ambient = 0.4;
    vec3 skyAmbient = vec3(0.62, 0.74, 0.98) * (N.y * 0.5 + 0.5);
    vec3 groundAmbient = vec3(0.42, 0.36, 0.32) * (0.5 - N.y * 0.5);
    vec3 ambientLight = (skyAmbient + groundAmbient) * ambient * voxelAO;
    
    vec3 H = normalize(keyLight + V);
    float spec = pow(max(dot(N, H), 0.0), 32.0) * 0.15;
    vec3 specular = vec3(spec) * keyColor * keyDot * keyShadow;
    
    float floor_y = pc.bounds_min.y;
    float groundDist = p.y - floor_y;
    float groundAO = smoothstep(0.0, 1.5, groundDist) * 0.3 + 0.7;
    
    vec3 color = (albedo * ambientLight + diffuse + specular) * groundAO * voxelAO;

    float curvature = fwidth(N.x) + fwidth(N.y) + fwidth(N.z);
    float ao_curv = clamp(1.0 - curvature * 8.0, 0.6, 1.0);
    color *= ao_curv;
    
    float rim = 1.0 - max(dot(N, V), 0.0);
    rim = pow(rim, 4.0) * 0.08;
    color += rim * vec3(0.7, 0.8, 1.0);

    float edge = clamp(curvature * 2.0, 0.0, 1.0);
    float aa = 1.0 - edge * 0.35;
    float grain = fract(sin(dot(p.xy, vec2(12.9898, 78.233))) * 43758.5453);
    color = color * aa + (grain - 0.5) * 0.01;
    
    color = pow(color, vec3(1.0/2.2));
    
    out_color = vec4(clamp(color, 0.0, 1.0), 1.0);
}
