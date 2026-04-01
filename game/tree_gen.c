#include "tree_gen.h"
#include "content/materials.h"
#include "engine/core/noise.h"
#include <math.h>
#include <string.h>

#define K_TAU (2.0f * K_PI)
#define K_PHI 0.618033988749895f
#define K_RAD_PER_BRANCH (K_TAU * K_PHI)

static Bounds3D bounds_empty(void)
{
    Bounds3D b;
    b.min_x = 1e20f;
    b.min_y = 1e20f;
    b.min_z = 1e20f;
    b.max_x = -1e20f;
    b.max_y = -1e20f;
    b.max_z = -1e20f;
    return b;
}

static Bounds3D bounds_expand(Bounds3D a, Bounds3D b)
{
    Bounds3D r;
    r.min_x = minf(a.min_x, b.min_x);
    r.min_y = minf(a.min_y, b.min_y);
    r.min_z = minf(a.min_z, b.min_z);
    r.max_x = maxf(a.max_x, b.max_x);
    r.max_y = maxf(a.max_y, b.max_y);
    r.max_z = maxf(a.max_z, b.max_z);
    return r;
}

static float dist_to_line_segment_sq(Vec3 p, Vec3 a, Vec3 b)
{
    Vec3 ab = vec3_sub(b, a);
    Vec3 ap = vec3_sub(p, a);
    float t = vec3_dot(ap, ab) / maxf(vec3_dot(ab, ab), K_EPSILON);
    t = clampf(t, 0.0f, 1.0f);
    Vec3 closest = vec3_add(a, vec3_scale(ab, t));
    Vec3 diff = vec3_sub(p, closest);
    return vec3_dot(diff, diff);
}

static Vec3 project_point_on_line(Vec3 p, Vec3 a, Vec3 b)
{
    Vec3 ab = vec3_sub(b, a);
    Vec3 ap = vec3_sub(p, a);
    float t = vec3_dot(ap, ab) / maxf(vec3_dot(ab, ab), K_EPSILON);
    t = clampf(t, 0.0f, 1.0f);
    return vec3_add(a, vec3_scale(ab, t));
}

/* Ported from Veloren tree.rs:819-933 add_branch() */
static int32_t add_branch(ProceduralTree *tree, const TreeConfig *config,
                           Vec3 start, Vec3 dir, float branch_len, float branch_radius,
                           int32_t depth, int32_t sibling_idx, float proportion, RngState *rng)
{
    if (tree->branch_count >= TREE_MAX_BRANCHES)
        return -1;

    Vec3 end = vec3_add(start, vec3_scale(dir, branch_len));
    float wood_radius = branch_radius;
    float leaf_radius = 0.0f;

    if (depth == config->max_depth)
    {
        leaf_radius = lerpf(config->leaf_radius_min, config->leaf_radius_max, rng_float(rng));
        float bias_factor = lerpf(1.0f, 1.0f - proportion, fabsf(config->branch_len_bias));
        leaf_radius += config->leaf_radius_scaled * bias_factor;
        if (leaf_radius < 0.0f)
            leaf_radius = 0.0f;
    }

    float extent = maxf(wood_radius, leaf_radius);
    Bounds3D aabb;
    aabb.min_x = minf(start.x, end.x) - extent;
    aabb.min_y = minf(start.y, end.y) - extent;
    aabb.min_z = minf(start.z, end.z) - extent;
    aabb.max_x = maxf(start.x, end.x) + extent;
    aabb.max_y = maxf(start.y, end.y) + extent;
    aabb.max_z = maxf(start.z, end.z) + extent;

    int32_t child_idx = -1;

    if (depth < config->max_depth)
    {
        Vec3 rand_vec = vec3_create(
            rng_float(rng) * 2.0f - 1.0f,
            rng_float(rng) * 2.0f - 1.0f,
            rng_float(rng) * 2.0f - 1.0f);
        Vec3 x_axis = vec3_normalize(vec3_cross(dir, rand_vec));
        Vec3 y_axis = vec3_normalize(vec3_cross(dir, x_axis));
        float screw_shift = rng_float(rng) * K_TAU;

        float splits_f = lerpf(config->splits_min, config->splits_max, rng_float(rng));
        int32_t splits = (int32_t)(splits_f + 0.5f);
        if (splits < 1)
            splits = 1;

        for (int32_t i = 0; i < splits; i++)
        {
            float prop = (splits > 1) ? (float)i / (float)(splits - 1) : 0.5f;
            float dist = lerpf(rng_float(rng), prop, config->proportionality);

            float angle = screw_shift + (float)i * K_RAD_PER_BRANCH;
            Vec3 screw = vec3_add(
                vec3_scale(x_axis, sinf(angle)),
                vec3_scale(y_axis, cosf(angle)));

            float split_factor = lerpf(config->split_range_min, config->split_range_max, dist);
            Vec3 tgt = vec3_add(
                vec3_lerp(start, end, split_factor),
                vec3_lerp(rand_vec, screw, config->proportionality));

            Vec3 branch_start = project_point_on_line(tgt, start, end);
            Vec3 branch_dir_raw = vec3_sub(tgt, branch_start);
            Vec3 branch_dir = vec3_normalize(
                vec3_lerp(branch_dir_raw, dir, config->straightness));

            float child_len = branch_len * config->branch_child_len
                * (1.0f - (split_factor - 0.5f) * 2.0f
                   * clampf(config->branch_len_bias, -1.0f, 1.0f));

            float child_radius;
            if (config->branch_child_radius_lerp)
                child_radius = lerpf(branch_radius * config->branch_child_radius,
                                     branch_radius, prop);
            else
                child_radius = branch_radius * config->branch_child_radius;

            int32_t branch_idx = add_branch(tree, config, branch_start, branch_dir,
                                             child_len, child_radius,
                                             depth + 1, child_idx, prop, rng);
            if (branch_idx >= 0)
            {
                child_idx = branch_idx;
                aabb = bounds_expand(aabb, tree->branches[branch_idx].aabb);
            }
        }
    }

    int32_t idx = tree->branch_count;
    if (idx >= TREE_MAX_BRANCHES)
        return -1;

    tree->branches[idx].start = start;
    tree->branches[idx].end = end;
    tree->branches[idx].wood_radius = wood_radius;
    tree->branches[idx].leaf_radius = leaf_radius;
    tree->branches[idx].leaf_vertical_scale = config->leaf_vertical_scale;
    tree->branches[idx].aabb = aabb;
    tree->branches[idx].sibling_idx = sibling_idx;
    tree->branches[idx].child_idx = child_idx;
    tree->branch_count++;

    return idx;
}

void tree_generate(ProceduralTree *tree, const TreeConfig *config, RngState *rng)
{
    memset(tree, 0, sizeof(ProceduralTree));
    tree->branch_count = 0;
    tree->trunk_idx = -1;
    tree->bounds = bounds_empty();

    Vec3 trunk_origin = vec3_create(0.0f, -config->trunk_radius * 0.5f, 0.0f);
    Vec3 trunk_dir = vec3_normalize(vec3_create(
        rng_float(rng) * 0.2f - 0.1f,
        10.0f,
        rng_float(rng) * 0.2f - 0.1f));

    int32_t trunk_idx = add_branch(tree, config, trunk_origin, trunk_dir,
                                    config->trunk_len, config->trunk_radius,
                                    0, -1, 1.0f, rng);
    tree->trunk_idx = trunk_idx;

    if (trunk_idx >= 0)
        tree->bounds = tree->branches[trunk_idx].aabb;
}

static void voxelize_branch(const TreeBranch *branches, int32_t idx,
                             const TreeConfig *config, VoxelVolume *vol,
                             Vec3 origin, float voxel_size)
{
    if (idx < 0)
        return;

    const TreeBranch *b = &branches[idx];

    Bounds3D world_aabb;
    world_aabb.min_x = origin.x + b->aabb.min_x;
    world_aabb.min_y = origin.y + b->aabb.min_y;
    world_aabb.min_z = origin.z + b->aabb.min_z;
    world_aabb.max_x = origin.x + b->aabb.max_x;
    world_aabb.max_y = origin.y + b->aabb.max_y;
    world_aabb.max_z = origin.z + b->aabb.max_z;

    if (world_aabb.max_x < vol->bounds.min_x || world_aabb.min_x > vol->bounds.max_x ||
        world_aabb.max_y < vol->bounds.min_y || world_aabb.min_y > vol->bounds.max_y ||
        world_aabb.max_z < vol->bounds.min_z || world_aabb.min_z > vol->bounds.max_z)
        return;

    Vec3 ws = vec3_add(origin, b->start);
    Vec3 we = vec3_add(origin, b->end);

    float step = voxel_size;
    float wood_r2 = b->wood_radius * b->wood_radius;
    bool has_leaves = b->leaf_radius > 0.0f;
    Vec3 mid = vec3_scale(vec3_add(ws, we), 0.5f);
    float yscale = b->leaf_vertical_scale;
    if (yscale < K_EPSILON)
        yscale = 1.0f;

    for (float x = world_aabb.min_x; x <= world_aabb.max_x; x += step)
    {
        for (float z = world_aabb.min_z; z <= world_aabb.max_z; z += step)
        {
            float n1 = noise_value_2d(x * 2.0f, z * 2.0f, 88888);
            float n2 = noise_value_2d(x * 5.0f, z * 5.0f, 99999);
            float noisy_r = b->leaf_radius * (1.0f + n1 * 0.3f + n2 * 0.15f);
            float noisy_r2 = noisy_r * noisy_r;

            for (float y = world_aabb.min_y; y <= world_aabb.max_y; y += step)
            {
                Vec3 pos = vec3_create(x, y, z);
                float d2 = dist_to_line_segment_sq(pos, ws, we);

                if (d2 <= wood_r2)
                {
                    volume_set_at(vol, pos, config->bark_material);
                }
                else if (has_leaves)
                {
                    Vec3 diff = vec3_sub(pos, mid);
                    float scaled_y = diff.y / yscale;
                    float leaf_d2 = diff.x * diff.x + scaled_y * scaled_y + diff.z * diff.z;
                    if (leaf_d2 <= noisy_r2)
                    {
                        if (!volume_is_solid_at(vol, pos))
                            volume_set_at(vol, pos, config->leaf_material);
                    }
                }
            }
        }
    }

    voxelize_branch(branches, b->child_idx, config, vol, origin, voxel_size);
    voxelize_branch(branches, b->sibling_idx, config, vol, origin, voxel_size);
}

void tree_voxelize(const ProceduralTree *tree, const TreeConfig *config,
                   VoxelVolume *vol, Vec3 origin, float voxel_size)
{
    if (tree->trunk_idx < 0)
        return;

    voxelize_branch(tree->branches, tree->trunk_idx, config, vol, origin, voxel_size);
}

/* --- Tree config presets ported from Veloren tree.rs:382-680 --- */

TreeConfig tree_config_create(TreeType type, RngState *rng, float scale)
{
    float s = scale * (0.8f + rng_float(rng) * rng_float(rng) * 0.5f);
    float log_s = maxf(s, 0.15f);

    TreeConfig c;
    memset(&c, 0, sizeof(c));
    c.branch_child_radius_lerp = true;
    c.leaf_vertical_scale = 1.0f;
    c.proportionality = 0.0f;

    switch (type)
    {
    case TREE_OAK:
        c.trunk_len = 11.0f * s;
        c.trunk_radius = 1.5f * s;
        c.branch_child_len = 0.75f;
        c.branch_child_radius = 0.75f;
        c.leaf_radius_min = 2.0f * log_s;
        c.leaf_radius_max = 2.5f * log_s;
        c.leaf_radius_scaled = 0.0f;
        c.straightness = 0.3f;
        c.max_depth = 4;
        c.splits_min = 4.25f;
        c.splits_max = 6.25f;
        c.split_range_min = 0.75f;
        c.split_range_max = 1.5f;
        c.bark_material = MAT_OAK_BARK;
        c.leaf_material = MAT_OAK_LEAF;
        break;

    case TREE_PINE:
        c.trunk_len = 20.0f * s;
        c.trunk_radius = 1.5f * s;
        c.branch_child_len = 0.3f / maxf(s, 0.5f);
        c.branch_child_radius = 0.0f;
        c.branch_child_radius_lerp = false;
        c.leaf_radius_min = 1.2f;
        c.leaf_radius_max = 2.0f;
        c.leaf_radius_scaled = 0.3f * log_s;
        c.straightness = 0.3f;
        c.max_depth = 1;
        c.splits_min = 20.0f * s;
        c.splits_max = 22.0f * s;
        c.split_range_min = 0.1f;
        c.split_range_max = 1.2f;
        c.branch_len_bias = 0.75f;
        c.leaf_vertical_scale = 0.6f;
        c.proportionality = 1.0f;
        c.bark_material = MAT_PINE_BARK;
        c.leaf_material = MAT_PINE_LEAF;
        break;

    case TREE_FROSTPINE:
        c.trunk_len = 36.0f * s;
        c.trunk_radius = 2.3f * s;
        c.branch_child_len = 0.25f / maxf(s, 0.5f);
        c.branch_child_radius = 0.0f;
        c.branch_child_radius_lerp = false;
        c.leaf_radius_min = 1.3f;
        c.leaf_radius_max = 2.2f;
        c.leaf_radius_scaled = 0.4f * log_s;
        c.straightness = 0.3f;
        c.max_depth = 1;
        c.splits_min = 34.0f * s;
        c.splits_max = 35.0f * s;
        c.split_range_min = 0.1f;
        c.split_range_max = 1.2f;
        c.branch_len_bias = 0.75f;
        c.leaf_vertical_scale = 0.6f;
        c.proportionality = 1.0f;
        c.bark_material = MAT_PINE_BARK;
        c.leaf_material = MAT_PINE_LEAF;
        break;

    case TREE_BIRCH:
        c.trunk_len = 14.0f * s;
        c.trunk_radius = 1.0f * s;
        c.branch_child_len = 0.7f;
        c.branch_child_radius = 0.65f;
        c.leaf_radius_min = 1.8f * log_s;
        c.leaf_radius_max = 2.2f * log_s;
        c.leaf_radius_scaled = 0.0f;
        c.straightness = 0.4f;
        c.max_depth = 3;
        c.splits_min = 3.5f;
        c.splits_max = 5.5f;
        c.split_range_min = 0.6f;
        c.split_range_max = 1.4f;
        c.bark_material = MAT_BIRCH_BARK;
        c.leaf_material = MAT_BIRCH_LEAF;
        break;

    case TREE_JUNGLE:
        s = scale * (0.8f + rng_float(rng) * 0.5f);
        log_s = maxf(s, 0.15f);
        c.trunk_len = 44.0f * s;
        c.trunk_radius = 2.25f * s;
        c.branch_child_len = 0.35f;
        c.branch_child_radius = 0.5f;
        c.leaf_radius_min = 10.0f * log_s;
        c.leaf_radius_max = 11.5f * log_s;
        c.leaf_radius_scaled = -8.0f * log_s;
        c.straightness = 0.2f;
        c.max_depth = 2;
        c.splits_min = 7.5f;
        c.splits_max = 8.5f;
        c.split_range_min = 0.2f;
        c.split_range_max = 1.25f;
        c.branch_len_bias = 0.5f;
        c.leaf_vertical_scale = 0.35f;
        c.proportionality = 0.8f;
        c.bark_material = MAT_JUNGLE_BARK;
        c.leaf_material = MAT_JUNGLE_LEAF;
        break;

    case TREE_BAOBAB:
        s = scale * (0.5f + rng_float(rng) * rng_float(rng) * rng_float(rng) * rng_float(rng));
        log_s = maxf(s, 0.15f);
        c.trunk_len = 24.0f * s;
        c.trunk_radius = 7.0f * s;
        c.branch_child_len = 0.55f;
        c.branch_child_radius = 0.3f;
        c.leaf_radius_min = 2.5f * log_s;
        c.leaf_radius_max = 3.0f * log_s;
        c.leaf_radius_scaled = 0.0f;
        c.straightness = 0.5f;
        c.max_depth = 4;
        c.splits_min = 3.0f;
        c.splits_max = 3.5f;
        c.split_range_min = 0.95f;
        c.split_range_max = 1.0f;
        c.leaf_vertical_scale = 0.2f;
        c.proportionality = 1.0f;
        c.bark_material = MAT_JUNGLE_BARK;
        c.leaf_material = MAT_OAK_LEAF;
        break;

    case TREE_ACACIA:
        c.trunk_len = 16.0f * s;
        c.trunk_radius = 2.0f * s;
        c.branch_child_len = 0.6f;
        c.branch_child_radius = 0.45f;
        c.leaf_radius_min = 6.0f * log_s;
        c.leaf_radius_max = 7.0f * log_s;
        c.leaf_radius_scaled = -4.0f * log_s;
        c.straightness = 0.3f;
        c.max_depth = 2;
        c.splits_min = 5.0f;
        c.splits_max = 6.0f;
        c.split_range_min = 0.6f;
        c.split_range_max = 1.2f;
        c.branch_len_bias = 0.4f;
        c.leaf_vertical_scale = 0.25f;
        c.proportionality = 0.7f;
        c.bark_material = MAT_OAK_BARK;
        c.leaf_material = MAT_OAK_LEAF;
        break;

    case TREE_CHERRY:
        c.trunk_len = 8.0f * s;
        c.trunk_radius = 1.2f * s;
        c.branch_child_len = 0.85f;
        c.branch_child_radius = 0.8f;
        c.leaf_radius_min = 2.0f * log_s;
        c.leaf_radius_max = 3.0f * log_s;
        c.leaf_radius_scaled = 0.0f;
        c.straightness = 0.35f;
        c.max_depth = 4;
        c.splits_min = 2.5f;
        c.splits_max = 4.5f;
        c.split_range_min = 0.5f;
        c.split_range_max = 1.5f;
        c.bark_material = MAT_OAK_BARK;
        c.leaf_material = MAT_CHERRY_LEAF;
        break;

    case TREE_DEAD:
        c.trunk_len = 9.0f * s;
        c.trunk_radius = 2.0f * s;
        c.branch_child_len = 0.9f;
        c.branch_child_radius = 0.7f;
        c.leaf_radius_min = 0.0f;
        c.leaf_radius_max = 0.1f;
        c.leaf_radius_scaled = 0.0f;
        c.straightness = 0.35f;
        c.max_depth = 3;
        c.splits_min = 2.25f;
        c.splits_max = 3.25f;
        c.split_range_min = 0.75f;
        c.split_range_max = 1.5f;
        c.bark_material = MAT_DEAD_WOOD;
        c.leaf_material = MAT_DEAD_WOOD;
        break;

    case TREE_MANGROVE:
        c.trunk_len = 10.0f * s;
        c.trunk_radius = 1.8f * s;
        c.branch_child_len = 0.65f;
        c.branch_child_radius = 0.6f;
        c.leaf_radius_min = 2.0f * log_s;
        c.leaf_radius_max = 3.0f * log_s;
        c.leaf_radius_scaled = 0.0f;
        c.straightness = 0.25f;
        c.max_depth = 3;
        c.splits_min = 3.0f;
        c.splits_max = 5.0f;
        c.split_range_min = 0.4f;
        c.split_range_max = 1.3f;
        c.bark_material = MAT_JUNGLE_BARK;
        c.leaf_material = MAT_JUNGLE_LEAF;
        break;

    case TREE_REDWOOD:
        c.trunk_len = 50.0f * s;
        c.trunk_radius = 3.0f * s;
        c.branch_child_len = 0.3f / maxf(s, 0.5f);
        c.branch_child_radius = 0.0f;
        c.branch_child_radius_lerp = false;
        c.leaf_radius_min = 1.5f;
        c.leaf_radius_max = 2.5f;
        c.leaf_radius_scaled = 0.5f * log_s;
        c.straightness = 0.4f;
        c.max_depth = 1;
        c.splits_min = 40.0f * s;
        c.splits_max = 42.0f * s;
        c.split_range_min = 0.3f;
        c.split_range_max = 1.1f;
        c.branch_len_bias = 0.7f;
        c.leaf_vertical_scale = 0.5f;
        c.proportionality = 1.0f;
        c.bark_material = MAT_OAK_BARK;
        c.leaf_material = MAT_PINE_LEAF;
        break;

    case TREE_CEDAR:
        c.trunk_len = 22.0f * s;
        c.trunk_radius = 1.8f * s;
        c.branch_child_len = 0.3f / maxf(s, 0.5f);
        c.branch_child_radius = 0.0f;
        c.branch_child_radius_lerp = false;
        c.leaf_radius_min = 1.4f;
        c.leaf_radius_max = 2.3f;
        c.leaf_radius_scaled = 0.35f * log_s;
        c.straightness = 0.35f;
        c.max_depth = 1;
        c.splits_min = 24.0f * s;
        c.splits_max = 26.0f * s;
        c.split_range_min = 0.15f;
        c.split_range_max = 1.15f;
        c.branch_len_bias = 0.7f;
        c.leaf_vertical_scale = 0.55f;
        c.proportionality = 1.0f;
        c.bark_material = MAT_PINE_BARK;
        c.leaf_material = MAT_PINE_LEAF;
        break;

    case TREE_MAPLE:
        c.trunk_len = 12.0f * s;
        c.trunk_radius = 1.3f * s;
        c.branch_child_len = 0.75f;
        c.branch_child_radius = 0.7f;
        c.leaf_radius_min = 2.2f * log_s;
        c.leaf_radius_max = 2.8f * log_s;
        c.leaf_radius_scaled = 0.0f;
        c.straightness = 0.3f;
        c.max_depth = 4;
        c.splits_min = 4.0f;
        c.splits_max = 5.5f;
        c.split_range_min = 0.7f;
        c.split_range_max = 1.4f;
        c.bark_material = MAT_OAK_BARK;
        c.leaf_material = MAT_AUTUMN_LEAF;
        break;

    case TREE_PALM:
        c.trunk_len = 18.0f * s;
        c.trunk_radius = 1.0f * s;
        c.branch_child_len = 0.4f;
        c.branch_child_radius = 0.0f;
        c.branch_child_radius_lerp = false;
        c.leaf_radius_min = 3.0f * log_s;
        c.leaf_radius_max = 4.0f * log_s;
        c.leaf_radius_scaled = 0.0f;
        c.straightness = 0.6f;
        c.max_depth = 1;
        c.splits_min = 6.0f;
        c.splits_max = 8.0f;
        c.split_range_min = 0.9f;
        c.split_range_max = 1.1f;
        c.leaf_vertical_scale = 0.3f;
        c.proportionality = 1.0f;
        c.bark_material = MAT_JUNGLE_BARK;
        c.leaf_material = MAT_JUNGLE_LEAF;
        break;

    case TREE_SWAMP:
        c.trunk_len = 10.0f * s;
        c.trunk_radius = 1.6f * s;
        c.branch_child_len = 0.8f;
        c.branch_child_radius = 0.65f;
        c.leaf_radius_min = 2.5f * log_s;
        c.leaf_radius_max = 3.5f * log_s;
        c.leaf_radius_scaled = 0.0f;
        c.straightness = 0.2f;
        c.max_depth = 3;
        c.splits_min = 3.0f;
        c.splits_max = 4.5f;
        c.split_range_min = 0.5f;
        c.split_range_max = 1.3f;
        c.bark_material = MAT_JUNGLE_BARK;
        c.leaf_material = MAT_OAK_LEAF;
        break;

    case TREE_GIANT:
        s = scale * (1.5f + rng_float(rng) * 0.5f);
        log_s = maxf(s, 0.15f);
        c.trunk_len = 30.0f * s;
        c.trunk_radius = 4.0f * s;
        c.branch_child_len = 0.6f;
        c.branch_child_radius = 0.5f;
        c.leaf_radius_min = 5.0f * log_s;
        c.leaf_radius_max = 7.0f * log_s;
        c.leaf_radius_scaled = 0.0f;
        c.straightness = 0.4f;
        c.max_depth = 3;
        c.splits_min = 4.0f;
        c.splits_max = 5.0f;
        c.split_range_min = 0.7f;
        c.split_range_max = 1.2f;
        c.leaf_vertical_scale = 0.6f;
        c.bark_material = MAT_OAK_BARK;
        c.leaf_material = MAT_OAK_LEAF;
        break;

    case TREE_AUTUMN:
        c.trunk_len = 11.0f * s;
        c.trunk_radius = 1.4f * s;
        c.branch_child_len = 0.75f;
        c.branch_child_radius = 0.7f;
        c.leaf_radius_min = 2.0f * log_s;
        c.leaf_radius_max = 2.5f * log_s;
        c.leaf_radius_scaled = 0.0f;
        c.straightness = 0.3f;
        c.max_depth = 4;
        c.splits_min = 4.0f;
        c.splits_max = 6.0f;
        c.split_range_min = 0.75f;
        c.split_range_max = 1.5f;
        c.bark_material = MAT_OAK_BARK;
        c.leaf_material = MAT_AUTUMN_LEAF;
        break;

    case TREE_CHESTNUT:
        c.trunk_len = 13.0f * s;
        c.trunk_radius = 1.6f * s;
        c.branch_child_len = 0.7f;
        c.branch_child_radius = 0.7f;
        c.leaf_radius_min = 2.5f * log_s;
        c.leaf_radius_max = 3.0f * log_s;
        c.leaf_radius_scaled = 0.0f;
        c.straightness = 0.35f;
        c.max_depth = 4;
        c.splits_min = 3.5f;
        c.splits_max = 5.0f;
        c.split_range_min = 0.65f;
        c.split_range_max = 1.4f;
        c.bark_material = MAT_OAK_BARK;
        c.leaf_material = MAT_OAK_LEAF;
        break;

    default:
        c = tree_config_create(TREE_OAK, rng, scale);
        break;
    }

    return c;
}
