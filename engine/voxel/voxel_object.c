#include "voxel_object.h"
#include "bvh.h"
#include "content/materials.h"
#include "engine/core/profile.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static uint32_t g_voxel_revision_counter = 0;

uint32_t voxel_object_next_revision(void)
{
    g_voxel_revision_counter++;
    if (g_voxel_revision_counter == 0)
        g_voxel_revision_counter = 1;
    return g_voxel_revision_counter;
}

STATIC_ASSERT(VOBJ_BRICK_COUNT <= 64, "brick masks are 64-bit");

void voxel_object_mark_changed(VoxelObject *obj, uint64_t bricks)
{
    obj->voxel_revision = voxel_object_next_revision();
    for (int32_t b = 0; b < VOBJ_BRICK_COUNT; b++)
    {
        if (bricks & (1ull << b))
            obj->brick_revision[b] = obj->voxel_revision;
    }
}

int32_t voxel_object_world_alloc_slot(VoxelObjectWorld *world)
{
    /* O(1) allocation via free-list */
    int32_t slot;
    if (world->first_free_slot >= 0)
    {
        slot = world->first_free_slot;
        world->first_free_slot = world->objects[slot].next_free;
    }
    else
    {
        if (world->object_count >= VOBJ_MAX_OBJECTS)
            return -1;
        slot = world->object_count++;
    }

    VoxelObject *obj = &world->objects[slot];
    memset(obj, 0, sizeof(VoxelObject));
    obj->next_free = -1;
    obj->next_dirty = -1;
    if (++world->next_alloc_id == 0)
        world->next_alloc_id = 1;
    obj->alloc_id = world->next_alloc_id;
    /* Revisions are process-wide: no brick of the new occupant matches a stamp a cache took
     * from the slot's previous one (or from another world) */
    voxel_object_mark_changed(obj, VOBJ_ALL_BRICKS);
    return slot;
}

#define VOBJ_HULL_LATTICE_DIRS 26
#define VOBJ_HULL_SPREAD_DIRS 100
#define VOBJ_HULL_DIRS (VOBJ_HULL_LATTICE_DIRS + VOBJ_HULL_SPREAD_DIRS)
STATIC_ASSERT(VOBJ_HULL_DIRS <= VOBJ_MAX_SURFACE_VOXELS, "hull points must fit surface_voxels");

/* Convex hull candidate points: the extreme voxel corner in each of a fixed set of
 * directions (26 lattice directions plus 100 spread evenly over the sphere). Only the
 * first and last voxel of each x-row can own an extreme corner, since the voxels in
 * between lie inside the hull of those two. The result is bounded by VOBJ_HULL_DIRS,
 * so it never truncates, and it is exact for boxes and other axis-aligned shapes. */
static void compute_hull_points(VoxelObject *obj)
{
    Vec3 dirs[VOBJ_HULL_DIRS];
    int32_t dir_count = 0;
    for (int32_t dz = -1; dz <= 1; dz++)
        for (int32_t dy = -1; dy <= 1; dy++)
            for (int32_t dx = -1; dx <= 1; dx++)
                if (dx != 0 || dy != 0 || dz != 0)
                    dirs[dir_count++] = vec3_normalize(vec3_create((float)dx, (float)dy, (float)dz));

    const float golden_angle = 2.39996323f;
    for (int32_t i = 0; i < VOBJ_HULL_SPREAD_DIRS; i++)
    {
        float y = 1.0f - 2.0f * ((float)i + 0.5f) / (float)VOBJ_HULL_SPREAD_DIRS;
        float r = sqrtf(fmaxf(0.0f, 1.0f - y * y));
        float phi = golden_angle * (float)i;
        dirs[dir_count++] = vec3_create(r * cosf(phi), y, r * sinf(phi));
    }

    /* Per direction: which row end can win (the max-x voxel if d.x > 0, else the
     * min-x one; ties do not matter) and which corner of that voxel is extreme. */
    bool use_row_max[VOBJ_HULL_DIRS];
    int32_t corner_off[VOBJ_HULL_DIRS][3];
    float best_dot[VOBJ_HULL_DIRS];
    int32_t best_corner[VOBJ_HULL_DIRS][3];
    for (int32_t d = 0; d < VOBJ_HULL_DIRS; d++)
    {
        use_row_max[d] = dirs[d].x > 0.0f;
        corner_off[d][0] = dirs[d].x > 0.0f ? 1 : 0;
        corner_off[d][1] = dirs[d].y > 0.0f ? 1 : 0;
        corner_off[d][2] = dirs[d].z > 0.0f ? 1 : 0;
        best_dot[d] = -1e30f;
    }

    for (int32_t z = 0; z < VOBJ_GRID_SIZE; z++)
    {
        for (int32_t y = 0; y < VOBJ_GRID_SIZE; y++)
        {
            int32_t row_min = -1, row_max = -1;
            for (int32_t x = 0; x < VOBJ_GRID_SIZE; x++)
            {
                if (obj->voxels[vobj_index(x, y, z)].material != 0)
                {
                    if (row_min < 0)
                        row_min = x;
                    row_max = x;
                }
            }
            if (row_min < 0)
                continue;

            for (int32_t d = 0; d < VOBJ_HULL_DIRS; d++)
            {
                int32_t cx = (use_row_max[d] ? row_max : row_min) + corner_off[d][0];
                int32_t cy = y + corner_off[d][1];
                int32_t cz = z + corner_off[d][2];
                float dot = dirs[d].x * (float)cx + dirs[d].y * (float)cy + dirs[d].z * (float)cz;
                if (dot > best_dot[d])
                {
                    best_dot[d] = dot;
                    best_corner[d][0] = cx;
                    best_corner[d][1] = cy;
                    best_corner[d][2] = cz;
                }
            }
        }
    }

    float half_grid = (float)VOBJ_GRID_SIZE * 0.5f;
    obj->surface_voxel_count = 0;
    for (int32_t d = 0; d < VOBJ_HULL_DIRS; d++)
    {
        if (best_dot[d] <= -1e29f)
            continue;

        bool duplicate = false;
        for (int32_t p = 0; p < d && !duplicate; p++)
        {
            duplicate = best_dot[p] > -1e29f &&
                        best_corner[p][0] == best_corner[d][0] &&
                        best_corner[p][1] == best_corner[d][1] &&
                        best_corner[p][2] == best_corner[d][2];
        }
        if (duplicate)
            continue;

        obj->surface_voxels[obj->surface_voxel_count++] = vec3_create(
            ((float)best_corner[d][0] - half_grid) * obj->voxel_size,
            ((float)best_corner[d][1] - half_grid) * obj->voxel_size,
            ((float)best_corner[d][2] - half_grid) * obj->voxel_size);
    }
}

void voxel_object_recalc_shape(VoxelObject *obj)
{
    /* shape_dirty is left alone: it marks the object as linked in the dirty list,
     * which only voxel_object_world_process_recalcs unlinks */
    if (obj->voxel_count <= 0)
    {
        obj->active = false;
        obj->surface_voxel_count = 0;
        obj->voxel_revision = voxel_object_next_revision();
        return;
    }

    /* Recenter voxels in the 32³ grid, so they keep room to every side of obj->position.
     * After splits or destruction, voxels may be clustered in one corner of the grid.
     * The grid centre (position 16) is a voxel corner, so an odd extent ends up half a
     * voxel towards -axis: shape_center records where the box really is. */
    bool recentered = false;
    {
        int32_t bmin_x = VOBJ_GRID_SIZE, bmax_x = 0;
        int32_t bmin_y = VOBJ_GRID_SIZE, bmax_y = 0;
        int32_t bmin_z = VOBJ_GRID_SIZE, bmax_z = 0;

        for (int32_t z = 0; z < VOBJ_GRID_SIZE; z++)
            for (int32_t y = 0; y < VOBJ_GRID_SIZE; y++)
                for (int32_t x = 0; x < VOBJ_GRID_SIZE; x++)
                {
                    if (obj->voxels[vobj_index(x, y, z)].material != 0)
                    {
                        if (x < bmin_x) bmin_x = x;
                        if (x > bmax_x) bmax_x = x;
                        if (y < bmin_y) bmin_y = y;
                        if (y > bmax_y) bmax_y = y;
                        if (z < bmin_z) bmin_z = z;
                        if (z > bmax_z) bmax_z = z;
                    }
                }

        int32_t occ_x = bmax_x - bmin_x + 1;
        int32_t occ_y = bmax_y - bmin_y + 1;
        int32_t occ_z = bmax_z - bmin_z + 1;
        int32_t ideal_x = (VOBJ_GRID_SIZE - occ_x) / 2;
        int32_t ideal_y = (VOBJ_GRID_SIZE - occ_y) / 2;
        int32_t ideal_z = (VOBJ_GRID_SIZE - occ_z) / 2;
        int32_t sx = ideal_x - bmin_x;
        int32_t sy = ideal_y - bmin_y;
        int32_t sz = ideal_z - bmin_z;

        if (sx != 0 || sy != 0 || sz != 0)
        {
            Vec3 local_shift = vec3_create(
                -(float)sx * obj->voxel_size,
                -(float)sy * obj->voxel_size,
                -(float)sz * obj->voxel_size);
            obj->position = vec3_add(obj->position,
                                     quat_rotate_vec3(obj->orientation, local_shift));
            /* The previous pose moves with the voxels, so drawing does not glide across it */
            obj->prev_position = vec3_add(obj->prev_position,
                                          quat_rotate_vec3(obj->prev_orientation, local_shift));

            VObjVoxel tmp[VOBJ_TOTAL_VOXELS];
            memset(tmp, 0, sizeof(tmp));
            for (int32_t z = bmin_z; z <= bmax_z; z++)
                for (int32_t y = bmin_y; y <= bmax_y; y++)
                    for (int32_t x = bmin_x; x <= bmax_x; x++)
                    {
                        int32_t src = vobj_index(x, y, z);
                        if (obj->voxels[src].material == 0)
                            continue;
                        int32_t nx = x + sx, ny = y + sy, nz = z + sz;
                        tmp[vobj_index(nx, ny, nz)] = obj->voxels[src];
                    }
            memcpy(obj->voxels, tmp, sizeof(obj->voxels));
            recentered = true;
        }
    }

    int32_t min_x = VOBJ_GRID_SIZE, max_x = 0;
    int32_t min_y = VOBJ_GRID_SIZE, max_y = 0;
    int32_t min_z = VOBJ_GRID_SIZE, max_z = 0;

    float half_grid = (float)VOBJ_GRID_SIZE * 0.5f;

    for (int32_t z = 0; z < VOBJ_GRID_SIZE; z++)
    {
        for (int32_t y = 0; y < VOBJ_GRID_SIZE; y++)
        {
            for (int32_t x = 0; x < VOBJ_GRID_SIZE; x++)
            {
                if (obj->voxels[vobj_index(x, y, z)].material != 0)
                {
                    if (x < min_x)
                        min_x = x;
                    if (x > max_x)
                        max_x = x;
                    if (y < min_y)
                        min_y = y;
                    if (y > max_y)
                        max_y = y;
                    if (z < min_z)
                        min_z = z;
                    if (z > max_z)
                        max_z = z;
                }
            }
        }
    }

    /* Convex hull candidate points (stored in surface_voxels) */
    compute_hull_points(obj);

    float extent_x = (float)(max_x - min_x + 1) * obj->voxel_size * 0.5f;
    float extent_y = (float)(max_y - min_y + 1) * obj->voxel_size * 0.5f;
    float extent_z = (float)(max_z - min_z + 1) * obj->voxel_size * 0.5f;
    obj->shape_half_extents = vec3_create(extent_x, extent_y, extent_z);
    obj->shape_center = vec3_create(((float)(min_x + max_x + 1) * 0.5f - half_grid) * obj->voxel_size,
                                    ((float)(min_y + max_y + 1) * 0.5f - half_grid) * obj->voxel_size,
                                    ((float)(min_z + max_z + 1) * 0.5f - half_grid) * obj->voxel_size);

#ifndef NDEBUG
    /* DEBUG: verify shape extents are valid */
    float max_extent = half_grid * obj->voxel_size;
    assert(extent_x > 0.0f && extent_x <= max_extent);
    assert(extent_y > 0.0f && extent_y <= max_extent);
    assert(extent_z > 0.0f && extent_z <= max_extent);
#endif

    /* Compute center of mass and total mass using per-material density */
    float mass_sum = 0.0f;
    float mass_com_x = 0.0f, mass_com_y = 0.0f, mass_com_z = 0.0f;
    for (int32_t z = 0; z < VOBJ_GRID_SIZE; z++)
    {
        for (int32_t y = 0; y < VOBJ_GRID_SIZE; y++)
        {
            for (int32_t x = 0; x < VOBJ_GRID_SIZE; x++)
            {
                uint8_t mat = obj->voxels[vobj_index(x, y, z)].material;
                if (mat != 0)
                {
                    const MaterialDescriptor *desc = material_get(mat);
                    float density = (desc && desc->density > 0.0f) ? desc->density : 1.0f;
                    float px = ((float)x + 0.5f - half_grid) * obj->voxel_size;
                    float py = ((float)y + 0.5f - half_grid) * obj->voxel_size;
                    float pz = ((float)z + 0.5f - half_grid) * obj->voxel_size;
                    mass_sum += density;
                    mass_com_x += density * px;
                    mass_com_y += density * py;
                    mass_com_z += density * pz;
                }
            }
        }
    }

    if (mass_sum > 0.0f)
    {
        float inv_mass = 1.0f / mass_sum;
        obj->local_com = vec3_create(mass_com_x * inv_mass,
                                      mass_com_y * inv_mass,
                                      mass_com_z * inv_mass);
    }
    else
    {
        obj->local_com = vec3_zero();
    }
    obj->total_mass = mass_sum;

    /* Compute inertia tensor about COM using parallel-axis theorem */
    float Ixx = 0.0f, Iyy = 0.0f, Izz = 0.0f;
    float vs2 = obj->voxel_size * obj->voxel_size;
    float voxel_inertia = vs2 / 6.0f; /* single voxel I = m * s^2 / 6 for each axis */
    for (int32_t z = 0; z < VOBJ_GRID_SIZE; z++)
    {
        for (int32_t y = 0; y < VOBJ_GRID_SIZE; y++)
        {
            for (int32_t x = 0; x < VOBJ_GRID_SIZE; x++)
            {
                uint8_t mat = obj->voxels[vobj_index(x, y, z)].material;
                if (mat != 0)
                {
                    const MaterialDescriptor *desc = material_get(mat);
                    float density = (desc && desc->density > 0.0f) ? desc->density : 1.0f;
                    float rx = ((float)x + 0.5f - half_grid) * obj->voxel_size - obj->local_com.x;
                    float ry = ((float)y + 0.5f - half_grid) * obj->voxel_size - obj->local_com.y;
                    float rz = ((float)z + 0.5f - half_grid) * obj->voxel_size - obj->local_com.z;
                    float self = density * voxel_inertia;
                    Ixx += self + density * (ry * ry + rz * rz);
                    Iyy += self + density * (rx * rx + rz * rz);
                    Izz += self + density * (rx * rx + ry * ry);
                }
            }
        }
    }
    obj->inertia_diag = vec3_create(Ixx, Iyy, Izz);

    /* Radius: calculate from GRID CENTER (which corresponds to obj->position) to corners.
     * This is critical for split objects where voxels may be off-center in the grid.
     * The raycast bounding sphere test uses obj->position, not COM. */
    float grid_center = (float)VOBJ_GRID_SIZE * 0.5f;
    float max_dist_sq = 0.0f;
    for (int32_t c = 0; c < 8; c++)
    {
        float cx = ((c & 1) ? (float)max_x + 1.0f : (float)min_x);
        float cy = ((c & 2) ? (float)max_y + 1.0f : (float)min_y);
        float cz = ((c & 4) ? (float)max_z + 1.0f : (float)min_z);
        float dx = (cx - grid_center) * obj->voxel_size;
        float dy = (cy - grid_center) * obj->voxel_size;
        float dz = (cz - grid_center) * obj->voxel_size;
        float dist_sq = dx * dx + dy * dy + dz * dz;
        if (dist_sq > max_dist_sq)
            max_dist_sq = dist_sq;
    }
    obj->radius = sqrtf(max_dist_sq);

    /* The bricks start at the box's min corner, or earlier where its last brick would reach
     * past the grid: the voxels take the fewest bricks (ceil(extent / 8) per axis, however
     * the box sits in the grid), all of them inside the grid */
    const int32_t box_min[3] = {min_x, min_y, min_z};
    const int32_t box_max[3] = {max_x, max_y, max_z};
    bool offset_moved = false;
    for (int32_t a = 0; a < 3; a++)
    {
        const int32_t bricks = (box_max[a] - box_min[a]) / VOBJ_BRICK_SIZE + 1;
        const int32_t max_offset = VOBJ_GRID_SIZE - bricks * VOBJ_BRICK_SIZE;
        const uint8_t offset = (uint8_t)(box_min[a] < max_offset ? box_min[a] : max_offset);
        if (offset != obj->brick_offset[a])
            offset_moved = true;
        obj->brick_offset[a] = offset;
    }

    /* Shape data changed: hull cache, physics mass sync and GPU upload all key on the
     * revision. Recentering moved the voxels of every brick; a new offset moved every brick. */
    voxel_object_mark_changed(obj, recentered || offset_moved ? VOBJ_ALL_BRICKS : 0);
}

void voxel_object_world_mark_dirty(VoxelObjectWorld *world, int32_t obj_index)
{
    if (!world || obj_index < 0 || obj_index >= world->object_count)
        return;

    VoxelObject *obj = &world->objects[obj_index];
    if (obj->shape_dirty)
        return; /* Already in dirty list */

    obj->shape_dirty = true;
    obj->next_dirty = world->first_dirty;
    world->first_dirty = obj_index;
}

void voxel_object_world_free_slot(VoxelObjectWorld *world, int32_t slot)
{
    if (!world || slot < 0 || slot >= world->object_count)
        return;

    VoxelObject *obj = &world->objects[slot];
    obj->active = false;

    /* Already free, or still linked in the dirty list (process_recalcs unlinks it
     * and frees it then). Pushing it again would hand the same slot out twice. */
    if (obj->in_free_list || obj->shape_dirty)
        return;

    obj->in_free_list = true;
    obj->next_free = world->first_free_slot;
    world->first_free_slot = slot;
}

VoxelObjectWorld *voxel_object_world_create(Bounds3D bounds, float voxel_size)
{
    VoxelObjectWorld *world = (VoxelObjectWorld *)calloc(1, sizeof(VoxelObjectWorld));
    if (!world)
        return NULL;

    world->bounds = bounds;
    world->voxel_size = voxel_size;
    world->object_count = 0;
    world->first_free_slot = -1;
    world->first_dirty = -1;

    world->bvh = bvh_create();

    return world;
}

void voxel_object_world_destroy(VoxelObjectWorld *world)
{
    if (world)
    {
        if (world->bvh)
        {
            bvh_destroy(world->bvh);
        }
        free(world);
    }
}

int32_t voxel_object_world_add_from_voxels(VoxelObjectWorld *world,
                                           const uint8_t *voxels,
                                           int32_t size_x, int32_t size_y, int32_t size_z,
                                           Vec3 origin, float voxel_size)
{
    if (!world || !voxels || size_x <= 0 || size_y <= 0 || size_z <= 0)
        return -1;

    if (size_x > VOBJ_GRID_SIZE || size_y > VOBJ_GRID_SIZE || size_z > VOBJ_GRID_SIZE)
        return -1;

    int32_t slot = voxel_object_world_alloc_slot(world);
    if (slot < 0)
        return -1;

    VoxelObject *obj = &world->objects[slot];

    obj->voxel_size = voxel_size;
    obj->voxel_count = 0;
    obj->active = true;
    obj->orientation = quat_identity();

    int32_t offset_x = (VOBJ_GRID_SIZE - size_x) / 2;
    int32_t offset_y = (VOBJ_GRID_SIZE - size_y) / 2;
    int32_t offset_z = (VOBJ_GRID_SIZE - size_z) / 2;

    for (int32_t z = 0; z < size_z; z++)
    {
        for (int32_t y = 0; y < size_y; y++)
        {
            for (int32_t x = 0; x < size_x; x++)
            {
                int32_t src_idx = x + y * size_x + z * size_x * size_y;
                uint8_t mat = voxels[src_idx];
                if (mat == 0)
                    continue;

                int32_t dst_x = x + offset_x;
                int32_t dst_y = y + offset_y;
                int32_t dst_z = z + offset_z;
                int32_t dst_idx = vobj_index(dst_x, dst_y, dst_z);

                obj->voxels[dst_idx].material = mat;
                obj->voxel_count++;
            }
        }
    }

    if (obj->voxel_count == 0)
    {
        voxel_object_world_free_slot(world, slot);
        return -1;
    }

    obj->voxel_revision = voxel_object_next_revision();

    /* Source voxel (x, y, z) sits at grid cell (x + offset_x, ...), whose min corner is
     * (cell - VOBJ_GRID_SIZE / 2) * voxel_size from the grid centre: put the grid
     * centre where that lands every voxel on its source position. (For odd sizes this
     * is half a voxel off the source box centre.) */
    float half_grid = (float)VOBJ_GRID_SIZE * 0.5f;
    obj->position = vec3_create(origin.x + (half_grid - (float)offset_x) * voxel_size,
                                origin.y + (half_grid - (float)offset_y) * voxel_size,
                                origin.z + (half_grid - (float)offset_z) * voxel_size);
    /* A new object is drawn where it appears, not slid in from the origin */
    obj->prev_position = obj->position;
    obj->prev_orientation = obj->orientation;

    voxel_object_recalc_shape(obj);
    return slot;
}

VoxelObjectHit voxel_object_world_raycast(VoxelObjectWorld *world, Vec3 origin, Vec3 dir)
{
    PROFILE_BEGIN(PROFILE_VOXEL_RAYCAST);

    VoxelObjectHit result = {0};
    result.hit = false;
    result.object_index = -1;

    float closest_t = 1e30f;

    int32_t candidates[VOBJ_RAYCAST_MAX_CANDIDATES];
    int32_t candidate_count = 0;
    bool use_bvh = world->bvh != NULL && world->bvh->object_count > 0;

    if (use_bvh)
    {
        candidate_count = bvh_query_ray_candidates(world->bvh, origin, dir, VOBJ_RAYCAST_MAX_DIST,
                                                    candidates, VOBJ_RAYCAST_MAX_CANDIDATES);
    }

    int32_t loop_count = use_bvh ? candidate_count : world->object_count;

    for (int32_t loop_i = 0; loop_i < loop_count; loop_i++)
    {
        int32_t i = use_bvh ? candidates[loop_i] : loop_i;
        VoxelObject *obj = &world->objects[i];
        if (!obj->active || obj->voxel_count == 0)
            continue;

        Vec3 pivot = obj->position;
        Vec3 oc = vec3_sub(origin, pivot);
        float a = vec3_dot(dir, dir);
        float b = 2.0f * vec3_dot(oc, dir);
        float c = vec3_dot(oc, oc) - obj->radius * obj->radius;
        float discriminant = b * b - 4.0f * a * c;

        if (discriminant < 0.0f)
            continue;

        float sqrt_disc = sqrtf(discriminant);
        float t0 = (-b - sqrt_disc) / (2.0f * a);
        float t1 = (-b + sqrt_disc) / (2.0f * a);

        float t_sphere = t0;
        if (t_sphere < 0.0f)
            t_sphere = t1;
        if (c <= 0.0f)
            t_sphere = 0.0f;
        if (t_sphere < 0.0f || t_sphere >= closest_t)
            continue;

        Quat to_local = quat_conjugate(obj->orientation);
        Vec3 local_origin = quat_rotate_vec3(to_local, vec3_sub(origin, pivot));
        Vec3 local_dir = quat_rotate_vec3(to_local, dir);

        float half_size = obj->voxel_size * (float)VOBJ_GRID_SIZE * 0.5f;
        local_origin = vec3_add(local_origin, vec3_create(half_size, half_size, half_size));

        Vec3 inv_dir;
        inv_dir.x = (fabsf(local_dir.x) > VOBJ_DIR_EPSILON) ? 1.0f / local_dir.x : 1e10f;
        inv_dir.y = (fabsf(local_dir.y) > VOBJ_DIR_EPSILON) ? 1.0f / local_dir.y : 1e10f;
        inv_dir.z = (fabsf(local_dir.z) > VOBJ_DIR_EPSILON) ? 1.0f / local_dir.z : 1e10f;

        float t_start = fmaxf(t_sphere - obj->radius * VOBJ_SPHERE_ENTRY_BIAS, 0.0f);
        Vec3 pos = vec3_add(local_origin, vec3_scale(local_dir, t_start));

        int32_t map_x = (int32_t)floorf(pos.x / obj->voxel_size);
        int32_t map_y = (int32_t)floorf(pos.y / obj->voxel_size);
        int32_t map_z = (int32_t)floorf(pos.z / obj->voxel_size);

        int32_t step_x = (local_dir.x >= 0.0f) ? 1 : -1;
        int32_t step_y = (local_dir.y >= 0.0f) ? 1 : -1;
        int32_t step_z = (local_dir.z >= 0.0f) ? 1 : -1;

        float t_max_x = ((float)(map_x + (step_x > 0 ? 1 : 0)) * obj->voxel_size - pos.x) * inv_dir.x;
        float t_max_y = ((float)(map_y + (step_y > 0 ? 1 : 0)) * obj->voxel_size - pos.y) * inv_dir.y;
        float t_max_z = ((float)(map_z + (step_z > 0 ? 1 : 0)) * obj->voxel_size - pos.z) * inv_dir.z;

        float t_delta_x = fabsf(obj->voxel_size * inv_dir.x);
        float t_delta_y = fabsf(obj->voxel_size * inv_dir.y);
        float t_delta_z = fabsf(obj->voxel_size * inv_dir.z);

        float t_current = t_start;

        for (int32_t step = 0; step < VOBJ_DDA_MAX_STEPS; step++)
        {
            if (map_x >= 0 && map_x < VOBJ_GRID_SIZE &&
                map_y >= 0 && map_y < VOBJ_GRID_SIZE &&
                map_z >= 0 && map_z < VOBJ_GRID_SIZE)
            {
                int32_t idx = vobj_index(map_x, map_y, map_z);
                if (obj->voxels[idx].material != 0)
                {
                    if (t_current < closest_t)
                    {
                        closest_t = t_current;
                        result.hit = true;
                        result.object_index = i;
                        result.impact_point = vec3_add(origin, vec3_scale(dir, t_current));
                    }
                    break;
                }
            }

            if (t_max_x < t_max_y && t_max_x < t_max_z)
            {
                t_current = t_start + t_max_x;
                t_max_x += t_delta_x;
                map_x += step_x;
            }
            else if (t_max_y < t_max_z)
            {
                t_current = t_start + t_max_y;
                t_max_y += t_delta_y;
                map_y += step_y;
            }
            else
            {
                t_current = t_start + t_max_z;
                t_max_z += t_delta_z;
                map_z += step_z;
            }

            if (t_current > closest_t)
                break;
        }
    }

    PROFILE_END(PROFILE_VOXEL_RAYCAST);
    return result;
}

VoxelObjectPointTest voxel_object_world_test_point(const VoxelObjectWorld *world, Vec3 world_pos)
{
    VoxelObjectPointTest result = {0};
    result.hit = false;
    result.object_index = -1;
    result.surface_normal = vec3_create(0.0f, 1.0f, 0.0f);

    if (!world)
        return result;

    int32_t candidate_count = 0;
    bool use_bvh = world->bvh != NULL && world->bvh->object_count > 0;

    BVHQueryResult query_result = {0};
    if (use_bvh)
    {
        query_result = bvh_query_sphere(world->bvh, world_pos, 0.01f);
        candidate_count = query_result.count;
    }

    int32_t loop_count = use_bvh ? candidate_count : world->object_count;

    for (int32_t loop_i = 0; loop_i < loop_count; loop_i++)
    {
        int32_t i = use_bvh ? query_result.indices[loop_i] : loop_i;
        const VoxelObject *obj = &world->objects[i];
        if (!obj->active || obj->voxel_count == 0)
            continue;

        Vec3 to_obj = vec3_sub(world_pos, obj->position);
        if (vec3_length_sq(to_obj) > obj->radius * obj->radius)
            continue;

        float rot_mat[9], inv_rot_mat[9];
        quat_to_mat3(obj->orientation, rot_mat);
        mat3_transpose(rot_mat, inv_rot_mat);

        Vec3 local_pos = mat3_transform_vec3(inv_rot_mat, to_obj);
        float half_size = obj->voxel_size * (float)VOBJ_GRID_SIZE * 0.5f;
        local_pos = vec3_add(local_pos, vec3_create(half_size, half_size, half_size));

        int32_t gx = (int32_t)floorf(local_pos.x / obj->voxel_size);
        int32_t gy = (int32_t)floorf(local_pos.y / obj->voxel_size);
        int32_t gz = (int32_t)floorf(local_pos.z / obj->voxel_size);

        if (gx < 0 || gx >= VOBJ_GRID_SIZE ||
            gy < 0 || gy >= VOBJ_GRID_SIZE ||
            gz < 0 || gz >= VOBJ_GRID_SIZE)
            continue;

        if (obj->voxels[vobj_index(gx, gy, gz)].material == 0)
            continue;

        result.hit = true;
        result.object_index = i;

        /* Estimate surface normal from 6-neighbor probe in local grid */
        Vec3 local_normal = vec3_zero();
        if (gx + 1 >= VOBJ_GRID_SIZE || obj->voxels[vobj_index(gx + 1, gy, gz)].material == 0)
            local_normal.x += 1.0f;
        if (gx - 1 < 0 || obj->voxels[vobj_index(gx - 1, gy, gz)].material == 0)
            local_normal.x -= 1.0f;
        if (gy + 1 >= VOBJ_GRID_SIZE || obj->voxels[vobj_index(gx, gy + 1, gz)].material == 0)
            local_normal.y += 1.0f;
        if (gy - 1 < 0 || obj->voxels[vobj_index(gx, gy - 1, gz)].material == 0)
            local_normal.y -= 1.0f;
        if (gz + 1 >= VOBJ_GRID_SIZE || obj->voxels[vobj_index(gx, gy, gz + 1)].material == 0)
            local_normal.z += 1.0f;
        if (gz - 1 < 0 || obj->voxels[vobj_index(gx, gy, gz - 1)].material == 0)
            local_normal.z -= 1.0f;

        float len = vec3_length(local_normal);
        if (len > 0.001f)
            local_normal = vec3_scale(local_normal, 1.0f / len);
        else
            local_normal = vec3_create(0.0f, 1.0f, 0.0f);

        result.surface_normal = mat3_transform_vec3(rot_mat, local_normal);
        return result;
    }

    return result;
}

void voxel_object_world_update_bvh(VoxelObjectWorld *world)
{
    if (!world || !world->bvh)
        return;

    if (bvh_needs_rebuild(world->bvh, world))
        bvh_build(world->bvh, world, NULL);
    else
        bvh_refit(world->bvh, world, NULL);
}

void voxel_object_world_queue_split(VoxelObjectWorld *world, int32_t obj_index)
{
    if (!world || obj_index < 0 || obj_index >= world->object_count)
        return;

    VoxelObject *obj = &world->objects[obj_index];
    if (obj->split_queued)
        return;

    int32_t next_tail = (world->split_queue_tail + 1) % VOBJ_SPLIT_QUEUE_SIZE;
    if (next_tail == world->split_queue_head)
        return; /* Queue full */

    world->split_queue[world->split_queue_tail] = obj_index;
    world->split_queue_tail = next_tail;
    obj->split_queued = true;
}

void voxel_object_world_tick_render_delays(VoxelObjectWorld *world)
{
    if (!world)
        return;
    for (int32_t i = 0; i < world->object_count; i++)
    {
        if (world->objects[i].active && world->objects[i].render_delay > 0)
            world->objects[i].render_delay--;
    }
}

void voxel_object_world_begin_tick(VoxelObjectWorld *world)
{
    if (!world)
        return;
    for (int32_t i = 0; i < world->object_count; i++)
    {
        VoxelObject *obj = &world->objects[i];
        obj->prev_position = obj->position;
        obj->prev_orientation = obj->orientation;
    }
}

void voxel_object_world_process_recalcs(VoxelObjectWorld *world)
{
    if (!world)
        return;

    PROFILE_BEGIN(PROFILE_SIM_VOXEL_UPDATE);

    /* Pop entries off the dirty list. Clearing shape_dirty here, and only here, keeps
     * it equal to "linked": mark_dirty never links an object twice and free_slot
     * never hands out a slot that is still linked. */
    int32_t processed = 0;
    while (world->first_dirty >= 0 && processed < VOBJ_MAX_RECALCS_PER_TICK)
    {
        int32_t idx = world->first_dirty;
        VoxelObject *obj = &world->objects[idx];
        world->first_dirty = obj->next_dirty;
        obj->next_dirty = -1;
        obj->shape_dirty = false;

        if (obj->active)
        {
            voxel_object_recalc_shape(obj);
            processed++;
        }

        /* Freed while queued, or emptied (recalc_shape deactivates at 0 voxels) */
        if (!obj->active)
            voxel_object_world_free_slot(world, idx);
    }

    PROFILE_END(PROFILE_SIM_VOXEL_UPDATE);
}

/* Island splitting works on rows: bit x of rows[y + z * VOBJ_GRID_SIZE] is voxel
 * (x, y, z). A mask of the whole grid is 4 KB, so the scratch lives on the stack and
 * objects of different worlds can be split on different threads at once. */
#define VOBJ_ROW_COUNT (VOBJ_GRID_SIZE * VOBJ_GRID_SIZE)
STATIC_ASSERT(VOBJ_GRID_SIZE == 32, "voxel rows are 32-bit masks");

static void build_solid_rows(const VoxelObject *obj, uint32_t rows[VOBJ_ROW_COUNT])
{
    for (int32_t r = 0; r < VOBJ_ROW_COUNT; r++)
    {
        const VObjVoxel *row = &obj->voxels[r * VOBJ_GRID_SIZE];
        uint32_t bits = 0;
        for (int32_t x = 0; x < VOBJ_GRID_SIZE; x++)
            bits |= (uint32_t)(row[x].material != 0) << x;
        rows[r] = bits;
    }
}

/* The seed bits grown along x through the runs of solid they lie in */
static uint32_t fill_row_runs(uint32_t seeds, uint32_t solid)
{
    uint32_t grown = seeds;
    do
    {
        seeds = grown;
        grown = seeds | (((seeds << 1) | (seeds >> 1)) & solid);
    } while (grown != seeds);
    return grown;
}

/* Marks in reached every solid voxel face-connected to voxel seed_x of row seed_row.
 * A row whose reach grew waits on a stack until its runs are filled and passed on to
 * the four neighbouring rows (y +-1, z +-1). */
static void fill_component(const uint32_t solid[VOBJ_ROW_COUNT], uint32_t reached[VOBJ_ROW_COUNT],
                           int32_t seed_row, int32_t seed_x)
{
    uint16_t pending[VOBJ_ROW_COUNT];
    uint8_t is_pending[VOBJ_ROW_COUNT];
    memset(is_pending, 0, sizeof(is_pending));
    int32_t pending_count = 0;

    reached[seed_row] |= 1u << seed_x;
    pending[pending_count++] = (uint16_t)seed_row;
    is_pending[seed_row] = 1;

    while (pending_count > 0)
    {
        int32_t r = pending[--pending_count];
        is_pending[r] = 0;
        uint32_t bits = fill_row_runs(reached[r], solid[r]);
        reached[r] = bits;

        int32_t y = r % VOBJ_GRID_SIZE;
        int32_t z = r / VOBJ_GRID_SIZE;
        int32_t neighbours[4];
        int32_t neighbour_count = 0;
        if (y > 0)
            neighbours[neighbour_count++] = r - 1;
        if (y < VOBJ_GRID_SIZE - 1)
            neighbours[neighbour_count++] = r + 1;
        if (z > 0)
            neighbours[neighbour_count++] = r - VOBJ_GRID_SIZE;
        if (z < VOBJ_GRID_SIZE - 1)
            neighbours[neighbour_count++] = r + VOBJ_GRID_SIZE;

        for (int32_t i = 0; i < neighbour_count; i++)
        {
            int32_t n = neighbours[i];
            uint32_t added = bits & solid[n] & ~reached[n];
            if (added == 0)
                continue;
            reached[n] |= added;
            if (!is_pending[n])
            {
                is_pending[n] = 1;
                pending[pending_count++] = (uint16_t)n;
            }
        }
    }
}

static bool split_one_island(VoxelObjectWorld *world, int32_t obj_index)
{
    if (obj_index < 0 || obj_index >= world->object_count)
        return false;

    VoxelObject *obj = &world->objects[obj_index];
    if (!obj->active || obj->voxel_count <= 1)
        return false;

    uint32_t solid[VOBJ_ROW_COUNT];
    uint32_t reached[VOBJ_ROW_COUNT];
    build_solid_rows(obj, solid);
    memset(reached, 0, sizeof(reached));

    /* The component of the first voxel in index order stays; the rest moves out */
    int32_t first_row = 0;
    while (first_row < VOBJ_ROW_COUNT && solid[first_row] == 0)
        first_row++;
    if (first_row == VOBJ_ROW_COUNT)
        return false;
    int32_t first_x = 0;
    while (!(solid[first_row] & (1u << first_x)))
        first_x++;

    fill_component(solid, reached, first_row, first_x);

    bool detached = false;
    for (int32_t r = 0; r < VOBJ_ROW_COUNT && !detached; r++)
        detached = (solid[r] & ~reached[r]) != 0;
    if (!detached)
        return false;

    int32_t new_obj_idx = voxel_object_world_alloc_slot(world);
    if (new_obj_idx < 0)
        return false;

    VoxelObject *new_obj = &world->objects[new_obj_idx];
    new_obj->position = obj->position;
    new_obj->orientation = obj->orientation;
    new_obj->prev_position = obj->prev_position;
    new_obj->prev_orientation = obj->prev_orientation;
    new_obj->voxel_size = obj->voxel_size;
    new_obj->active = true;
    new_obj->voxel_count = 0;
    new_obj->split_source = obj_index + 1;

    uint64_t moved_bricks = 0;
    for (int32_t r = 0; r < VOBJ_ROW_COUNT; r++)
    {
        uint32_t moving = solid[r] & ~reached[r];
        for (int32_t x = 0; moving != 0; x++, moving >>= 1)
        {
            if (!(moving & 1))
                continue;
            int32_t i = r * VOBJ_GRID_SIZE + x;
            new_obj->voxels[i].material = obj->voxels[i].material;
            new_obj->voxel_count++;
            obj->voxels[i].material = 0;
            obj->voxel_count--;
            moved_bricks |= 1ull << vobj_brick_index(obj, x, r % VOBJ_GRID_SIZE, r / VOBJ_GRID_SIZE);
        }
    }

    voxel_object_mark_changed(obj, moved_bricks);
    new_obj->voxel_revision = voxel_object_next_revision();

    voxel_object_recalc_shape(new_obj);
    voxel_object_recalc_shape(obj);

    /* Queue both for further splitting */
    voxel_object_world_queue_split(world, obj_index);
    voxel_object_world_queue_split(world, new_obj_idx);

    return true;
}

void voxel_object_world_process_splits(VoxelObjectWorld *world)
{
    if (!world)
        return;

    PROFILE_BEGIN(PROFILE_SIM_VOXEL_UPDATE);

    int32_t processed = 0;
    while (world->split_queue_head != world->split_queue_tail &&
           processed < VOBJ_MAX_SPLITS_PER_TICK)
    {
        int32_t obj_index = world->split_queue[world->split_queue_head];
        world->split_queue_head = (world->split_queue_head + 1) % VOBJ_SPLIT_QUEUE_SIZE;
        world->objects[obj_index].split_queued = false;

        if (split_one_island(world, obj_index))
        {
            processed++;
        }
    }

    PROFILE_END(PROFILE_SIM_VOXEL_UPDATE);
}
