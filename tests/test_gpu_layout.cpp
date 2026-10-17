#include "engine/render/gpu_bvh.h"
#include "engine/render/gpu_volume.h"
#include "engine/render/raster_gpu_types.h"
#include "engine/render/voxel_bricks.h"
#include "engine/render/voxel_push_constants.h"
#include "engine/voxel/bvh.h"
#include "engine/voxel/chunk.h"

/* Include embedded shaders to verify link visibility */
#include "shaders_embedded.h"

#include "test_common.h"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

/* ========================================================================= */
/* Layout Tests - Verify GPU struct sizes match shader expectations          */
/* ========================================================================= */

TEST(gpu_chunk_header_size)
{
    ASSERT(sizeof(GPUChunkHeader) == 16);
    return 1;
}

TEST(gpu_chunk_header_matches_uvec4)
{
    /* GPUChunkHeader must match shader's uvec4 layout */
    ASSERT(sizeof(GPUChunkHeader) == 4 * sizeof(uint32_t));
    ASSERT(offsetof(GPUChunkHeader, level0_lo) == 0);
    ASSERT(offsetof(GPUChunkHeader, level0_hi) == 4);
    ASSERT(offsetof(GPUChunkHeader, packed) == 8);
    ASSERT(offsetof(GPUChunkHeader, pad) == 12);
    return 1;
}

TEST(gpu_material_color_size)
{
    ASSERT(sizeof(GPUMaterialColor) == 32);
    return 1;
}

TEST(gpu_material_color_layout)
{
    /* Two vec4s: (r,g,b,emissive) and (roughness,metallic,flags,pad) */
    ASSERT(offsetof(GPUMaterialColor, r) == 0);
    ASSERT(offsetof(GPUMaterialColor, g) == 4);
    ASSERT(offsetof(GPUMaterialColor, b) == 8);
    ASSERT(offsetof(GPUMaterialColor, emissive) == 12);
    ASSERT(offsetof(GPUMaterialColor, roughness) == 16);
    ASSERT(offsetof(GPUMaterialColor, metallic) == 20);
    ASSERT(offsetof(GPUMaterialColor, flags) == 24);
    ASSERT(offsetof(GPUMaterialColor, pad) == 28);
    return 1;
}

TEST(gpu_material_palette_size)
{
    ASSERT(sizeof(GPUMaterialPalette) == 8192);
    ASSERT(sizeof(GPUMaterialPalette) == GPU_MATERIAL_PALETTE_SIZE * sizeof(GPUMaterialColor));
    return 1;
}

TEST(voxel_push_constants_size)
{
    ASSERT(sizeof(patch::VoxelPushConstants) == 232);
    return 1;
}

TEST(voxel_push_constants_layout)
{
    using namespace patch;
    ASSERT(offsetof(VoxelPushConstants, inv_view) == 0);
    ASSERT(offsetof(VoxelPushConstants, inv_projection) == 64);
    ASSERT(offsetof(VoxelPushConstants, bounds_min) == 128);
    ASSERT(offsetof(VoxelPushConstants, voxel_size) == 140);
    ASSERT(offsetof(VoxelPushConstants, bounds_max) == 144);
    ASSERT(offsetof(VoxelPushConstants, max_steps) == 156);
    ASSERT(offsetof(VoxelPushConstants, camera_pos) == 160);
    ASSERT(offsetof(VoxelPushConstants, history_valid) == 172);
    ASSERT(offsetof(VoxelPushConstants, grid_size) == 176);
    ASSERT(offsetof(VoxelPushConstants, total_chunks) == 188);
    ASSERT(offsetof(VoxelPushConstants, chunks_dim) == 192);
    ASSERT(offsetof(VoxelPushConstants, frame_count) == 204);
    ASSERT(offsetof(VoxelPushConstants, debug_mode) == 208);
    ASSERT(offsetof(VoxelPushConstants, far_plane) == 212);
    ASSERT(offsetof(VoxelPushConstants, object_count) == 216);
    ASSERT(offsetof(VoxelPushConstants, shadow_quality) == 220);
    ASSERT(offsetof(VoxelPushConstants, shadow_contact) == 224);
    ASSERT(offsetof(VoxelPushConstants, ao_quality) == 228);
    return 1;
}

TEST(voxel_temporal_ubo_size)
{
    ASSERT(sizeof(patch::VoxelTemporalUBO) == 96);
    return 1;
}

TEST(voxel_temporal_ubo_layout)
{
    /* std140: a mat4 and a vec4 sit at the same offsets as in std430 */
    using namespace patch;
    ASSERT(offsetof(VoxelTemporalUBO, prev_view_proj) == 0);
    ASSERT(offsetof(VoxelTemporalUBO, prev_camera_pos) == 64);
    ASSERT(offsetof(VoxelTemporalUBO, jitter_uv) == 80);
    ASSERT(offsetof(VoxelTemporalUBO, inv_render_size) == 88);
    return 1;
}

TEST(scene_lighting_ubo_size)
{
    ASSERT(sizeof(patch::SceneLightingUBO) == 144);
    return 1;
}

TEST(scene_lighting_ubo_layout)
{
    using namespace patch;
    ASSERT(offsetof(SceneLightingUBO, sun_dir) == 0);
    ASSERT(offsetof(SceneLightingUBO, sun_color) == 16);
    ASSERT(offsetof(SceneLightingUBO, fill_dir) == 32);
    ASSERT(offsetof(SceneLightingUBO, fill_color) == 48);
    ASSERT(offsetof(SceneLightingUBO, back_dir) == 64);
    ASSERT(offsetof(SceneLightingUBO, back_color) == 80);
    ASSERT(offsetof(SceneLightingUBO, sky_ambient) == 96);
    ASSERT(offsetof(SceneLightingUBO, ground_ambient) == 112);
    ASSERT(offsetof(SceneLightingUBO, sky_color) == 128);
    return 1;
}

/* ========================================================================= */
/* CPU structs against the GLSL declarations (read from shaders/)           */
/* ========================================================================= */

/* A shader source file, whole; false if it cannot be read or does not fit */
static bool read_shader_source(const char *relative_path, char *out, size_t capacity)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", PATCH_SHADER_DIR, relative_path);
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    size_t length = fread(out, 1, capacity - 1, f);
    bool whole = feof(f) != 0;
    fclose(f);
    out[length] = '\0';
    return whole;
}

struct GlslMember
{
    char type[16];
    char name[32];
    int count; /* array length, 1 for a plain member */
};

/* The text with block comments blanked out */
static void strip_comments(char *text)
{
    for (char *open = strstr(text, "/*"); open; open = strstr(open, "/*"))
    {
        char *close = strstr(open + 2, "*/");
        char *end = close ? close + 2 : open + strlen(open);
        memset(open, ' ', (size_t)(end - open));
    }
}

/* Members of the first `<header> { type member; type array[N]; ... }` (a struct, or a block
 * such as "uniform Constants") in declaration order; -1 if absent */
static int parse_glsl_members(const char *source, const char *header, GlslMember *out, int max_members)
{
    const char *start = strstr(source, header);
    if (!start)
        return -1;
    const char *open = strchr(start, '{');
    const char *close = open ? strchr(open, '}') : nullptr;
    if (!close)
        return -1;

    char body[2048];
    size_t length = (size_t)(close - open - 1);
    if (length >= sizeof(body))
        return -1;
    memcpy(body, open + 1, length);
    body[length] = '\0';
    strip_comments(body);

    int count = 0;
    for (char *decl = body; count < max_members;)
    {
        char *semicolon = strchr(decl, ';');
        if (!semicolon)
            break;
        *semicolon = '\0';
        if (sscanf(decl, " %15s %31s", out[count].type, out[count].name) == 2)
        {
            out[count].count = 1;
            if (char *bracket = strchr(out[count].name, '['))
            {
                out[count].count = atoi(bracket + 1);
                *bracket = '\0';
            }
            count++;
        }
        decl = semicolon + 1;
    }
    return count;
}

static int parse_glsl_struct(const char *source, const char *name, GlslMember *out, int max_members)
{
    char header[64];
    snprintf(header, sizeof(header), "struct %s", name);
    return parse_glsl_members(source, header, out, max_members);
}

/* Value of `#define <name> <integer>`; -1 if absent */
static long glsl_define(const char *source, const char *name)
{
    char directive[64];
    snprintf(directive, sizeof(directive), "#define %s ", name);
    const char *found = strstr(source, directive);
    return found ? strtol(found + strlen(directive), nullptr, 10) : -1;
}

/* std430 size and alignment of a scalar or vector type */
static bool std430_layout(const char *type, size_t *size, size_t *align)
{
    static const struct
    {
        const char *names[3];
        size_t size, align;
    } TYPES[] = {
        {{"float", "int", "uint"}, 4, 4},
        {{"vec2", "ivec2", "uvec2"}, 8, 8},
        {{"vec3", "ivec3", "uvec3"}, 12, 16},
        {{"vec4", "ivec4", "uvec4"}, 16, 16},
        {{"mat4", nullptr, nullptr}, 64, 16},
    };
    for (const auto &t : TYPES)
    {
        for (const char *n : t.names)
        {
            if (n && strcmp(type, n) == 0)
            {
                *size = t.size;
                *align = t.align;
                return true;
            }
        }
    }
    return false;
}

TEST(bvh_node_matches_glsl)
{
    /* The CPU BVH is copied byte for byte into the buffer bvh.glsl reads */
    static char source[8192];
    ASSERT(read_shader_source("include/bvh.glsl", source, sizeof(source)));

    GlslMember members[8];
    ASSERT(parse_glsl_struct(source, "BVHNode", members, 8) == 4);

    static const char *const names[4] = {"aabb_min", "left_first", "aabb_max", "count"};
    const size_t cpu_offsets[4] = {offsetof(BVHNode, aabb_min), offsetof(BVHNode, left_first),
                                   offsetof(BVHNode, aabb_max), offsetof(BVHNode, count)};
    const size_t gpu_offsets[4] = {offsetof(patch::GPUBVHNode, aabb_min), offsetof(patch::GPUBVHNode, left_first),
                                   offsetof(patch::GPUBVHNode, aabb_max), offsetof(patch::GPUBVHNode, count)};
    size_t offset = 0, struct_align = 0;
    for (int i = 0; i < 4; i++)
    {
        size_t size = 0, align = 0;
        ASSERT(std430_layout(members[i].type, &size, &align));
        offset = (offset + align - 1) / align * align;
        ASSERT(strcmp(members[i].name, names[i]) == 0);
        ASSERT(cpu_offsets[i] == offset);
        ASSERT(gpu_offsets[i] == offset);
        offset += size;
        struct_align = align > struct_align ? align : struct_align;
    }
    /* An array element of a struct takes its size rounded up to its alignment */
    size_t stride = (offset + struct_align - 1) / struct_align * struct_align;
    ASSERT(sizeof(BVHNode) == stride);
    ASSERT(sizeof(patch::GPUBVHNode) == stride);

    ASSERT(glsl_define(source, "BVH_MAX_NODES") == BVH_MAX_NODES);
    ASSERT(glsl_define(source, "BVH_MAX_OBJECTS") == BVH_MAX_OBJECTS);
    /* The traversal stack holds the deepest path the CPU builder makes (bvh.c sizes its own the same) */
    ASSERT(glsl_define(source, "BVH_STACK_SIZE") == BVH_MAX_DEPTH + 1);
    ASSERT(BVH_MAX_NODES == patch::GPU_BVH_MAX_NODES);
    ASSERT(BVH_MAX_OBJECTS == patch::GPU_BVH_MAX_OBJECTS);
    return 1;
}

/* std430 offsets of the members, the array stride of the struct and where its last member ends;
 * false on an unknown type or no members. Arrays here are of scalars and vectors: elements take
 * their own size (vec3 rounded to 16). */
static bool std430_struct_layout(const GlslMember *members, int count, size_t *offsets, size_t *stride,
                                 size_t *end = nullptr)
{
    if (count <= 0)
        return false;
    size_t offset = 0, struct_align = 0;
    for (int i = 0; i < count; i++)
    {
        size_t size = 0, align = 0;
        if (!std430_layout(members[i].type, &size, &align))
            return false;
        if (members[i].count > 1)
            size = (size + align - 1) / align * align * (size_t)members[i].count;
        offset = (offset + align - 1) / align * align;
        offsets[i] = offset;
        offset += size;
        struct_align = align > struct_align ? align : struct_align;
    }
    *stride = (offset + struct_align - 1) / struct_align * struct_align;
    if (end)
        *end = offset;
    return true;
}

/* A CPU member as the GLSL declares it: its name there, offset and size on the CPU */
struct CpuMember
{
    const char *name;
    size_t offset, size;
};

/*
 * The GLSL members are the CPU ones: same order and names, at the CPU offsets, each taking the
 * bytes up to the next (the CPU size), the last up to cpu_size. A struct read as an array
 * element ends at its std430 stride, a push constant block where its last member ends.
 */
static int glsl_members_match(const GlslMember *members, int count, const CpuMember *cpu, int cpu_count,
                              size_t cpu_size, bool array_element)
{
    ASSERT(count == cpu_count);
    size_t offsets[32], stride = 0, end = 0;
    ASSERT(count <= 32 && std430_struct_layout(members, count, offsets, &stride, &end));
    const size_t total = array_element ? stride : end;
    for (int i = 0; i < count; i++)
    {
        ASSERT(strcmp(members[i].name, cpu[i].name) == 0);
        ASSERT(offsets[i] == cpu[i].offset);
        const size_t next = i + 1 < count ? offsets[i + 1] : total;
        ASSERT(next - offsets[i] == cpu[i].size);
    }
    ASSERT(total == cpu_size);
    return 1;
}

/* The push_constant block `uniform Constants` of a shader file against the CPU members */
static int push_constants_match(const char *shader, const CpuMember *cpu, int cpu_count, size_t cpu_size)
{
    static char source[16384];
    ASSERT(read_shader_source(shader, source, sizeof(source)));
    strip_comments(source);
    GlslMember members[32];
    const int count = parse_glsl_members(source, "uniform Constants", members, 32);
    ASSERT(glsl_members_match(members, count, cpu, cpu_count, cpu_size, false));
    return 1;
}

TEST(voxel_object_gpu_matches_glsl)
{
    /* The metadata buffer the voxel object shaders read, written member by member on the CPU */
    static char source[8192];
    ASSERT(read_shader_source("include/voxobj_types.glsl", source, sizeof(source)));

    GlslMember members[16];
    const int count = parse_glsl_struct(source, "VoxelObjectGPU", members, 16);

    using patch::VoxelObjectGPU;
    static const CpuMember CPU[] = {
        {"world_to_local", offsetof(VoxelObjectGPU, world_to_local), sizeof(VoxelObjectGPU::world_to_local)},
        {"local_to_world", offsetof(VoxelObjectGPU, local_to_world), sizeof(VoxelObjectGPU::local_to_world)},
        {"bounds_min", offsetof(VoxelObjectGPU, bounds_min), sizeof(VoxelObjectGPU::bounds_min)},
        {"bounds_max", offsetof(VoxelObjectGPU, bounds_max), sizeof(VoxelObjectGPU::bounds_max)},
        {"position", offsetof(VoxelObjectGPU, position), sizeof(VoxelObjectGPU::position)},
        {"brick_mask", offsetof(VoxelObjectGPU, brick_mask), sizeof(VoxelObjectGPU::brick_mask)},
        {"brick_offset", offsetof(VoxelObjectGPU, brick_offset), sizeof(VoxelObjectGPU::brick_offset)},
        {"pad", offsetof(VoxelObjectGPU, pad), sizeof(VoxelObjectGPU::pad)},
        {"brick_table", offsetof(VoxelObjectGPU, brick_table), sizeof(VoxelObjectGPU::brick_table)},
        {"prev_local_to_world", offsetof(VoxelObjectGPU, prev_local_to_world), sizeof(VoxelObjectGPU::prev_local_to_world)},
    };
    ASSERT(glsl_members_match(members, count, CPU, (int)(sizeof(CPU) / sizeof(CPU[0])), sizeof(VoxelObjectGPU), true));

    /* The brick geometry the traversal and the atlas packing share */
    ASSERT(glsl_define(source, "VOBJ_BRICK_SIZE") == VOBJ_BRICK_SIZE);
    ASSERT(glsl_define(source, "VOBJ_BRICKS_PER_AXIS") == VOBJ_BRICKS_PER_AXIS);
    ASSERT(glsl_define(source, "BRICK_ATLAS_BRICKS_PER_AXIS") == (long)patch::BRICK_ATLAS_BRICKS_PER_AXIS);
    return 1;
}

TEST(particle_gpu_matches_glsl)
{
    /* The instance buffer of particle.vert and particle.frag, written member by member on the CPU */
    using patch::ParticleGPU;
    static const CpuMember CPU[] = {
        {"position_radius", offsetof(ParticleGPU, position), sizeof(ParticleGPU::position) + sizeof(ParticleGPU::radius)},
        {"color_flags", offsetof(ParticleGPU, color), sizeof(ParticleGPU::color) + sizeof(ParticleGPU::flags)},
        {"prev_position", offsetof(ParticleGPU, prev_position), sizeof(ParticleGPU::prev_position)},
    };
    for (const char *shader : {"particle.vert", "particle.frag"})
    {
        static char source[16384];
        ASSERT(read_shader_source(shader, source, sizeof(source)));
        GlslMember members[8];
        const int count = parse_glsl_struct(source, "ParticleGPU", members, 8);
        ASSERT(glsl_members_match(members, count, CPU, (int)(sizeof(CPU) / sizeof(CPU[0])), sizeof(ParticleGPU), true));
    }
    return 1;
}

TEST(raster_push_constants_match_glsl)
{
    using patch::ParticlePushConstants;
    using patch::VoxelObjectPushConstants;
    static const CpuMember OBJECTS[] = {
        {"view_proj", offsetof(VoxelObjectPushConstants, view_proj), sizeof(VoxelObjectPushConstants::view_proj)},
        {"camera_pos", offsetof(VoxelObjectPushConstants, camera_pos), sizeof(VoxelObjectPushConstants::camera_pos)},
        {"near_plane", offsetof(VoxelObjectPushConstants, near_plane), sizeof(VoxelObjectPushConstants::near_plane)},
        {"far_plane", offsetof(VoxelObjectPushConstants, far_plane), sizeof(VoxelObjectPushConstants::far_plane)},
        {"debug_mode", offsetof(VoxelObjectPushConstants, debug_mode), sizeof(VoxelObjectPushConstants::debug_mode)},
        {"near_clip_radius", offsetof(VoxelObjectPushConstants, near_clip_radius), sizeof(VoxelObjectPushConstants::near_clip_radius)},
        {"camera_inside", offsetof(VoxelObjectPushConstants, camera_inside), sizeof(VoxelObjectPushConstants::camera_inside)},
    };
    static const CpuMember PARTICLES[] = {
        {"view_proj", offsetof(ParticlePushConstants, view_proj), sizeof(ParticlePushConstants::view_proj)},
        {"camera_pos", offsetof(ParticlePushConstants, camera_pos), sizeof(ParticlePushConstants::camera_pos)},
        {"near_plane", offsetof(ParticlePushConstants, near_plane), sizeof(ParticlePushConstants::near_plane)},
        {"far_plane", offsetof(ParticlePushConstants, far_plane), sizeof(ParticlePushConstants::far_plane)},
        {"camera_inside", offsetof(ParticlePushConstants, camera_inside), sizeof(ParticlePushConstants::camera_inside)},
    };
    const int object_count = (int)(sizeof(OBJECTS) / sizeof(OBJECTS[0]));
    const int particle_count = (int)(sizeof(PARTICLES) / sizeof(PARTICLES[0]));
    ASSERT(push_constants_match("voxel_object.vert", OBJECTS, object_count, sizeof(VoxelObjectPushConstants)));
    ASSERT(push_constants_match("voxel_object.frag", OBJECTS, object_count, sizeof(VoxelObjectPushConstants)));
    ASSERT(push_constants_match("particle.vert", PARTICLES, particle_count, sizeof(ParticlePushConstants)));
    ASSERT(push_constants_match("particle.frag", PARTICLES, particle_count, sizeof(ParticlePushConstants)));
    return 1;
}

TEST(temporal_ubo_matches_glsl)
{
    /* Every shader that reads the temporal UBO declares all of it (std140 lays out these
     * members as std430 does) */
    using patch::VoxelTemporalUBO;
    static const CpuMember CPU[] = {
        {"prev_view_proj", offsetof(VoxelTemporalUBO, prev_view_proj), sizeof(VoxelTemporalUBO::prev_view_proj)},
        {"prev_camera_pos", offsetof(VoxelTemporalUBO, prev_camera_pos), sizeof(VoxelTemporalUBO::prev_camera_pos)},
        {"jitter_uv", offsetof(VoxelTemporalUBO, jitter_uv), sizeof(VoxelTemporalUBO::jitter_uv)},
        {"inv_render_size", offsetof(VoxelTemporalUBO, inv_render_size), sizeof(VoxelTemporalUBO::inv_render_size)},
    };
    static const char *const SHADERS[] = {"raymarch_gbuffer.comp", "voxel_object.frag", "particle.frag", "temporal_ao.comp"};
    static char source[32768];
    for (const char *shader : SHADERS)
    {
        ASSERT(read_shader_source(shader, source, sizeof(source)));
        strip_comments(source);
        GlslMember members[8];
        const int count = parse_glsl_members(source, "uniform TemporalUBO", members, 8);
        ASSERT(glsl_members_match(members, count, CPU, (int)(sizeof(CPU) / sizeof(CPU[0])), sizeof(VoxelTemporalUBO), false));
    }
    return 1;
}

TEST(voxel_push_constants_match_glsl)
{
    /* The block every compute pass and the lighting / denoise passes share */
    static char source[8192];
    ASSERT(read_shader_source("include/voxel_push_constants.glsl", source, sizeof(source)));
    strip_comments(source);
    GlslMember members[32];
    const int count = parse_glsl_members(source, "uniform VoxelPushConstants", members, 32);

    using patch::VoxelPushConstants;
    static const CpuMember CPU[] = {
        {"inv_view", offsetof(VoxelPushConstants, inv_view), sizeof(VoxelPushConstants::inv_view)},
        {"inv_projection", offsetof(VoxelPushConstants, inv_projection), sizeof(VoxelPushConstants::inv_projection)},
        {"bounds_min", offsetof(VoxelPushConstants, bounds_min), sizeof(VoxelPushConstants::bounds_min)},
        {"voxel_size", offsetof(VoxelPushConstants, voxel_size), sizeof(VoxelPushConstants::voxel_size)},
        {"bounds_max", offsetof(VoxelPushConstants, bounds_max), sizeof(VoxelPushConstants::bounds_max)},
        {"max_steps", offsetof(VoxelPushConstants, max_steps), sizeof(VoxelPushConstants::max_steps)},
        {"camera_pos", offsetof(VoxelPushConstants, camera_pos), sizeof(VoxelPushConstants::camera_pos)},
        {"history_valid", offsetof(VoxelPushConstants, history_valid), sizeof(VoxelPushConstants::history_valid)},
        {"grid_size", offsetof(VoxelPushConstants, grid_size), sizeof(VoxelPushConstants::grid_size)},
        {"total_chunks", offsetof(VoxelPushConstants, total_chunks), sizeof(VoxelPushConstants::total_chunks)},
        {"chunks_dim", offsetof(VoxelPushConstants, chunks_dim), sizeof(VoxelPushConstants::chunks_dim)},
        {"frame_count", offsetof(VoxelPushConstants, frame_count), sizeof(VoxelPushConstants::frame_count)},
        {"debug_mode", offsetof(VoxelPushConstants, debug_mode), sizeof(VoxelPushConstants::debug_mode)},
        {"far_plane", offsetof(VoxelPushConstants, far_plane), sizeof(VoxelPushConstants::far_plane)},
        {"object_count", offsetof(VoxelPushConstants, object_count), sizeof(VoxelPushConstants::object_count)},
        {"shadow_quality", offsetof(VoxelPushConstants, shadow_quality), sizeof(VoxelPushConstants::shadow_quality)},
        {"shadow_contact", offsetof(VoxelPushConstants, shadow_contact), sizeof(VoxelPushConstants::shadow_contact)},
        {"ao_quality", offsetof(VoxelPushConstants, ao_quality), sizeof(VoxelPushConstants::ao_quality)},
    };
    ASSERT(glsl_members_match(members, count, CPU, (int)(sizeof(CPU) / sizeof(CPU[0])), sizeof(VoxelPushConstants), false));
    return 1;
}

/* ========================================================================= */
/* Shader Embedding Tests - Verify shaders are linked and have valid sizes   */
/* ========================================================================= */

TEST(shader_ui_frag_embedded)
{
    using namespace patch::shaders;
    size_t size = sizeof(k_shader_ui_frag_spv);
    ASSERT(size > 0);
    ASSERT(size % 4 == 0);
    ASSERT(k_shader_ui_frag_spv[0] == 0x07230203);
    return 1;
}

TEST(shader_ui_vert_embedded)
{
    using namespace patch::shaders;
    size_t size = sizeof(k_shader_ui_vert_spv);
    ASSERT(size > 0);
    ASSERT(size % 4 == 0);
    ASSERT(k_shader_ui_vert_spv[0] == 0x07230203);
    return 1;
}

TEST(shader_voxel_vert_embedded)
{
    using namespace patch::shaders;
    size_t size = sizeof(k_shader_voxel_vert_spv);
    ASSERT(size > 0);
    ASSERT(size % 4 == 0);
    ASSERT(k_shader_voxel_vert_spv[0] == 0x07230203);
    return 1;
}

/* ========================================================================= */
/* ChunkOccupancy → GPU Header Packing Tests                                 */
/* ========================================================================= */

TEST(chunk_header_empty_chunk)
{
    Chunk chunk;
    memset(&chunk, 0, sizeof(chunk));
    chunk.occupancy.has_any = 0;
    chunk.occupancy.level0 = 0;
    chunk.occupancy.level1 = 0;
    chunk.occupancy.solid_count = 0;

    GPUChunkHeader header = gpu_chunk_header_from_chunk(&chunk);

    ASSERT(header.level0_lo == 0);
    ASSERT(header.level0_hi == 0);
    ASSERT((header.packed & 0xFF) == 0);        /* has_any = 0 */
    ASSERT(((header.packed >> 8) & 0xFF) == 0); /* level1 = 0 */
    ASSERT((header.packed >> 16) == 0);         /* solid_count = 0 */
    return 1;
}

TEST(chunk_header_full_occupancy)
{
    Chunk chunk;
    memset(&chunk, 0, sizeof(chunk));
    chunk.occupancy.has_any = 1;
    chunk.occupancy.level0 = 0xFFFFFFFFFFFFFFFFULL;
    chunk.occupancy.level1 = 0xFF;
    chunk.occupancy.solid_count = CHUNK_VOXEL_COUNT;

    GPUChunkHeader header = gpu_chunk_header_from_chunk(&chunk);

    ASSERT(header.level0_lo == 0xFFFFFFFF);
    ASSERT(header.level0_hi == 0xFFFFFFFF);
    ASSERT((header.packed & 0xFF) == 1);                /* has_any = 1 */
    ASSERT(((header.packed >> 8) & 0xFF) == 0xFF);      /* level1 = 0xFF */
    ASSERT((header.packed >> 16) == CHUNK_VOXEL_COUNT); /* solid_count */
    return 1;
}

TEST(chunk_header_partial_occupancy)
{
    Chunk chunk;
    memset(&chunk, 0, sizeof(chunk));
    chunk.occupancy.has_any = 1;
    chunk.occupancy.level0 = 0x123456789ABCDEF0ULL;
    chunk.occupancy.level1 = 0x55;
    chunk.occupancy.solid_count = 1000;

    GPUChunkHeader header = gpu_chunk_header_from_chunk(&chunk);

    /* Verify level0 split correctly */
    ASSERT(header.level0_lo == 0x9ABCDEF0);
    ASSERT(header.level0_hi == 0x12345678);

    /* Verify packed fields */
    ASSERT((header.packed & 0xFF) == 1);           /* has_any */
    ASSERT(((header.packed >> 8) & 0xFF) == 0x55); /* level1 */
    ASSERT((header.packed >> 16) == 1000);         /* solid_count */
    return 1;
}

TEST(chunk_header_roundtrip_level0)
{
    /* Test that level0 can be reconstructed from lo/hi */
    Chunk chunk;
    memset(&chunk, 0, sizeof(chunk));
    chunk.occupancy.level0 = 0xDEADBEEFCAFEBABEULL;

    GPUChunkHeader header = gpu_chunk_header_from_chunk(&chunk);

    uint64_t reconstructed = ((uint64_t)header.level0_hi << 32) | header.level0_lo;
    ASSERT(reconstructed == chunk.occupancy.level0);
    return 1;
}

int main()
{
    printf("=== GPU Layout Tests (Non-GPU) ===\n\n");

    printf("GPU Struct Layout:\n");
    RUN_TEST(gpu_chunk_header_size);
    RUN_TEST(gpu_chunk_header_matches_uvec4);
    RUN_TEST(gpu_material_color_size);
    RUN_TEST(gpu_material_color_layout);
    RUN_TEST(gpu_material_palette_size);
    RUN_TEST(voxel_push_constants_size);
    RUN_TEST(voxel_push_constants_layout);
    RUN_TEST(voxel_temporal_ubo_size);
    RUN_TEST(voxel_temporal_ubo_layout);
    RUN_TEST(temporal_ubo_matches_glsl);
    RUN_TEST(scene_lighting_ubo_size);
    RUN_TEST(scene_lighting_ubo_layout);
    RUN_TEST(bvh_node_matches_glsl);
    RUN_TEST(voxel_object_gpu_matches_glsl);
    RUN_TEST(particle_gpu_matches_glsl);
    RUN_TEST(raster_push_constants_match_glsl);
    RUN_TEST(voxel_push_constants_match_glsl);

    printf("\nShader Embedding:\n");
    RUN_TEST(shader_ui_frag_embedded);
    RUN_TEST(shader_ui_vert_embedded);
    RUN_TEST(shader_voxel_vert_embedded);

    printf("\nChunkOccupancy → GPU Packing:\n");
    RUN_TEST(chunk_header_empty_chunk);
    RUN_TEST(chunk_header_full_occupancy);
    RUN_TEST(chunk_header_partial_occupancy);
    RUN_TEST(chunk_header_roundtrip_level0);

    printf("\nResults: %d/%d passed\n", g_tests_passed, g_tests_run);
    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
