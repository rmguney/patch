#include "volume_contact.h"
#include "engine/core/math.h"
#include <math.h>
#include <string.h>

int32_t volume_contact_sphere(const VoxelVolume *vol, Vec3 center, float radius,
                               VoxelContactResult *result)
{
    memset(result, 0, sizeof(VoxelContactResult));

    if (!vol)
        return 0;

    float vs = vol->voxel_size;
    float half_vs = vs * 0.5f;
    int32_t range = (int32_t)ceilf(radius / vs) + 1;

    Vec3 normal_sum = vec3_zero();

    for (int32_t dz = -range; dz <= range && result->count < CONTACT_MAX_VOXELS; dz++)
    {
        for (int32_t dy = -range; dy <= range && result->count < CONTACT_MAX_VOXELS; dy++)
        {
            for (int32_t dx = -range; dx <= range && result->count < CONTACT_MAX_VOXELS; dx++)
            {
                Vec3 check_pos = vec3_create(
                    center.x + dx * vs,
                    center.y + dy * vs,
                    center.z + dz * vs);

                uint8_t mat = volume_get_at(vol, check_pos);
                if (mat == 0)
                    continue;

                Vec3 voxel_center = volume_world_to_voxel_center(vol, check_pos);

                /* Find closest point on voxel box to sphere center */
                Vec3 closest;
                closest.x = fmaxf(voxel_center.x - half_vs, fminf(center.x, voxel_center.x + half_vs));
                closest.y = fmaxf(voxel_center.y - half_vs, fminf(center.y, voxel_center.y + half_vs));
                closest.z = fmaxf(voxel_center.z - half_vs, fminf(center.z, voxel_center.z + half_vs));

                Vec3 diff = vec3_sub(center, closest);
                float dist_sq = vec3_dot(diff, diff);

                if (dist_sq < radius * radius)
                {
                    float dist = sqrtf(dist_sq);
                    float depth = radius - dist;

                    Vec3 normal;
                    if (dist > 0.0001f)
                    {
                        normal = vec3_scale(diff, 1.0f / dist);
                    }
                    else
                    {
                        /* Sphere center inside voxel, push out along most penetrated axis */
                        Vec3 to_center = vec3_sub(center, voxel_center);
                        float ax = fabsf(to_center.x);
                        float ay = fabsf(to_center.y);
                        float az = fabsf(to_center.z);

                        if (ay >= ax && ay >= az)
                        {
                            normal = vec3_create(0.0f, to_center.y >= 0.0f ? 1.0f : -1.0f, 0.0f);
                        }
                        else if (ax >= az)
                        {
                            normal = vec3_create(to_center.x >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f);
                        }
                        else
                        {
                            normal = vec3_create(0.0f, 0.0f, to_center.z >= 0.0f ? 1.0f : -1.0f);
                        }
                        depth = half_vs + radius;
                    }

                    VoxelContact *contact = &result->contacts[result->count++];
                    contact->voxel_center = voxel_center;
                    contact->penetration = vec3_scale(normal, depth);
                    contact->depth = depth;
                    contact->material = mat;

                    normal_sum = vec3_add(normal_sum, normal);

                    if (depth > result->max_depth)
                        result->max_depth = depth;
                }
            }
        }
    }

    result->any_contact = (result->count > 0);
    if (result->count > 0)
    {
        result->average_normal = vec3_normalize(normal_sum);
    }

    return result->count;
}

int32_t volume_contact_aabb(const VoxelVolume *vol, Vec3 min_corner, Vec3 max_corner,
                             VoxelContactResult *result)
{
    memset(result, 0, sizeof(VoxelContactResult));

    if (!vol)
        return 0;

    float vs = vol->voxel_size;
    float half_vs = vs * 0.5f;

    Vec3 aabb_center = vec3_scale(vec3_add(min_corner, max_corner), 0.5f);
    Vec3 aabb_half = vec3_scale(vec3_sub(max_corner, min_corner), 0.5f);

    Vec3 normal_sum = vec3_zero();

    /* Iterate voxels in AABB range */
    int32_t start_x = (int32_t)floorf((min_corner.x - vol->bounds.min_x) / vs) - 1;
    int32_t end_x = (int32_t)ceilf((max_corner.x - vol->bounds.min_x) / vs) + 1;
    int32_t start_y = (int32_t)floorf((min_corner.y - vol->bounds.min_y) / vs) - 1;
    int32_t end_y = (int32_t)ceilf((max_corner.y - vol->bounds.min_y) / vs) + 1;
    int32_t start_z = (int32_t)floorf((min_corner.z - vol->bounds.min_z) / vs) - 1;
    int32_t end_z = (int32_t)ceilf((max_corner.z - vol->bounds.min_z) / vs) + 1;

    for (int32_t vz = start_z; vz <= end_z && result->count < CONTACT_MAX_VOXELS; vz++)
    {
        for (int32_t vy = start_y; vy <= end_y && result->count < CONTACT_MAX_VOXELS; vy++)
        {
            for (int32_t vx = start_x; vx <= end_x && result->count < CONTACT_MAX_VOXELS; vx++)
            {
                Vec3 check_pos = vec3_create(
                    vol->bounds.min_x + (vx + 0.5f) * vs,
                    vol->bounds.min_y + (vy + 0.5f) * vs,
                    vol->bounds.min_z + (vz + 0.5f) * vs);

                uint8_t mat = volume_get_at(vol, check_pos);
                if (mat == 0)
                    continue;

                Vec3 voxel_center = volume_world_to_voxel_center(vol, check_pos);

                /* AABB vs AABB overlap test */
                float ox = (aabb_half.x + half_vs) - fabsf(aabb_center.x - voxel_center.x);
                float oy = (aabb_half.y + half_vs) - fabsf(aabb_center.y - voxel_center.y);
                float oz = (aabb_half.z + half_vs) - fabsf(aabb_center.z - voxel_center.z);

                if (ox > 0.0f && oy > 0.0f && oz > 0.0f)
                {
                    /* Find minimum penetration axis */
                    Vec3 normal;
                    float depth;

                    if (ox <= oy && ox <= oz)
                    {
                        normal = vec3_create(aabb_center.x > voxel_center.x ? 1.0f : -1.0f, 0.0f, 0.0f);
                        depth = ox;
                    }
                    else if (oy <= oz)
                    {
                        normal = vec3_create(0.0f, aabb_center.y > voxel_center.y ? 1.0f : -1.0f, 0.0f);
                        depth = oy;
                    }
                    else
                    {
                        normal = vec3_create(0.0f, 0.0f, aabb_center.z > voxel_center.z ? 1.0f : -1.0f);
                        depth = oz;
                    }

                    VoxelContact *contact = &result->contacts[result->count++];
                    contact->voxel_center = voxel_center;
                    contact->penetration = vec3_scale(normal, depth);
                    contact->depth = depth;
                    contact->material = mat;

                    normal_sum = vec3_add(normal_sum, normal);

                    if (depth > result->max_depth)
                        result->max_depth = depth;
                }
            }
        }
    }

    result->any_contact = (result->count > 0);
    if (result->count > 0)
    {
        result->average_normal = vec3_normalize(normal_sum);
    }

    return result->count;
}

int32_t volume_contact_capsule(const VoxelVolume *vol, Vec3 p0, Vec3 p1, float radius,
                                VoxelContactResult *result)
{
    memset(result, 0, sizeof(VoxelContactResult));

    if (!vol)
        return 0;

    float vs = vol->voxel_size;
    float half_vs = vs * 0.5f;

    Vec3 seg = vec3_sub(p1, p0);
    float seg_len = vec3_length(seg);
    Vec3 seg_dir = (seg_len > 0.0001f) ? vec3_scale(seg, 1.0f / seg_len) : vec3_create(0, 1, 0);

    /* Compute AABB of capsule */
    Vec3 min_corner = vec3_create(
        fminf(p0.x, p1.x) - radius,
        fminf(p0.y, p1.y) - radius,
        fminf(p0.z, p1.z) - radius);
    Vec3 max_corner = vec3_create(
        fmaxf(p0.x, p1.x) + radius,
        fmaxf(p0.y, p1.y) + radius,
        fmaxf(p0.z, p1.z) + radius);

    Vec3 normal_sum = vec3_zero();

    int32_t start_x = (int32_t)floorf((min_corner.x - vol->bounds.min_x) / vs) - 1;
    int32_t end_x = (int32_t)ceilf((max_corner.x - vol->bounds.min_x) / vs) + 1;
    int32_t start_y = (int32_t)floorf((min_corner.y - vol->bounds.min_y) / vs) - 1;
    int32_t end_y = (int32_t)ceilf((max_corner.y - vol->bounds.min_y) / vs) + 1;
    int32_t start_z = (int32_t)floorf((min_corner.z - vol->bounds.min_z) / vs) - 1;
    int32_t end_z = (int32_t)ceilf((max_corner.z - vol->bounds.min_z) / vs) + 1;

    for (int32_t vz = start_z; vz <= end_z && result->count < CONTACT_MAX_VOXELS; vz++)
    {
        for (int32_t vy = start_y; vy <= end_y && result->count < CONTACT_MAX_VOXELS; vy++)
        {
            for (int32_t vx = start_x; vx <= end_x && result->count < CONTACT_MAX_VOXELS; vx++)
            {
                Vec3 check_pos = vec3_create(
                    vol->bounds.min_x + (vx + 0.5f) * vs,
                    vol->bounds.min_y + (vy + 0.5f) * vs,
                    vol->bounds.min_z + (vz + 0.5f) * vs);

                uint8_t mat = volume_get_at(vol, check_pos);
                if (mat == 0)
                    continue;

                Vec3 voxel_center = volume_world_to_voxel_center(vol, check_pos);

                /* Find closest point on capsule segment to voxel center */
                Vec3 to_voxel = vec3_sub(voxel_center, p0);
                float t = vec3_dot(to_voxel, seg_dir);
                t = fmaxf(0.0f, fminf(t, seg_len));
                Vec3 closest_on_seg = vec3_add(p0, vec3_scale(seg_dir, t));

                /* Now test sphere at closest_on_seg vs voxel */
                Vec3 closest_on_voxel;
                closest_on_voxel.x = fmaxf(voxel_center.x - half_vs, fminf(closest_on_seg.x, voxel_center.x + half_vs));
                closest_on_voxel.y = fmaxf(voxel_center.y - half_vs, fminf(closest_on_seg.y, voxel_center.y + half_vs));
                closest_on_voxel.z = fmaxf(voxel_center.z - half_vs, fminf(closest_on_seg.z, voxel_center.z + half_vs));

                Vec3 diff = vec3_sub(closest_on_seg, closest_on_voxel);
                float dist_sq = vec3_dot(diff, diff);

                if (dist_sq < radius * radius)
                {
                    float dist = sqrtf(dist_sq);
                    float depth = radius - dist;

                    Vec3 normal;
                    if (dist > 0.0001f)
                    {
                        normal = vec3_scale(diff, 1.0f / dist);
                    }
                    else
                    {
                        normal = vec3_create(0.0f, 1.0f, 0.0f);
                        depth = half_vs + radius;
                    }

                    VoxelContact *contact = &result->contacts[result->count++];
                    contact->voxel_center = voxel_center;
                    contact->penetration = vec3_scale(normal, depth);
                    contact->depth = depth;
                    contact->material = mat;

                    normal_sum = vec3_add(normal_sum, normal);

                    if (depth > result->max_depth)
                        result->max_depth = depth;
                }
            }
        }
    }

    result->any_contact = (result->count > 0);
    if (result->count > 0)
    {
        result->average_normal = vec3_normalize(normal_sum);
    }

    return result->count;
}

bool volume_contact_segment(const VoxelVolume *vol, Vec3 start, Vec3 end,
                             Vec3 *out_hit_pos, Vec3 *out_hit_normal, uint8_t *out_material)
{
    if (!vol)
        return false;

    Vec3 dir = vec3_sub(end, start);
    float max_dist = vec3_length(dir);
    if (max_dist < 0.0001f)
        return false;

    dir = vec3_scale(dir, 1.0f / max_dist);

    float hit_dist = volume_raycast(vol, start, dir, max_dist, out_hit_pos, out_hit_normal, out_material);
    return hit_dist > 0.0f;
}

Vec3 volume_contact_resolve(const VoxelContactResult *result)
{
    if (!result || result->count == 0)
        return vec3_zero();

    /* Find the deepest penetration and resolve along its normal */
    Vec3 push = vec3_zero();

    for (int32_t i = 0; i < result->count; i++)
    {
        const VoxelContact *c = &result->contacts[i];
        Vec3 p = c->penetration;

        /* Accumulate push-out, biased toward deeper contacts */
        push = vec3_add(push, vec3_scale(p, c->depth));
    }

    /* Normalize by total depth to get average push direction, then scale by max depth */
    float total_depth = 0.0f;
    for (int32_t i = 0; i < result->count; i++)
    {
        total_depth += result->contacts[i].depth;
    }

    if (total_depth > 0.0001f)
    {
        push = vec3_scale(push, 1.0f / total_depth);
        /* Scale to resolve max penetration */
        float push_len = vec3_length(push);
        if (push_len > 0.0001f)
        {
            push = vec3_scale(push, result->max_depth / push_len);
        }
    }

    return push;
}

float volume_sweep_sphere(const VoxelVolume *vol, Vec3 start, Vec3 direction, float distance,
                           float radius, Vec3 *out_hit_pos, Vec3 *out_hit_normal)
{
    if (!vol || distance < 0.0001f)
        return 1.0f;

    float vs = vol->voxel_size;
    float step = vs * 0.5f;
    int32_t steps = (int32_t)ceilf(distance / step);

    VoxelContactResult contacts;
    Vec3 pos = start;

    for (int32_t i = 0; i <= steps; i++)
    {
        float t = (float)i / (float)steps;
        pos = vec3_add(start, vec3_scale(direction, t * distance));

        volume_contact_sphere(vol, pos, radius, &contacts);
        if (contacts.any_contact)
        {
            if (out_hit_pos)
                *out_hit_pos = pos;
            if (out_hit_normal)
                *out_hit_normal = contacts.average_normal;
            return t;
        }
    }

    return 1.0f;
}

float volume_sweep_aabb(const VoxelVolume *vol, Vec3 start, Vec3 half_extents,
                         Vec3 direction, float distance,
                         Vec3 *out_hit_pos, Vec3 *out_hit_normal)
{
    if (!vol || distance < 0.0001f)
        return 1.0f;

    float vs = vol->voxel_size;
    float step = vs * 0.5f;
    int32_t steps = (int32_t)ceilf(distance / step);

    VoxelContactResult contacts;
    Vec3 pos = start;

    for (int32_t i = 0; i <= steps; i++)
    {
        float t = (float)i / (float)steps;
        pos = vec3_add(start, vec3_scale(direction, t * distance));

        Vec3 min_corner = vec3_sub(pos, half_extents);
        Vec3 max_corner = vec3_add(pos, half_extents);
        volume_contact_aabb(vol, min_corner, max_corner, &contacts);

        if (contacts.any_contact)
        {
            if (out_hit_pos)
                *out_hit_pos = pos;
            if (out_hit_normal)
                *out_hit_normal = contacts.average_normal;
            return t;
        }
    }

    return 1.0f;
}
