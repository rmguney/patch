#version 450

#if defined(PATCH_VULKAN)
#define PUSH_CONSTANT layout(push_constant)
#define SET_BINDING(set_, binding_) set = set_, binding = binding_
#else
#define PUSH_CONSTANT
#define SET_BINDING(set_, binding_) binding = binding_
#endif

layout(location = 0) in vec2 in_uv;

layout(location = 0) out vec4 out_albedo;
layout(location = 1) out vec4 out_normal;
layout(location = 2) out vec4 out_material;
layout(location = 3) out float out_linear_depth;

PUSH_CONSTANT uniform Constants {
    mat4 inv_view;
    mat4 inv_projection;
    vec3 bounds_min;
    float voxel_size;
    vec3 bounds_max;
    float chunk_size;
    vec3 camera_pos;
    float pad1;
    ivec3 grid_size;
    int total_chunks;
    ivec3 chunks_dim;
    int frame_count;
    int rt_quality;
    int debug_mode;
    int is_orthographic;
    int max_steps;
    float near_plane;
    float far_plane;
    int reserved[6];
} pc;

layout(std430, SET_BINDING(0, 0)) readonly buffer VoxelBuffer {
    uint voxel_data[];
};

layout(std430, SET_BINDING(0, 1)) readonly buffer ChunkHeaders {
    uvec4 chunk_headers[];
};

layout(std140, SET_BINDING(0, 2)) uniform MaterialPalette {
    vec4 materials[512];
};

layout(std140, SET_BINDING(0, 3)) uniform TemporalUBO {
    mat4 prev_view_proj;
};

layout(SET_BINDING(0, 4)) uniform sampler2D depth_tex;

const int REGION_SIZE = 8;
const int CHUNK_SIZE = 32;
const int CHUNK_UINT_COUNT = 8192;
const int DEFAULT_MAX_STEPS = 512;

uint get_material(ivec3 p) {
    if (p.x < 0 || p.x >= pc.grid_size.x ||
        p.y < 0 || p.y >= pc.grid_size.y ||
        p.z < 0 || p.z >= pc.grid_size.z) {
        return 0u;
    }

    ivec3 chunk_pos = p / CHUNK_SIZE;
    int chunk_idx = chunk_pos.x + chunk_pos.y * pc.chunks_dim.x +
                    chunk_pos.z * pc.chunks_dim.x * pc.chunks_dim.y;

    ivec3 local = p - chunk_pos * CHUNK_SIZE;
    int local_idx = local.x + local.y * CHUNK_SIZE + local.z * CHUNK_SIZE * CHUNK_SIZE;

    int chunk_data_offset = chunk_idx * CHUNK_UINT_COUNT;
    int uint_idx = local_idx / 4;
    int byte_idx = local_idx % 4;

    uint packed = voxel_data[chunk_data_offset + uint_idx];
    return (packed >> (byte_idx * 8)) & 0xFFu;
}

bool chunk_has_any(int chunk_idx) {
    if (chunk_idx < 0 || chunk_idx >= pc.total_chunks) return false;
    return (chunk_headers[chunk_idx].z & 0xFFu) != 0u;
}

uvec2 chunk_get_level0(int chunk_idx) {
    return chunk_headers[chunk_idx].xy;
}

bool region_occupied(int chunk_idx, ivec3 region) {
    uvec2 level0 = chunk_get_level0(chunk_idx);
    int bit = region.x + region.y * 4 + region.z * 16;
    if (bit < 32) {
        return (level0.x & (1u << bit)) != 0u;
    } else {
        return (level0.y & (1u << (bit - 32))) != 0u;
    }
}

vec3 get_material_color(uint material_id) {
    return materials[material_id * 2u].rgb;
}

float get_material_emissive(uint material_id) {
    return materials[material_id * 2u].a;
}

float get_material_roughness(uint material_id) {
    return materials[material_id * 2u + 1u].r;
}

float get_material_metallic(uint material_id) {
    return materials[material_id * 2u + 1u].g;
}

bool is_solid(ivec3 p) {
    return get_material(p) != 0u;
}

vec3 world_to_grid(vec3 world_pos) {
    return (world_pos - pc.bounds_min) / pc.voxel_size;
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
    uint material_id;
    float emissive;
    float roughness;
    float metallic;
    bvec3 step_mask;
};

HitInfo raymarch_voxels(vec3 ro, vec3 rd) {
    HitInfo info;
    info.hit = false;
    info.t = 1e10;
    info.step_mask = bvec3(false, true, false);

    vec2 box_hit = intersect_aabb(ro, rd, pc.bounds_min, pc.bounds_max);
    if (box_hit.x > box_hit.y || box_hit.y < 0.0) {
        return info;
    }

    float t_start = max(box_hit.x, 0.001);
    vec3 start_pos = ro + rd * t_start;
    vec3 grid_pos = world_to_grid(start_pos);

    /* Clamp to valid grid range */
    grid_pos = clamp(grid_pos, vec3(0.0), vec3(pc.grid_size) - 0.001);

    ivec3 map_pos = ivec3(floor(grid_pos));
    vec3 delta_dist = abs(1.0 / rd);
    ivec3 step_dir = ivec3(sign(rd));

    /* Standard DDA side_dist initialization */
    vec3 side_dist = (sign(rd) * (vec3(map_pos) - grid_pos) + sign(rd) * 0.5 + 0.5) * delta_dist;

    ivec3 last_chunk = ivec3(-1000);
    ivec3 last_region = ivec3(-1000);
    bool chunk_empty = false;
    bool region_empty = false;
    int current_chunk_idx = -1;

    /* Compute initial entry face from AABB intersection for first voxel.
       If camera is inside bounds (box_hit.x < 0), use first DDA step direction. */
    bvec3 last_mask;
    if (box_hit.x > 0.0) {
        vec3 inv_rd = 1.0 / rd;
        vec3 t0 = (pc.bounds_min - ro) * inv_rd;
        vec3 t1 = (pc.bounds_max - ro) * inv_rd;
        vec3 tmin = min(t0, t1);
        float max_tmin = max(max(tmin.x, tmin.y), tmin.z);
        last_mask = bvec3(
            abs(tmin.x - max_tmin) < 0.0001,
            abs(tmin.y - max_tmin) < 0.0001,
            abs(tmin.z - max_tmin) < 0.0001
        );
    } else {
        /* Camera inside bounds - use first voxel face based on fractional position */
        vec3 frac_pos = fract(grid_pos);
        vec3 dist_to_face = mix(frac_pos, 1.0 - frac_pos, greaterThan(rd, vec3(0.0)));
        float min_dist = min(min(dist_to_face.x, dist_to_face.y), dist_to_face.z);
        last_mask = lessThanEqual(dist_to_face, vec3(min_dist + 0.0001));
    }

    for (int i = 0; i < (pc.max_steps > 0 ? pc.max_steps : DEFAULT_MAX_STEPS); i++) {
        if (map_pos.x < 0 || map_pos.x >= pc.grid_size.x ||
            map_pos.y < 0 || map_pos.y >= pc.grid_size.y ||
            map_pos.z < 0 || map_pos.z >= pc.grid_size.z) {
            break;
        }

        ivec3 current_chunk = map_pos / CHUNK_SIZE;
        if (current_chunk != last_chunk) {
            last_chunk = current_chunk;
            current_chunk_idx = current_chunk.x + current_chunk.y * pc.chunks_dim.x +
                               current_chunk.z * pc.chunks_dim.x * pc.chunks_dim.y;
            chunk_empty = !chunk_has_any(current_chunk_idx);
            last_region = ivec3(-1000);
        }

        if (chunk_empty) {
            bvec3 mask = lessThanEqual(side_dist.xyz, min(side_dist.yzx, side_dist.zxy));
            last_mask = mask;
            side_dist += vec3(mask) * delta_dist;
            map_pos += ivec3(mask) * step_dir;
            continue;
        }

        ivec3 local_pos = map_pos - current_chunk * CHUNK_SIZE;
        ivec3 current_region = local_pos / REGION_SIZE;
        if (current_region != last_region) {
            last_region = current_region;
            region_empty = !region_occupied(current_chunk_idx, current_region);
        }

        if (region_empty) {
            bvec3 mask = lessThanEqual(side_dist.xyz, min(side_dist.yzx, side_dist.zxy));
            last_mask = mask;
            side_dist += vec3(mask) * delta_dist;
            map_pos += ivec3(mask) * step_dir;
            continue;
        }

        uint mat = get_material(map_pos);
        if (mat != 0u) {
            info.hit = true;
            info.material_id = mat;
            info.color = get_material_color(mat);
            info.emissive = get_material_emissive(mat);
            info.roughness = get_material_roughness(mat);
            info.metallic = get_material_metallic(mat);
            info.step_mask = last_mask;

            vec3 voxel_min = pc.bounds_min + vec3(map_pos) * pc.voxel_size;
            vec3 voxel_max = voxel_min + pc.voxel_size;
            vec2 voxel_hit = intersect_aabb(ro, rd, voxel_min, voxel_max);
            info.t = voxel_hit.x;
            info.pos = ro + rd * info.t;

            /* Use DDA step mask for normal - much more robust than position comparison */
            info.normal = vec3(0.0);
            if (last_mask.x) {
                info.normal.x = -sign(rd.x);
            } else if (last_mask.y) {
                info.normal.y = -sign(rd.y);
            } else {
                info.normal.z = -sign(rd.z);
            }

            return info;
        }

        bvec3 mask = lessThanEqual(side_dist.xyz, min(side_dist.yzx, side_dist.zxy));
        last_mask = mask;
        side_dist += vec3(mask) * delta_dist;
        map_pos += ivec3(mask) * step_dir;
    }

    return info;
}

float linear_depth_to_ndc(float linear_depth, float near, float far) {
    return (far - near * far / linear_depth) / (far - near);
}

void main() {
    vec2 ndc = in_uv * 2.0 - 1.0;

    vec3 ray_origin;
    vec3 ray_world;

    if (pc.is_orthographic != 0) {
        /* Orthographic: parallel rays from near plane (Vulkan NDC z is [0,1]) */
        vec4 near_clip = vec4(ndc.x, ndc.y, 0.0, 1.0);
        vec4 far_clip = vec4(ndc.x, ndc.y, 1.0, 1.0);
        vec4 near_view = pc.inv_projection * near_clip;
        vec4 far_view = pc.inv_projection * far_clip;
        vec3 near_world = (pc.inv_view * vec4(near_view.xyz, 1.0)).xyz;
        vec3 far_world = (pc.inv_view * vec4(far_view.xyz, 1.0)).xyz;
        ray_origin = near_world;
        ray_world = normalize(far_world - near_world);
    } else {
        /* Perspective: rays from camera position (fast path, no clipping)
           Note: Projection matrix already has Y-flip, no -ndc.y needed */
        vec4 ray_clip = vec4(ndc.x, ndc.y, -1.0, 1.0);
        vec4 ray_view = pc.inv_projection * ray_clip;
        ray_view.z = -1.0;
        ray_view.w = 0.0;
        ray_world = normalize((pc.inv_view * ray_view).xyz);
        ray_origin = pc.camera_pos;
    }

    /* Debug mode 2: solid magenta test pattern - shader is executing */
    if (pc.debug_mode == 2) {
        out_albedo = vec4(1.0, 0.0, 1.0, 1.0);  /* Magenta */
        out_normal = vec4(0.5, 0.5, 1.0, 1.0);
        out_material = vec4(0.5, 0.0, 0.0, 0.0);
        out_linear_depth = 10.0;
        gl_FragDepth = 0.5;
        return;
    }

    /* Debug mode 1: visualize AABB intersection */
    if (pc.debug_mode == 1) {
        vec2 box_hit = intersect_aabb(ray_origin, ray_world, pc.bounds_min, pc.bounds_max);
        if (box_hit.x > box_hit.y || box_hit.y < 0.0) {
            /* Ray misses AABB - red */
            out_albedo = vec4(1.0, 0.0, 0.0, 1.0);
        } else {
            /* Ray hits AABB - green (check if any chunks have data) */
            bool any_active = false;
            for (int i = 0; i < pc.total_chunks && !any_active; i++) {
                if (chunk_has_any(i)) any_active = true;
            }
            if (any_active) {
                out_albedo = vec4(0.0, 1.0, 0.0, 1.0);  /* Green: AABB hit, chunks have data */
            } else {
                out_albedo = vec4(1.0, 1.0, 0.0, 1.0);  /* Yellow: AABB hit but no chunk data */
            }
        }
        out_normal = vec4(0.5, 0.5, 1.0, 1.0);
        out_material = vec4(0.5, 0.0, 0.0, 0.0);
        out_linear_depth = 10.0;
        gl_FragDepth = 0.5;
        return;
    }

    /* Debug mode 3: show entry point and grid coords */
    if (pc.debug_mode == 3) {
        vec2 box_hit = intersect_aabb(ray_origin, ray_world, pc.bounds_min, pc.bounds_max);
        if (box_hit.x > box_hit.y || box_hit.y < 0.0) {
            out_albedo = vec4(1.0, 0.0, 0.0, 1.0);
        } else {
            float t_start = max(box_hit.x, 0.001);
            vec3 start_pos = ray_origin + ray_world * t_start;
            vec3 grid_pos = world_to_grid(start_pos);
            /* Show grid Y coordinate as color - floor should be Y=0-5 */
            float y_norm = grid_pos.y / float(pc.grid_size.y);
            out_albedo = vec4(y_norm, 1.0 - y_norm, 0.0, 1.0);
        }
        out_normal = vec4(0.5, 0.5, 1.0, 1.0);
        out_material = vec4(0.5, 0.0, 0.0, 0.0);
        out_linear_depth = 10.0;
        gl_FragDepth = 0.5;
        return;
    }

    /* Debug mode 4: sample a specific voxel and show its material */
    if (pc.debug_mode == 4) {
        vec2 box_hit = intersect_aabb(ray_origin, ray_world, pc.bounds_min, pc.bounds_max);
        if (box_hit.x > box_hit.y || box_hit.y < 0.0) {
            out_albedo = vec4(0.5, 0.0, 0.0, 1.0);
        } else {
            float t_start = max(box_hit.x, 0.001);
            vec3 start_pos = ray_origin + ray_world * t_start;
            vec3 grid_pos = world_to_grid(start_pos);
            ivec3 voxel = ivec3(floor(grid_pos));
            uint mat = get_material(voxel);
            if (mat != 0u) {
                out_albedo = vec4(0.0, 1.0, 0.0, 1.0);  /* Green: found solid */
            } else {
                out_albedo = vec4(0.0, 0.0, 1.0, 1.0);  /* Blue: empty at entry */
            }
        }
        out_normal = vec4(0.5, 0.5, 1.0, 1.0);
        out_material = vec4(0.5, 0.0, 0.0, 0.0);
        out_linear_depth = 10.0;
        gl_FragDepth = 0.5;
        return;
    }

    /* Debug mode 5: comprehensive buffer diagnostic */
    if (pc.debug_mode == 5) {
        /* Test multiple locations:
           R = corner voxel (0,0,0) solid
           G = center voxel (64,0,64) solid
           B = chunk 0 has_any flag
           All three should be bright for a properly filled floor.
        */
        uint mat_corner = get_material(ivec3(0, 0, 0));
        uint mat_center = get_material(ivec3(pc.grid_size.x / 2, 0, pc.grid_size.z / 2));
        bool chunk0_has_any = chunk_has_any(0);

        out_albedo = vec4(
            (mat_corner != 0u) ? 1.0 : 0.0,   /* R: corner voxel solid */
            (mat_center != 0u) ? 1.0 : 0.0,   /* G: center voxel solid */
            chunk0_has_any ? 1.0 : 0.0,       /* B: chunk 0 has_any */
            1.0
        );
        out_normal = vec4(0.5, 0.5, 1.0, 1.0);
        out_material = vec4(0.5, 0.0, 0.0, 0.0);
        out_linear_depth = 10.0;
        gl_FragDepth = 0.5;
        return;
    }

    /* Debug mode 6: brute force raymarch (no hierarchical skip) */
    if (pc.debug_mode == 6) {
        vec2 box_hit = intersect_aabb(ray_origin, ray_world, pc.bounds_min, pc.bounds_max);
        if (box_hit.x > box_hit.y || box_hit.y < 0.0) {
            out_albedo = vec4(0.3, 0.0, 0.0, 1.0);
        } else {
            float t_start = max(box_hit.x, 0.001);
            vec3 start_pos = ray_origin + ray_world * t_start;
            vec3 grid_pos = world_to_grid(start_pos);
            grid_pos = clamp(grid_pos, vec3(0.0), vec3(pc.grid_size) - 0.001);
            ivec3 map_pos = ivec3(floor(grid_pos));
            vec3 delta_dist = abs(1.0 / ray_world);
            ivec3 step_dir = ivec3(sign(ray_world));
            vec3 side_dist = (sign(ray_world) * (vec3(map_pos) - grid_pos) + sign(ray_world) * 0.5 + 0.5) * delta_dist;

            bool found = false;
            for (int i = 0; i < (pc.max_steps > 0 ? pc.max_steps : DEFAULT_MAX_STEPS); i++) {
                if (map_pos.x < 0 || map_pos.x >= pc.grid_size.x ||
                    map_pos.y < 0 || map_pos.y >= pc.grid_size.y ||
                    map_pos.z < 0 || map_pos.z >= pc.grid_size.z) {
                    break;
                }
                uint mat = get_material(map_pos);
                if (mat != 0u) {
                    found = true;
                    break;
                }
                bvec3 mask = lessThanEqual(side_dist.xyz, min(side_dist.yzx, side_dist.zxy));
                side_dist += vec3(mask) * delta_dist;
                map_pos += ivec3(mask) * step_dir;
            }

            if (found) {
                out_albedo = vec4(0.0, 1.0, 0.0, 1.0);  /* Green: found voxel */
            } else {
                out_albedo = vec4(0.0, 0.0, 1.0, 1.0);  /* Blue: no voxel found */
            }
        }
        out_normal = vec4(0.5, 0.5, 1.0, 1.0);
        out_material = vec4(0.5, 0.0, 0.0, 0.0);
        out_linear_depth = 10.0;
        gl_FragDepth = 0.5;
        return;
    }

    /* Debug mode 7: show ray direction for debugging */
    if (pc.debug_mode == 7) {
        /* Visualize ray direction: R=X, G=Y, B=Z (mapped from -1..1 to 0..1) */
        out_albedo = vec4(ray_world * 0.5 + 0.5, 1.0);
        out_normal = vec4(0.5, 0.5, 1.0, 1.0);
        out_material = vec4(0.5, 0.0, 0.0, 0.0);
        out_linear_depth = 10.0;
        gl_FragDepth = 0.5;
        return;
    }

    /* Debug mode 8: collider visualization handled after hit (cyan tint) */

    HitInfo hit = raymarch_voxels(ray_origin, ray_world);

    if (!hit.hit) {
        discard;
    }

    vec3 final_color = hit.color;

    /* Debug mode 8: Collider visualization - cyan tint for terrain */
    if (pc.debug_mode == 8) {
        final_color = mix(final_color, vec3(0.2, 0.8, 1.0), 0.7);
    }

    out_albedo = vec4(final_color, 1.0);
    out_normal = vec4(hit.normal * 0.5 + 0.5, 1.0);
    out_material = vec4(hit.roughness, hit.metallic, hit.emissive, 0.0);
    out_linear_depth = hit.t;

    gl_FragDepth = clamp(linear_depth_to_ndc(hit.t, pc.near_plane, pc.far_plane), 0.0, 1.0);
}
