#include "bvh.h"
#include "voxel_object.h"
#include <stdlib.h>
#include <string.h>
#include <float.h>

#define BVH_SAH_TRAVERSAL_COST 1.0f
#define BVH_SAH_INTERSECTION_COST 2.0f
#define BVH_STACK_SIZE (BVH_MAX_DEPTH + 1)

/* Per-object data is stored at the object world index */
STATIC_ASSERT(BVH_MAX_OBJECTS >= VOBJ_MAX_OBJECTS, "BVH arrays are indexed by world object index");

typedef struct
{
    float aabb_min[3];
    float aabb_max[3];
    int32_t count;
} SAHBin;

static float compute_surface_area(const float min[3], const float max[3])
{
    float dx = max[0] - min[0];
    float dy = max[1] - min[1];
    float dz = max[2] - min[2];
    if (dx <= 0.0f || dy <= 0.0f || dz <= 0.0f)
        return 0.0f;
    return 2.0f * (dx * dy + dy * dz + dz * dx);
}

static void update_node_bounds(BVH *bvh, int32_t node_idx)
{
    BVHNode *node = &bvh->nodes[node_idx];
    float min_bound[3] = {FLT_MAX, FLT_MAX, FLT_MAX};
    float max_bound[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};

    for (int32_t i = 0; i < node->count; i++)
    {
        int32_t obj_idx = bvh->object_indices[node->left_first + i];
        for (int32_t j = 0; j < 3; j++)
        {
            if (bvh->obj_aabb_min[obj_idx][j] < min_bound[j])
                min_bound[j] = bvh->obj_aabb_min[obj_idx][j];
            if (bvh->obj_aabb_max[obj_idx][j] > max_bound[j])
                max_bound[j] = bvh->obj_aabb_max[obj_idx][j];
        }
    }

    for (int32_t j = 0; j < 3; j++)
    {
        node->aabb_min[j] = min_bound[j];
        node->aabb_max[j] = max_bound[j];
    }
}

static float get_centroid_axis(const BVH *bvh, int32_t obj_idx, int32_t axis)
{
    switch (axis)
    {
    case 0:
        return bvh->obj_centroids[obj_idx].x;
    case 1:
        return bvh->obj_centroids[obj_idx].y;
    default:
        return bvh->obj_centroids[obj_idx].z;
    }
}

static float find_best_split(BVH *bvh, int32_t node_idx, int32_t *out_axis, float *out_pos)
{
    BVHNode *node = &bvh->nodes[node_idx];
    float best_cost = FLT_MAX;
    *out_axis = -1;
    *out_pos = 0.0f;

    for (int32_t axis = 0; axis < 3; axis++)
    {
        float cent_min = FLT_MAX, cent_max = -FLT_MAX;
        for (int32_t i = 0; i < node->count; i++)
        {
            int32_t obj_idx = bvh->object_indices[node->left_first + i];
            float cent = get_centroid_axis(bvh, obj_idx, axis);
            if (cent < cent_min)
                cent_min = cent;
            if (cent > cent_max)
                cent_max = cent;
        }

        if (cent_max - cent_min < 0.001f)
            continue;

        SAHBin bins[BVH_SAH_BINS];
        for (int32_t b = 0; b < BVH_SAH_BINS; b++)
        {
            bins[b].aabb_min[0] = bins[b].aabb_min[1] = bins[b].aabb_min[2] = FLT_MAX;
            bins[b].aabb_max[0] = bins[b].aabb_max[1] = bins[b].aabb_max[2] = -FLT_MAX;
            bins[b].count = 0;
        }

        float bin_scale = (float)BVH_SAH_BINS / (cent_max - cent_min);
        for (int32_t i = 0; i < node->count; i++)
        {
            int32_t obj_idx = bvh->object_indices[node->left_first + i];
            float cent = get_centroid_axis(bvh, obj_idx, axis);
            int32_t bin_idx = (int32_t)((cent - cent_min) * bin_scale);
            if (bin_idx >= BVH_SAH_BINS)
                bin_idx = BVH_SAH_BINS - 1;

            bins[bin_idx].count++;
            for (int32_t j = 0; j < 3; j++)
            {
                if (bvh->obj_aabb_min[obj_idx][j] < bins[bin_idx].aabb_min[j])
                    bins[bin_idx].aabb_min[j] = bvh->obj_aabb_min[obj_idx][j];
                if (bvh->obj_aabb_max[obj_idx][j] > bins[bin_idx].aabb_max[j])
                    bins[bin_idx].aabb_max[j] = bvh->obj_aabb_max[obj_idx][j];
            }
        }

        float left_area[BVH_SAH_BINS - 1], right_area[BVH_SAH_BINS - 1];
        int32_t left_count[BVH_SAH_BINS - 1], right_count[BVH_SAH_BINS - 1];

        float acc_min[3] = {FLT_MAX, FLT_MAX, FLT_MAX};
        float acc_max[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
        int32_t acc_count = 0;
        for (int32_t b = 0; b < BVH_SAH_BINS - 1; b++)
        {
            acc_count += bins[b].count;
            for (int32_t j = 0; j < 3; j++)
            {
                if (bins[b].aabb_min[j] < acc_min[j])
                    acc_min[j] = bins[b].aabb_min[j];
                if (bins[b].aabb_max[j] > acc_max[j])
                    acc_max[j] = bins[b].aabb_max[j];
            }
            left_count[b] = acc_count;
            left_area[b] = compute_surface_area(acc_min, acc_max);
        }

        acc_min[0] = acc_min[1] = acc_min[2] = FLT_MAX;
        acc_max[0] = acc_max[1] = acc_max[2] = -FLT_MAX;
        acc_count = 0;
        for (int32_t b = BVH_SAH_BINS - 1; b > 0; b--)
        {
            acc_count += bins[b].count;
            for (int32_t j = 0; j < 3; j++)
            {
                if (bins[b].aabb_min[j] < acc_min[j])
                    acc_min[j] = bins[b].aabb_min[j];
                if (bins[b].aabb_max[j] > acc_max[j])
                    acc_max[j] = bins[b].aabb_max[j];
            }
            right_count[b - 1] = acc_count;
            right_area[b - 1] = compute_surface_area(acc_min, acc_max);
        }

        float parent_area = compute_surface_area(node->aabb_min, node->aabb_max);
        if (parent_area < 0.0001f)
            continue;
        float scale = BVH_SAH_INTERSECTION_COST / parent_area;

        for (int32_t b = 0; b < BVH_SAH_BINS - 1; b++)
        {
            if (left_count[b] == 0 || right_count[b] == 0)
                continue;
            float cost = BVH_SAH_TRAVERSAL_COST +
                         scale * ((float)left_count[b] * left_area[b] + (float)right_count[b] * right_area[b]);
            if (cost < best_cost)
            {
                best_cost = cost;
                *out_axis = axis;
                *out_pos = cent_min + (b + 1) * (cent_max - cent_min) / BVH_SAH_BINS;
            }
        }
    }

    return best_cost;
}

/* Partition a node's objects by centroid against split_pos; returns the left count */
static int32_t partition_sah(BVH *bvh, const BVHNode *node, int32_t axis, float split_pos)
{
    int32_t left_end = node->left_first;
    int32_t right_start = node->left_first + node->count - 1;

    while (left_end <= right_start)
    {
        int32_t obj_idx = bvh->object_indices[left_end];
        if (get_centroid_axis(bvh, obj_idx, axis) < split_pos)
        {
            left_end++;
        }
        else
        {
            bvh->object_indices[left_end] = bvh->object_indices[right_start];
            bvh->object_indices[right_start] = obj_idx;
            right_start--;
        }
    }
    return left_end - node->left_first;
}

/* Sort a node's objects by centroid along the widest centroid axis and split them in
 * half; returns the left count */
static int32_t partition_median(BVH *bvh, const BVHNode *node)
{
    int32_t *objs = &bvh->object_indices[node->left_first];
    float cmin[3] = {FLT_MAX, FLT_MAX, FLT_MAX};
    float cmax[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    for (int32_t i = 0; i < node->count; i++)
    {
        for (int32_t a = 0; a < 3; a++)
        {
            float c = get_centroid_axis(bvh, objs[i], a);
            cmin[a] = c < cmin[a] ? c : cmin[a];
            cmax[a] = c > cmax[a] ? c : cmax[a];
        }
    }
    int32_t axis = 0;
    for (int32_t a = 1; a < 3; a++)
        if (cmax[a] - cmin[a] > cmax[axis] - cmin[axis])
            axis = a;

    for (int32_t i = 1; i < node->count; i++)
    {
        int32_t obj_idx = objs[i];
        float key = get_centroid_axis(bvh, obj_idx, axis);
        int32_t j = i - 1;
        while (j >= 0 && get_centroid_axis(bvh, objs[j], axis) > key)
        {
            objs[j + 1] = objs[j];
            j--;
        }
        objs[j + 1] = obj_idx;
    }
    return (node->count + 1) / 2;
}

/* Levels of halving that bring `count` objects down to leaf size */
static int32_t balanced_levels(int32_t count)
{
    int32_t levels = 0;
    while (count > BVH_LEAF_MAX_OBJECTS)
    {
        count = (count + 1) / 2;
        levels++;
    }
    return levels;
}

/* Split a node by SAH while depth allows. Once depth + balanced_levels(count) reaches
 * BVH_MAX_DEPTH, median splits finish the subtree, so no leaf is deeper than that. */
static void subdivide(BVH *bvh, int32_t node_idx, int32_t depth)
{
    BVHNode *node = &bvh->nodes[node_idx];
    if (node->count <= BVH_LEAF_MAX_OBJECTS)
        return;

    int32_t left_count;
    if (depth + balanced_levels(node->count) < BVH_MAX_DEPTH)
    {
        int32_t split_axis;
        float split_pos;
        float split_cost = find_best_split(bvh, node_idx, &split_axis, &split_pos);
        float no_split_cost = (float)node->count * BVH_SAH_INTERSECTION_COST;
        if (split_axis < 0 || split_cost >= no_split_cost)
            return;

        left_count = partition_sah(bvh, node, split_axis, split_pos);
        if (left_count == 0 || left_count == node->count)
            return;
    }
    else
    {
        left_count = partition_median(bvh, node);
    }

    int32_t left_idx = bvh->node_count++;
    int32_t right_idx = bvh->node_count++;

    bvh->nodes[left_idx].left_first = node->left_first;
    bvh->nodes[left_idx].count = left_count;
    bvh->nodes[right_idx].left_first = node->left_first + left_count;
    bvh->nodes[right_idx].count = node->count - left_count;

    node->left_first = left_idx;
    node->count = 0;

    update_node_bounds(bvh, left_idx);
    update_node_bounds(bvh, right_idx);

    subdivide(bvh, left_idx, depth + 1);
    subdivide(bvh, right_idx, depth + 1);
}

BVH *bvh_create(void)
{
    BVH *bvh = (BVH *)calloc(1, sizeof(BVH));
    return bvh;
}

void bvh_destroy(BVH *bvh)
{
    if (bvh)
        free(bvh);
}

/* Bounds of object i (world index) at position */
static void store_object_bounds(BVH *bvh, int32_t i, Vec3 position, float r)
{
    bvh->obj_centroids[i] = position;
    bvh->obj_aabb_min[i][0] = position.x - r;
    bvh->obj_aabb_min[i][1] = position.y - r;
    bvh->obj_aabb_min[i][2] = position.z - r;
    bvh->obj_aabb_max[i][0] = position.x + r;
    bvh->obj_aabb_max[i][1] = position.y + r;
    bvh->obj_aabb_max[i][2] = position.z + r;
}

void bvh_build(BVH *bvh, const VoxelObjectWorld *world, const Vec3 *positions)
{
    bvh->node_count = 0;
    bvh->object_count = 0;

    for (int32_t i = 0; i < VOBJ_MAX_OBJECTS && bvh->object_count < BVH_MAX_OBJECTS; i++)
    {
        const VoxelObject *obj = &world->objects[i];
        if (!obj->active)
            continue;

        int32_t idx = bvh->object_count++;
        bvh->object_indices[idx] = i;

        /* Store AABBs/centroids at WORLD index (i), not BVH-internal index (idx).
           update_node_bounds reads via object_indices[] which yields world indices. */
        store_object_bounds(bvh, i, positions ? positions[i] : obj->position, obj->radius);
    }

    if (bvh->object_count == 0)
        return;

    BVHNode *root = &bvh->nodes[bvh->node_count++];
    root->left_first = 0;
    root->count = bvh->object_count;
    update_node_bounds(bvh, 0);

    subdivide(bvh, 0, 0);
}

void bvh_refit(BVH *bvh, const VoxelObjectWorld *world, const Vec3 *positions)
{
    for (int32_t i = 0; i < bvh->object_count; i++)
    {
        int32_t world_idx = bvh->object_indices[i];
        const VoxelObject *obj = &world->objects[world_idx];

        /* Store at WORLD index to match update_node_bounds which reads via object_indices */
        store_object_bounds(bvh, world_idx, positions ? positions[world_idx] : obj->position, obj->radius);
    }

    for (int32_t i = bvh->node_count - 1; i >= 0; i--)
    {
        BVHNode *node = &bvh->nodes[i];

        if (node->count > 0)
        {
            update_node_bounds(bvh, i);
        }
        else
        {
            BVHNode *left = &bvh->nodes[node->left_first];
            BVHNode *right = &bvh->nodes[node->left_first + 1];

            for (int32_t j = 0; j < 3; j++)
            {
                node->aabb_min[j] = left->aabb_min[j] < right->aabb_min[j] ? left->aabb_min[j] : right->aabb_min[j];
                node->aabb_max[j] = left->aabb_max[j] > right->aabb_max[j] ? left->aabb_max[j] : right->aabb_max[j];
            }
        }
    }
}

bool bvh_needs_rebuild(const BVH *bvh, const VoxelObjectWorld *world)
{
    int32_t active_count = 0;
    for (int32_t i = 0; i < VOBJ_MAX_OBJECTS; i++)
    {
        if (world->objects[i].active)
            active_count++;
    }

    if (active_count != bvh->object_count)
        return true;

    for (int32_t i = 0; i < bvh->object_count; i++)
    {
        int32_t world_idx = bvh->object_indices[i];
        if (world_idx >= VOBJ_MAX_OBJECTS || !world->objects[world_idx].active)
            return true;
    }

    return false;
}

static float ray_aabb_intersect(Vec3 origin, Vec3 inv_dir, const float aabb_min[3], const float aabb_max[3])
{
    float t0x = (aabb_min[0] - origin.x) * inv_dir.x;
    float t1x = (aabb_max[0] - origin.x) * inv_dir.x;
    float t0y = (aabb_min[1] - origin.y) * inv_dir.y;
    float t1y = (aabb_max[1] - origin.y) * inv_dir.y;
    float t0z = (aabb_min[2] - origin.z) * inv_dir.z;
    float t1z = (aabb_max[2] - origin.z) * inv_dir.z;

    float tmin_x = t0x < t1x ? t0x : t1x;
    float tmax_x = t0x > t1x ? t0x : t1x;
    float tmin_y = t0y < t1y ? t0y : t1y;
    float tmax_y = t0y > t1y ? t0y : t1y;
    float tmin_z = t0z < t1z ? t0z : t1z;
    float tmax_z = t0z > t1z ? t0z : t1z;

    float tmin = tmin_x > tmin_y ? tmin_x : tmin_y;
    tmin = tmin > tmin_z ? tmin : tmin_z;
    float tmax = tmax_x < tmax_y ? tmax_x : tmax_y;
    tmax = tmax < tmax_z ? tmax : tmax_z;

    if (tmin <= tmax && tmax > 0.0f)
        return tmin > 0.0f ? tmin : 0.0f;
    return FLT_MAX;
}

int32_t bvh_query_ray_candidates(const BVH *bvh, Vec3 origin, Vec3 dir, float max_dist,
                                 int32_t *out_indices, int32_t max_results)
{
    if (bvh->node_count <= 0)
        return 0;

    Vec3 inv_dir = vec3_create(
        fabsf(dir.x) > K_EPSILON ? 1.0f / dir.x : (dir.x >= 0 ? FLT_MAX : -FLT_MAX),
        fabsf(dir.y) > K_EPSILON ? 1.0f / dir.y : (dir.y >= 0 ? FLT_MAX : -FLT_MAX),
        fabsf(dir.z) > K_EPSILON ? 1.0f / dir.z : (dir.z >= 0 ? FLT_MAX : -FLT_MAX));

    int32_t stack[BVH_STACK_SIZE];
    int32_t stack_ptr = 0;
    stack[stack_ptr++] = 0;

    int32_t count = 0;

    while (stack_ptr > 0 && count < max_results)
    {
        int32_t node_idx = stack[--stack_ptr];
        const BVHNode *node = &bvh->nodes[node_idx];

        float t_hit = ray_aabb_intersect(origin, inv_dir, node->aabb_min, node->aabb_max);
        if (t_hit > max_dist)
            continue;

        if (node->count > 0)
        {
            for (int32_t i = 0; i < node->count && count < max_results; i++)
            {
                out_indices[count++] = bvh->object_indices[node->left_first + i];
            }
        }
        else
        {
            int32_t left_idx = node->left_first;
            int32_t right_idx = node->left_first + 1;

            float t_left = ray_aabb_intersect(origin, inv_dir, bvh->nodes[left_idx].aabb_min, bvh->nodes[left_idx].aabb_max);
            float t_right = ray_aabb_intersect(origin, inv_dir, bvh->nodes[right_idx].aabb_min, bvh->nodes[right_idx].aabb_max);

            if (t_left < t_right)
            {
                if (t_right <= max_dist && stack_ptr < BVH_STACK_SIZE)
                    stack[stack_ptr++] = right_idx;
                if (t_left <= max_dist && stack_ptr < BVH_STACK_SIZE)
                    stack[stack_ptr++] = left_idx;
            }
            else
            {
                if (t_left <= max_dist && stack_ptr < BVH_STACK_SIZE)
                    stack[stack_ptr++] = left_idx;
                if (t_right <= max_dist && stack_ptr < BVH_STACK_SIZE)
                    stack[stack_ptr++] = right_idx;
            }
        }
    }

    return count;
}

static bool sphere_aabb_intersect(Vec3 center, float radius, const float aabb_min[3], const float aabb_max[3])
{
    float dist_sq = 0.0f;

    if (center.x < aabb_min[0])
    {
        float d = aabb_min[0] - center.x;
        dist_sq += d * d;
    }
    else if (center.x > aabb_max[0])
    {
        float d = center.x - aabb_max[0];
        dist_sq += d * d;
    }

    if (center.y < aabb_min[1])
    {
        float d = aabb_min[1] - center.y;
        dist_sq += d * d;
    }
    else if (center.y > aabb_max[1])
    {
        float d = center.y - aabb_max[1];
        dist_sq += d * d;
    }

    if (center.z < aabb_min[2])
    {
        float d = aabb_min[2] - center.z;
        dist_sq += d * d;
    }
    else if (center.z > aabb_max[2])
    {
        float d = center.z - aabb_max[2];
        dist_sq += d * d;
    }

    return dist_sq <= radius * radius;
}

BVHQueryResult bvh_query_sphere(const BVH *bvh, Vec3 center, float radius)
{
    BVHQueryResult result = {0};

    if (bvh->node_count <= 0)
        return result;

    int32_t stack[BVH_STACK_SIZE];
    int32_t stack_ptr = 0;
    stack[stack_ptr++] = 0;

    while (stack_ptr > 0 && result.count < BVH_QUERY_MAX_RESULTS)
    {
        int32_t node_idx = stack[--stack_ptr];
        const BVHNode *node = &bvh->nodes[node_idx];

        if (!sphere_aabb_intersect(center, radius, node->aabb_min, node->aabb_max))
            continue;

        if (node->count > 0)
        {
            for (int32_t i = 0; i < node->count && result.count < BVH_QUERY_MAX_RESULTS; i++)
            {
                result.indices[result.count++] = bvh->object_indices[node->left_first + i];
            }
        }
        else
        {
            if (stack_ptr < BVH_STACK_SIZE - 1)
            {
                stack[stack_ptr++] = node->left_first;
                stack[stack_ptr++] = node->left_first + 1;
            }
        }
    }

    return result;
}
