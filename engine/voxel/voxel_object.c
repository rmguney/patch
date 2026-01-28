#include "voxel_object.h"
#include "content/materials.h"
#include "engine/core/profile.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

int32_t voxel_object_world_alloc_slot(VoxelObjectWorld *world)
{
    /* O(1) allocation via free-list */
    if (world->first_free_slot >= 0)
    {
        int32_t slot = world->first_free_slot;
        world->first_free_slot = world->objects[slot].next_free;
        world->objects[slot].next_free = -1;
        return slot;
    }
    if (world->object_count >= VOBJ_MAX_OBJECTS)
        return -1;
    return world->object_count++;
}

static bool voxel_has_empty_neighbor(const VoxelObject *obj, int32_t x, int32_t y, int32_t z)
{
    static const int32_t dx[6] = {-1, 1, 0, 0, 0, 0};
    static const int32_t dy[6] = {0, 0, -1, 1, 0, 0};
    static const int32_t dz[6] = {0, 0, 0, 0, -1, 1};

    for (int32_t i = 0; i < 6; i++)
    {
        int32_t nx = x + dx[i];
        int32_t ny = y + dy[i];
        int32_t nz = z + dz[i];

        if (nx < 0 || nx >= VOBJ_GRID_SIZE ||
            ny < 0 || ny >= VOBJ_GRID_SIZE ||
            nz < 0 || nz >= VOBJ_GRID_SIZE)
        {
            return true;
        }

        if (obj->voxels[vobj_index(nx, ny, nz)].material == 0)
        {
            return true;
        }
    }
    return false;
}

static void compute_collider_boxes(VoxelObject *obj)
{
    obj->collider_box_count = 0;

    uint8_t assigned[VOBJ_TOTAL_VOXELS];
    memset(assigned, 0, sizeof(assigned));

    float half_grid = (float)VOBJ_GRID_SIZE * 0.5f;
    float vs = obj->voxel_size;

    for (int32_t z = 0; z < VOBJ_GRID_SIZE; z++)
    {
        for (int32_t y = 0; y < VOBJ_GRID_SIZE; y++)
        {
            for (int32_t x = 0; x < VOBJ_GRID_SIZE; x++)
            {
                int32_t idx = vobj_index(x, y, z);
                if (obj->voxels[idx].material == 0 || assigned[idx])
                    continue;

                if (obj->collider_box_count >= VOBJ_MAX_COLLIDER_BOXES)
                    return;

                int32_t ex = x;
                while (ex + 1 < VOBJ_GRID_SIZE)
                {
                    int32_t ni = vobj_index(ex + 1, y, z);
                    if (obj->voxels[ni].material == 0 || assigned[ni])
                        break;
                    ex++;
                }

                int32_t ey = y;
                while (ey + 1 < VOBJ_GRID_SIZE)
                {
                    bool row_ok = true;
                    for (int32_t ix = x; ix <= ex; ix++)
                    {
                        int32_t ni = vobj_index(ix, ey + 1, z);
                        if (obj->voxels[ni].material == 0 || assigned[ni])
                        {
                            row_ok = false;
                            break;
                        }
                    }
                    if (!row_ok)
                        break;
                    ey++;
                }

                int32_t ez = z;
                while (ez + 1 < VOBJ_GRID_SIZE)
                {
                    bool plane_ok = true;
                    for (int32_t iy = y; iy <= ey && plane_ok; iy++)
                    {
                        for (int32_t ix = x; ix <= ex; ix++)
                        {
                            int32_t ni = vobj_index(ix, iy, ez + 1);
                            if (obj->voxels[ni].material == 0 || assigned[ni])
                            {
                                plane_ok = false;
                                break;
                            }
                        }
                    }
                    if (!plane_ok)
                        break;
                    ez++;
                }

                for (int32_t iz = z; iz <= ez; iz++)
                    for (int32_t iy = y; iy <= ey; iy++)
                        for (int32_t ix = x; ix <= ex; ix++)
                            assigned[vobj_index(ix, iy, iz)] = 1;

                ColliderBox *box = &obj->collider_boxes[obj->collider_box_count++];
                box->local_min = vec3_create(
                    ((float)x - half_grid) * vs,
                    ((float)y - half_grid) * vs,
                    ((float)z - half_grid) * vs);
                box->local_max = vec3_create(
                    ((float)(ex + 1) - half_grid) * vs,
                    ((float)(ey + 1) - half_grid) * vs,
                    ((float)(ez + 1) - half_grid) * vs);
            }
        }
    }
}

void voxel_object_recalc_shape(VoxelObject *obj)
{
    if (obj->voxel_count <= 0)
    {
        obj->active = false;
        obj->shape_dirty = false;
        obj->surface_voxel_count = 0;
        obj->collider_box_count = 0;
        return;
    }

    /* Recenter voxels in the 32³ grid so OBB/collision shapes align with obj->position.
     * After splits or destruction, voxels may be clustered in one corner of the grid.
     * The OBB and terrain sample points assume voxels are centered around grid position 16. */
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
        }
    }

    int32_t min_x = VOBJ_GRID_SIZE, max_x = 0;
    int32_t min_y = VOBJ_GRID_SIZE, max_y = 0;
    int32_t min_z = VOBJ_GRID_SIZE, max_z = 0;
    float com_x = 0.0f, com_y = 0.0f, com_z = 0.0f;
    int32_t counted = 0;
    uint8_t occupancy = 0;

    obj->surface_voxel_count = 0;
    float half_grid = (float)VOBJ_GRID_SIZE * 0.5f;

    /* Single pass: bounds, center of mass, occupancy, and surface voxels */
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
                    com_x += (float)x + 0.5f;
                    com_y += (float)y + 0.5f;
                    com_z += (float)z + 0.5f;
                    counted++;

                    /* Occupancy: which region contains this voxel (2×2×2 regions) */
                    int32_t region_size = VOBJ_GRID_SIZE / 2;
                    int32_t region = (x / region_size) + ((y / region_size) * 2) + ((z / region_size) * 4);
                    occupancy |= (uint8_t)(1 << region);

                    /* Extract surface voxels for convex hull.
                     * Push position to voxel boundary along exposed faces
                     * so the convex hull matches actual voxel extents. */
                    if (obj->surface_voxel_count < VOBJ_MAX_SURFACE_VOXELS &&
                        voxel_has_empty_neighbor(obj, x, y, z))
                    {
                        bool neg_x = (x == 0 || obj->voxels[vobj_index(x - 1, y, z)].material == 0);
                        bool pos_x = (x >= VOBJ_GRID_SIZE - 1 || obj->voxels[vobj_index(x + 1, y, z)].material == 0);
                        bool neg_y = (y == 0 || obj->voxels[vobj_index(x, y - 1, z)].material == 0);
                        bool pos_y = (y >= VOBJ_GRID_SIZE - 1 || obj->voxels[vobj_index(x, y + 1, z)].material == 0);
                        bool neg_z = (z == 0 || obj->voxels[vobj_index(x, y, z - 1)].material == 0);
                        bool pos_z = (z >= VOBJ_GRID_SIZE - 1 || obj->voxels[vobj_index(x, y, z + 1)].material == 0);

                        float ox = (pos_x && !neg_x) ? 1.0f : (neg_x && !pos_x) ? 0.0f : 0.5f;
                        float oy = (pos_y && !neg_y) ? 1.0f : (neg_y && !pos_y) ? 0.0f : 0.5f;
                        float oz = (pos_z && !neg_z) ? 1.0f : (neg_z && !pos_z) ? 0.0f : 0.5f;

                        Vec3 local_pos = vec3_create(
                            ((float)x + ox - half_grid) * obj->voxel_size,
                            ((float)y + oy - half_grid) * obj->voxel_size,
                            ((float)z + oz - half_grid) * obj->voxel_size);
                        obj->surface_voxels[obj->surface_voxel_count++] = local_pos;
                    }
                }
            }
        }
    }

    obj->occupancy_mask = occupancy;

    float extent_x = (float)(max_x - min_x + 1) * obj->voxel_size * 0.5f;
    float extent_y = (float)(max_y - min_y + 1) * obj->voxel_size * 0.5f;
    float extent_z = (float)(max_z - min_z + 1) * obj->voxel_size * 0.5f;
    obj->shape_half_extents = vec3_create(extent_x, extent_y, extent_z);

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

    float inv_count = 1.0f / (float)counted;
    com_x *= inv_count;
    com_y *= inv_count;
    com_z *= inv_count;

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

    compute_collider_boxes(obj);

    obj->shape_dirty = false;
}

void voxel_object_mark_dirty(VoxelObject *obj)
{
    obj->shape_dirty = true;
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
    world->dirty_count++;
}

void voxel_object_world_free_slot(VoxelObjectWorld *world, int32_t slot)
{
    if (!world || slot < 0 || slot >= world->object_count)
        return;

    VoxelObject *obj = &world->objects[slot];
    obj->active = false;
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
    world->terrain = NULL;
    world->first_free_slot = -1;
    world->first_dirty = -1;
    world->dirty_count = 0;

    world->raycast_grid = (SpatialHashGrid *)calloc(1, sizeof(SpatialHashGrid));
    if (world->raycast_grid)
    {
        spatial_hash_init(world->raycast_grid, VOBJ_RAYCAST_CELL_SIZE, bounds);
    }
    world->raycast_grid_valid = false;

    return world;
}

void voxel_object_world_destroy(VoxelObjectWorld *world)
{
    if (world)
    {
        if (world->raycast_grid)
        {
            free(world->raycast_grid);
        }
        free(world);
    }
}

void voxel_object_world_set_terrain(VoxelObjectWorld *world, VoxelVolume *terrain)
{
    if (world)
    {
        world->terrain = terrain;
    }
}

int32_t voxel_object_world_add_sphere(VoxelObjectWorld *world, Vec3 position,
                                      float radius, uint8_t material)
{
    int32_t slot = voxel_object_world_alloc_slot(world);
    if (slot < 0)
        return -1;

    VoxelObject *obj = &world->objects[slot];
    memset(obj, 0, sizeof(VoxelObject));

    obj->position = position;
    obj->orientation = quat_identity();
    obj->active = true;

    obj->voxel_size = world->voxel_size;
    obj->voxel_count = 0;

    float half_grid = (float)VOBJ_GRID_SIZE * 0.5f;
    float r_voxels = radius / obj->voxel_size;

    for (int32_t z = 0; z < VOBJ_GRID_SIZE; z++)
    {
        for (int32_t y = 0; y < VOBJ_GRID_SIZE; y++)
        {
            for (int32_t x = 0; x < VOBJ_GRID_SIZE; x++)
            {
                float dx = (float)x - half_grid + 0.5f;
                float dy = (float)y - half_grid + 0.5f;
                float dz = (float)z - half_grid + 0.5f;
                float dist = sqrtf(dx * dx + dy * dy + dz * dz);

                int32_t idx = vobj_index(x, y, z);
                if (dist <= r_voxels)
                {
                    obj->voxels[idx].material = material;
                    obj->voxel_count++;
                }
            }
        }
    }

    obj->voxel_revision = 1;

    voxel_object_recalc_shape(obj);
    return slot;
}

int32_t voxel_object_world_add_box(VoxelObjectWorld *world, Vec3 position,
                                   Vec3 half_extents, uint8_t material)
{
    int32_t slot = voxel_object_world_alloc_slot(world);
    if (slot < 0)
        return -1;

    VoxelObject *obj = &world->objects[slot];
    memset(obj, 0, sizeof(VoxelObject));

    obj->position = position;
    obj->orientation = quat_identity();
    obj->active = true;

    obj->voxel_size = world->voxel_size;
    obj->voxel_count = 0;

    float half_grid = (float)VOBJ_GRID_SIZE * 0.5f;

    for (int32_t z = 0; z < VOBJ_GRID_SIZE; z++)
    {
        for (int32_t y = 0; y < VOBJ_GRID_SIZE; y++)
        {
            for (int32_t x = 0; x < VOBJ_GRID_SIZE; x++)
            {
                float dx = ((float)x - half_grid + 0.5f) * obj->voxel_size;
                float dy = ((float)y - half_grid + 0.5f) * obj->voxel_size;
                float dz = ((float)z - half_grid + 0.5f) * obj->voxel_size;

                int32_t idx = vobj_index(x, y, z);
                if (fabsf(dx) <= half_extents.x &&
                    fabsf(dy) <= half_extents.y &&
                    fabsf(dz) <= half_extents.z)
                {
                    obj->voxels[idx].material = material;
                    obj->voxel_count++;
                }
            }
        }
    }

    obj->voxel_revision = 1;

    voxel_object_recalc_shape(obj);
    return slot;
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
    memset(obj, 0, sizeof(VoxelObject));

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
        obj->active = false;
        return -1;
    }

    obj->voxel_revision = 1;

    float src_center_x = origin.x + (float)size_x * voxel_size * 0.5f;
    float src_center_y = origin.y + (float)size_y * voxel_size * 0.5f;
    float src_center_z = origin.z + (float)size_z * voxel_size * 0.5f;

    obj->position = vec3_create(src_center_x, src_center_y, src_center_z);

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
    bool use_grid = world->raycast_grid_valid && world->raycast_grid != NULL;

    if (use_grid)
    {
        float step_size = VOBJ_RAYCAST_QUERY_RADIUS * VOBJ_RAYCAST_STEP_MULT;

        for (float t = 0.0f; t < VOBJ_RAYCAST_MAX_DIST && candidate_count < VOBJ_RAYCAST_MAX_CANDIDATES; t += step_size)
        {
            Vec3 sample_pos = vec3_add(origin, vec3_scale(dir, t));
            int32_t found[VOBJ_RAYCAST_PER_QUERY_MAX];
            int32_t found_count = spatial_hash_query(world->raycast_grid, sample_pos, VOBJ_RAYCAST_QUERY_RADIUS,
                                                     found, VOBJ_RAYCAST_PER_QUERY_MAX);
            for (int32_t j = 0; j < found_count && candidate_count < VOBJ_RAYCAST_MAX_CANDIDATES; j++)
            {
                bool already_added = false;
                for (int32_t k = 0; k < candidate_count; k++)
                {
                    if (candidates[k] == found[j])
                    {
                        already_added = true;
                        break;
                    }
                }
                if (!already_added)
                {
                    candidates[candidate_count++] = found[j];
                }
            }
        }
    }

    int32_t loop_count = use_grid ? candidate_count : world->object_count;

    for (int32_t loop_i = 0; loop_i < loop_count; loop_i++)
    {
        int32_t i = use_grid ? candidates[loop_i] : loop_i;
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

        float rot_mat[9], inv_rot_mat[9];
        quat_to_mat3(obj->orientation, rot_mat);
        mat3_transpose(rot_mat, inv_rot_mat);

        Vec3 local_origin = mat3_transform_vec3(inv_rot_mat, vec3_sub(origin, pivot));
        Vec3 local_dir = mat3_transform_vec3(inv_rot_mat, dir);

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
        Vec3 hit_normal = vec3_zero();

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
                        result.impact_normal = mat3_transform_vec3(rot_mat, hit_normal);
                        result.impact_normal_local = hit_normal;
                        result.voxel_x = map_x;
                        result.voxel_y = map_y;
                        result.voxel_z = map_z;
                    }
                    break;
                }
            }

            if (t_max_x < t_max_y && t_max_x < t_max_z)
            {
                t_current = t_start + t_max_x;
                t_max_x += t_delta_x;
                map_x += step_x;
                hit_normal = vec3_create((float)(-step_x), 0.0f, 0.0f);
            }
            else if (t_max_y < t_max_z)
            {
                t_current = t_start + t_max_y;
                t_max_y += t_delta_y;
                map_y += step_y;
                hit_normal = vec3_create(0.0f, (float)(-step_y), 0.0f);
            }
            else
            {
                t_current = t_start + t_max_z;
                t_max_z += t_delta_z;
                map_z += step_z;
                hit_normal = vec3_create(0.0f, 0.0f, (float)(-step_z));
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

    int32_t candidates[VOBJ_RAYCAST_PER_QUERY_MAX];
    int32_t candidate_count = 0;
    bool use_grid = world->raycast_grid_valid && world->raycast_grid != NULL;

    if (use_grid)
    {
        candidate_count = spatial_hash_query(world->raycast_grid, world_pos,
                                             0.0f, candidates, VOBJ_RAYCAST_PER_QUERY_MAX);
    }

    int32_t loop_count = use_grid ? candidate_count : world->object_count;

    for (int32_t loop_i = 0; loop_i < loop_count; loop_i++)
    {
        int32_t i = use_grid ? candidates[loop_i] : loop_i;
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

void voxel_object_world_update_raycast_grid(VoxelObjectWorld *world)
{
    if (!world || !world->raycast_grid)
        return;

    spatial_hash_clear(world->raycast_grid);

    for (int32_t i = 0; i < world->object_count; i++)
    {
        VoxelObject *obj = &world->objects[i];
        if (obj->active && obj->voxel_count > 0)
        {
            spatial_hash_insert(world->raycast_grid, i, obj->position, obj->radius);
        }
    }

    world->raycast_grid_valid = true;
}

void voxel_object_world_queue_split(VoxelObjectWorld *world, int32_t obj_index)
{
    if (!world || obj_index < 0 || obj_index >= world->object_count)
        return;

    int32_t next_tail = (world->split_queue_tail + 1) % VOBJ_SPLIT_QUEUE_SIZE;
    if (next_tail == world->split_queue_head)
        return; /* Queue full */

    world->split_queue[world->split_queue_tail] = obj_index;
    world->split_queue_tail = next_tail;
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

void voxel_object_world_process_recalcs(VoxelObjectWorld *world)
{
    if (!world)
        return;

    PROFILE_BEGIN(PROFILE_SIM_VOXEL_UPDATE);

    /* O(1) dirty lookup via dirty-list */
    int32_t processed = 0;
    int32_t prev_idx = -1;
    int32_t curr_idx = world->first_dirty;

    while (curr_idx >= 0 && processed < VOBJ_MAX_RECALCS_PER_TICK)
    {
        VoxelObject *obj = &world->objects[curr_idx];
        int32_t next_idx = obj->next_dirty;

        if (obj->active && obj->shape_dirty)
        {
            voxel_object_recalc_shape(obj);
            obj->next_dirty = -1;
            world->dirty_count--;

            /* Remove from list */
            if (prev_idx < 0)
                world->first_dirty = next_idx;
            else
                world->objects[prev_idx].next_dirty = next_idx;

            /* recalc_shape may deactivate if voxel_count reached 0 */
            if (!obj->active)
                voxel_object_world_free_slot(world, curr_idx);

            processed++;
        }
        else
        {
            /* Inactive or clean: remove from dirty list and recycle slot */
            obj->shape_dirty = false;
            obj->next_dirty = -1;
            world->dirty_count--;

            if (prev_idx < 0)
                world->first_dirty = next_idx;
            else
                world->objects[prev_idx].next_dirty = next_idx;

            if (!obj->active)
                voxel_object_world_free_slot(world, curr_idx);
        }

        curr_idx = next_idx;
    }

    PROFILE_END(PROFILE_SIM_VOXEL_UPDATE);
}

static void flood_fill_voxels_local(const VoxelObject *obj, uint8_t *visited,
                                    int32_t start_x, int32_t start_y, int32_t start_z)
{
    static thread_local int32_t stack[VOBJ_TOTAL_VOXELS];
    int32_t stack_top = 0;

    int32_t start_idx = vobj_index(start_x, start_y, start_z);
    if (start_x < 0 || start_x >= VOBJ_GRID_SIZE ||
        start_y < 0 || start_y >= VOBJ_GRID_SIZE ||
        start_z < 0 || start_z >= VOBJ_GRID_SIZE ||
        visited[start_idx] || obj->voxels[start_idx].material == 0)
        return;

    stack[stack_top++] = start_idx;
    visited[start_idx] = 1;

    static const int32_t dx[6] = {-1, 1, 0, 0, 0, 0};
    static const int32_t dy[6] = {0, 0, -1, 1, 0, 0};
    static const int32_t dz[6] = {0, 0, 0, 0, -1, 1};

    while (stack_top > 0)
    {
        int32_t idx = stack[--stack_top];
        int32_t x, y, z;
        vobj_coords(idx, &x, &y, &z);

        for (int32_t i = 0; i < 6; i++)
        {
            int32_t nx = x + dx[i];
            int32_t ny = y + dy[i];
            int32_t nz = z + dz[i];

            if (nx < 0 || nx >= VOBJ_GRID_SIZE ||
                ny < 0 || ny >= VOBJ_GRID_SIZE ||
                nz < 0 || nz >= VOBJ_GRID_SIZE)
                continue;

            int32_t nidx = vobj_index(nx, ny, nz);
            if (visited[nidx] || obj->voxels[nidx].material == 0)
                continue;

            /* Bounds check: prevent stack overflow */
            if (stack_top >= VOBJ_TOTAL_VOXELS)
                continue;

            visited[nidx] = 1;
            stack[stack_top++] = nidx;
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

    uint8_t visited[VOBJ_TOTAL_VOXELS] = {0};

    int32_t first_x = -1, first_y = -1, first_z = -1;
    for (int32_t i = 0; i < VOBJ_TOTAL_VOXELS && first_x < 0; i++)
    {
        if (obj->voxels[i].material != 0)
        {
            vobj_coords(i, &first_x, &first_y, &first_z);
        }
    }
    if (first_x < 0)
        return false;

    flood_fill_voxels_local(obj, visited, first_x, first_y, first_z);

    int32_t unvisited_count = 0;
    for (int32_t i = 0; i < VOBJ_TOTAL_VOXELS; i++)
    {
        if (obj->voxels[i].material != 0 && !visited[i])
        {
            unvisited_count++;
        }
    }
    if (unvisited_count == 0)
        return false;

    int32_t new_obj_idx = voxel_object_world_alloc_slot(world);
    if (new_obj_idx < 0)
        return false;

    VoxelObject *new_obj = &world->objects[new_obj_idx];
    memset(new_obj, 0, sizeof(VoxelObject));
    new_obj->position = obj->position;
    new_obj->orientation = obj->orientation;
    new_obj->voxel_size = obj->voxel_size;
    new_obj->active = true;
    new_obj->voxel_count = 0;

    for (int32_t i = 0; i < VOBJ_TOTAL_VOXELS; i++)
    {
        if (obj->voxels[i].material != 0 && !visited[i])
        {
            new_obj->voxels[i].material = obj->voxels[i].material;
            new_obj->voxel_count++;
            obj->voxels[i].material = 0;
            obj->voxel_count--;
        }
    }

    obj->voxel_revision++;
    new_obj->voxel_revision = 1;

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

        if (split_one_island(world, obj_index))
        {
            processed++;
        }
    }

    PROFILE_END(PROFILE_SIM_VOXEL_UPDATE);
}
