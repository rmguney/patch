#include "tree_gen.h"
#include "game/terrain_gen.h"
#include "content/materials.h"
#include "engine/core/noise.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define K_TAU (2.0f * K_PI)
#define K_PHI 0.618033988749895f
#define K_RAD_PER_BRANCH (K_TAU * K_PHI)

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

    /* Claim the slot before recursing, so a full array drops leaves, never their parents */
    int32_t idx = tree->branch_count++;

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

    TreeBranch *b = &tree->branches[idx];
    b->start = start;
    b->end = end;
    b->wood_radius = wood_radius;
    b->leaf_radius = leaf_radius;
    b->aabb = aabb;
    b->sibling_idx = sibling_idx;
    b->child_idx = child_idx;

    return idx;
}

void tree_generate(ProceduralTree *tree, const TreeConfig *config, RngState *rng)
{
    tree->branch_count = 0;

    Vec3 trunk_origin = vec3_create(0.0f, -config->trunk_radius * 0.5f, 0.0f);
    Vec3 trunk_dir = vec3_normalize(vec3_create(
        rng_float(rng) * 0.2f - 0.1f,
        10.0f,
        rng_float(rng) * 0.2f - 0.1f));

    tree->trunk_idx = add_branch(tree, config, trunk_origin, trunk_dir,
                                 config->trunk_len, config->trunk_radius,
                                 0, -1, 1.0f, rng);
}

/* Leaf crowns swell by up to this factor with the radius noise (|noise| <= 1) */
#define LEAF_NOISE_MAX_SCALE 1.45f
/* Seeds of the leaf radius noise's two octaves, added to the world seed */
#define LEAF_NOISE_SEED_A 88888u
#define LEAF_NOISE_SEED_B 99999u

/* Index range of the voxel centres inside [lo, hi] on one axis, clipped to the volume and
 * to clip_first..clip_last; false when empty */
static bool voxel_span(float lo, float hi, float vol_min, float voxel_size,
                       int32_t clip_first, int32_t clip_last, int32_t *first, int32_t *last)
{
    *first = (int32_t)ceilf((lo - vol_min) / voxel_size - 0.5f);
    *last = (int32_t)floorf((hi - vol_min) / voxel_size - 0.5f);
    if (*first < clip_first)
        *first = clip_first;
    if (*last > clip_last)
        *last = clip_last;
    return *first <= *last;
}

/* Leaf radius noise of the voxel columns under one tree, computed on first use: the leaf
 * clusters of a tree overlap heavily and would otherwise evaluate it again for each */
typedef struct
{
    float *scale; /* 0 = not computed yet (the scale is always > 0.5) */
    int32_t first_x, first_z;
    int32_t nx, nz;
    uint32_t seed; /* World seed */
} LeafNoiseCache;

static float leaf_noise_scale(LeafNoiseCache *cache, int32_t ix, int32_t iz, float x, float z)
{
    float *slot = NULL;
    int32_t cx = ix - cache->first_x;
    int32_t cz = iz - cache->first_z;
    if (cache->scale && cx >= 0 && cx < cache->nx && cz >= 0 && cz < cache->nz)
    {
        slot = &cache->scale[cz * cache->nx + cx];
        if (*slot > 0.0f)
            return *slot;
    }
    float n1 = noise_value_2d(x * 2.0f, z * 2.0f, cache->seed + LEAF_NOISE_SEED_A);
    float n2 = noise_value_2d(x * 5.0f, z * 5.0f, cache->seed + LEAF_NOISE_SEED_B);
    float scale = 1.0f + n1 * 0.3f + n2 * 0.15f;
    if (slot)
        *slot = scale;
    return scale;
}

/* Writes one branch's own wood and leaves into the voxel centres of the box around its
 * segment (its aabb also spans the children, which are written separately), inside the
 * clip box. The wood fills a capsule around the segment and the leaves an ellipsoid
 * around its middle, so each is only tested inside its own bounds; the leaves never
 * leave the segment box. */
static void voxelize_branch_shape(const TreeBranch *b, const TreeConfig *config, VoxelVolume *vol,
                                  Vec3 origin, float voxel_size, const VoxelBox *clip,
                                  LeafNoiseCache *leaf_noise)
{
    Vec3 ws = vec3_add(origin, b->start);
    Vec3 we = vec3_add(origin, b->end);
    float extent = maxf(b->wood_radius, b->leaf_radius);

    const int32_t clip_first[3] = {clip->min_x, clip->min_y, clip->min_z};
    const int32_t clip_last[3] = {clip->max_x, clip->max_y, clip->max_z};
    const float vol_min[3] = {vol->bounds.min_x, vol->bounds.min_y, vol->bounds.min_z};
    const float seg_lo[3] = {minf(ws.x, we.x), minf(ws.y, we.y), minf(ws.z, we.z)};
    const float seg_hi[3] = {maxf(ws.x, we.x), maxf(ws.y, we.y), maxf(ws.z, we.z)};
    int32_t first[3], last[3];
    for (int axis = 0; axis < 3; axis++)
    {
        if (!voxel_span(seg_lo[axis] - extent, seg_hi[axis] + extent, vol_min[axis], voxel_size,
                        clip_first[axis], clip_last[axis], &first[axis], &last[axis]))
            return;
    }

    /* Wood: segment box grown by the wood radius */
    float wood_r2 = b->wood_radius * b->wood_radius;
    int32_t wf[3], wl[3];
    bool has_wood = b->wood_radius > 0.0f;
    for (int axis = 0; axis < 3 && has_wood; axis++)
        has_wood = voxel_span(seg_lo[axis] - b->wood_radius, seg_hi[axis] + b->wood_radius, vol_min[axis],
                              voxel_size, first[axis], last[axis], &wf[axis], &wl[axis]);
    if (has_wood)
    {
        for (int32_t ix = wf[0]; ix <= wl[0]; ix++)
        {
            float x = vol_min[0] + ((float)ix + 0.5f) * voxel_size;
            for (int32_t iz = wf[2]; iz <= wl[2]; iz++)
            {
                float z = vol_min[2] + ((float)iz + 0.5f) * voxel_size;
                for (int32_t iy = wf[1]; iy <= wl[1]; iy++)
                {
                    Vec3 pos = vec3_create(x, vol_min[1] + ((float)iy + 0.5f) * voxel_size, z);
                    if (dist_to_line_segment_sq(pos, ws, we) <= wood_r2)
                        terrain_voxel_set(vol, ix, iy, iz, config->bark_material);
                }
            }
        }
    }

    if (b->leaf_radius <= 0.0f)
        return;

    /* Leaves: ellipsoid around the middle, its radius swollen by noise per column */
    Vec3 mid = vec3_scale(vec3_add(ws, we), 0.5f);
    float yscale = config->leaf_vertical_scale;
    if (yscale < K_EPSILON)
        yscale = 1.0f;
    const float reach = b->leaf_radius * LEAF_NOISE_MAX_SCALE + voxel_size;
    int32_t lf[3], ll[3];
    if (!voxel_span(mid.x - reach, mid.x + reach, vol_min[0], voxel_size, first[0], last[0], &lf[0], &ll[0]) ||
        !voxel_span(mid.z - reach, mid.z + reach, vol_min[2], voxel_size, first[2], last[2], &lf[2], &ll[2]))
        return;

    for (int32_t ix = lf[0]; ix <= ll[0]; ix++)
    {
        float x = vol_min[0] + ((float)ix + 0.5f) * voxel_size;
        float dx = x - mid.x;
        for (int32_t iz = lf[2]; iz <= ll[2]; iz++)
        {
            float z = vol_min[2] + ((float)iz + 0.5f) * voxel_size;
            float dz = z - mid.z;

            float noisy_r = b->leaf_radius * leaf_noise_scale(leaf_noise, ix, iz, x, z);
            float noisy_r2 = noisy_r * noisy_r;
            float rest = noisy_r2 - dx * dx - dz * dz;
            if (rest < 0.0f)
                continue;

            /* Rows this column's slice of the ellipsoid can reach (a voxel of slack: the
             * exact test below decides) */
            float half_height = yscale * sqrtf(rest) + voxel_size;
            int32_t y_first, y_last;
            if (!voxel_span(mid.y - half_height, mid.y + half_height, vol_min[1], voxel_size,
                            first[1], last[1], &y_first, &y_last))
                continue;

            for (int32_t iy = y_first; iy <= y_last; iy++)
            {
                Vec3 pos = vec3_create(x, vol_min[1] + ((float)iy + 0.5f) * voxel_size, z);
                Vec3 diff = vec3_sub(pos, mid);
                float scaled_y = diff.y / yscale;
                float leaf_d2 = diff.x * diff.x + scaled_y * scaled_y + diff.z * diff.z;
                /* The segment's own wood is already solid, as are earlier branches */
                if (leaf_d2 <= noisy_r2 && terrain_voxel_get(vol, ix, iy, iz) == MAT_AIR)
                    terrain_voxel_set(vol, ix, iy, iz, config->leaf_material);
            }
        }
    }
}

/* World-space bounds of the voxels of a clip box */
typedef struct
{
    float min_x, min_y, min_z;
    float max_x, max_y, max_z;
} ClipBounds;

static void voxelize_branch(const TreeBranch *branches, int32_t idx, const TreeConfig *config,
                            VoxelVolume *vol, Vec3 origin, float voxel_size, const VoxelBox *clip,
                            const ClipBounds *clip_bounds, LeafNoiseCache *leaf_noise)
{
    /* Siblings are walked here, not through the recursion, so culling one branch's
     * subtree never drops the siblings after it. A branch writes only inside its aabb, so
     * when that misses the clip box neither the branch nor its children write there. */
    for (; idx >= 0; idx = branches[idx].sibling_idx)
    {
        const TreeBranch *b = &branches[idx];

        if (origin.x + b->aabb.max_x < clip_bounds->min_x || origin.x + b->aabb.min_x > clip_bounds->max_x ||
            origin.y + b->aabb.max_y < clip_bounds->min_y || origin.y + b->aabb.min_y > clip_bounds->max_y ||
            origin.z + b->aabb.max_z < clip_bounds->min_z || origin.z + b->aabb.min_z > clip_bounds->max_z)
            continue;

        voxelize_branch_shape(b, config, vol, origin, voxel_size, clip, leaf_noise);
        voxelize_branch(branches, b->child_idx, config, vol, origin, voxel_size, clip, clip_bounds, leaf_noise);
    }
}

void tree_voxelize_clipped(const ProceduralTree *tree, const TreeConfig *config, VoxelVolume *vol,
                           Vec3 origin, const VoxelBox *clip, uint32_t seed)
{
    if (tree->trunk_idx < 0)
        return;

    const float voxel_size = vol->voxel_size;
    ClipBounds clip_bounds;
    clip_bounds.min_x = vol->bounds.min_x + (float)clip->min_x * voxel_size;
    clip_bounds.min_y = vol->bounds.min_y + (float)clip->min_y * voxel_size;
    clip_bounds.min_z = vol->bounds.min_z + (float)clip->min_z * voxel_size;
    clip_bounds.max_x = vol->bounds.min_x + (float)(clip->max_x + 1) * voxel_size;
    clip_bounds.max_y = vol->bounds.min_y + (float)(clip->max_y + 1) * voxel_size;
    clip_bounds.max_z = vol->bounds.min_z + (float)(clip->max_z + 1) * voxel_size;

    /* The trunk's box spans the whole tree; without memory for the cache the noise is
     * evaluated per use */
    const Bounds3D *box = &tree->branches[tree->trunk_idx].aabb;
    LeafNoiseCache leaf_noise = {0};
    leaf_noise.seed = seed;
    int32_t last_x, last_z;
    if (voxel_span(origin.x + box->min_x, origin.x + box->max_x, vol->bounds.min_x, voxel_size, clip->min_x,
                   clip->max_x, &leaf_noise.first_x, &last_x) &&
        voxel_span(origin.z + box->min_z, origin.z + box->max_z, vol->bounds.min_z, voxel_size, clip->min_z,
                   clip->max_z, &leaf_noise.first_z, &last_z))
    {
        leaf_noise.nx = last_x - leaf_noise.first_x + 1;
        leaf_noise.nz = last_z - leaf_noise.first_z + 1;
        leaf_noise.scale = (float *)calloc((size_t)leaf_noise.nx * (size_t)leaf_noise.nz, sizeof(float));
    }

    voxelize_branch(tree->branches, tree->trunk_idx, config, vol, origin, voxel_size, clip, &clip_bounds,
                    &leaf_noise);
    free(leaf_noise.scale);
}

/* --- Tree config presets ported from Veloren tree.rs:382-680 --- */

TreeConfig tree_config_create(TreeType type, RngState *rng, float scale)
{
    /* size: this tree against the species' typical one. Lengths and radii follow s; counts
     * and length ratios (the conifers' branches) follow size alone, so a world scale that
     * shrinks every tree does not thin out their branches. */
    float size = 0.8f + rng_float(rng) * rng_float(rng) * 0.5f;
    float s = scale * size;
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
        c.branch_child_len = 0.3f / size;
        c.branch_child_radius = 0.0f;
        c.branch_child_radius_lerp = false;
        c.leaf_radius_min = 1.2f * log_s;
        c.leaf_radius_max = 2.0f * log_s;
        c.leaf_radius_scaled = 0.3f * log_s;
        c.straightness = 0.3f;
        c.max_depth = 1;
        c.splits_min = 20.0f * size;
        c.splits_max = 22.0f * size;
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
        c.branch_child_len = 0.25f / size;
        c.branch_child_radius = 0.0f;
        c.branch_child_radius_lerp = false;
        c.leaf_radius_min = 1.3f * log_s;
        c.leaf_radius_max = 2.2f * log_s;
        c.leaf_radius_scaled = 0.4f * log_s;
        c.straightness = 0.3f;
        c.max_depth = 1;
        c.splits_min = 34.0f * size;
        c.splits_max = 35.0f * size;
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
        c.branch_child_len = 0.3f / size;
        c.branch_child_radius = 0.0f;
        c.branch_child_radius_lerp = false;
        c.leaf_radius_min = 1.5f * log_s;
        c.leaf_radius_max = 2.5f * log_s;
        c.leaf_radius_scaled = 0.5f * log_s;
        c.straightness = 0.4f;
        c.max_depth = 1;
        c.splits_min = 40.0f * size;
        c.splits_max = 42.0f * size;
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
        c.branch_child_len = 0.3f / size;
        c.branch_child_radius = 0.0f;
        c.branch_child_radius_lerp = false;
        c.leaf_radius_min = 1.4f * log_s;
        c.leaf_radius_max = 2.3f * log_s;
        c.leaf_radius_scaled = 0.35f * log_s;
        c.straightness = 0.35f;
        c.max_depth = 1;
        c.splits_min = 24.0f * size;
        c.splits_max = 26.0f * size;
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
