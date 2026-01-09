#include "engine/sim/voxel_object.h"
#include "engine/core/profile.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define SPLIT_SEPARATION_IMPULSE 1.5f

static int32_t allocate_object_slot(VoxelObjectWorld *world)
{
    /* First try to find inactive slot */
    for (int32_t i = 0; i < world->object_count; i++)
    {
        if (!world->objects[i].active)
        {
            return i;
        }
    }
    /* Allocate new slot if available */
    if (world->object_count >= VOBJ_MAX_OBJECTS)
        return -1;
    return world->object_count++;
}

static void recalc_object_shape(VoxelObject *obj)
{
    if (obj->voxel_count <= 0)
    {
        obj->active = false;
        return;
    }

    int32_t min_x = VOBJ_GRID_SIZE, max_x = 0;
    int32_t min_y = VOBJ_GRID_SIZE, max_y = 0;
    int32_t min_z = VOBJ_GRID_SIZE, max_z = 0;
    float com_x = 0.0f, com_y = 0.0f, com_z = 0.0f;
    int32_t counted = 0;

    for (int32_t z = 0; z < VOBJ_GRID_SIZE; z++)
    {
        for (int32_t y = 0; y < VOBJ_GRID_SIZE; y++)
        {
            for (int32_t x = 0; x < VOBJ_GRID_SIZE; x++)
            {
                if (obj->voxels[vobj_index(x, y, z)].material != 0)
                {
                    if (x < min_x) min_x = x;
                    if (x > max_x) max_x = x;
                    if (y < min_y) min_y = y;
                    if (y > max_y) max_y = y;
                    if (z < min_z) min_z = z;
                    if (z > max_z) max_z = z;
                    com_x += (float)x + 0.5f;
                    com_y += (float)y + 0.5f;
                    com_z += (float)z + 0.5f;
                    counted++;
                }
            }
        }
    }

    float extent_x = (float)(max_x - min_x + 1) * obj->voxel_size * 0.5f;
    float extent_y = (float)(max_y - min_y + 1) * obj->voxel_size * 0.5f;
    float extent_z = (float)(max_z - min_z + 1) * obj->voxel_size * 0.5f;
    obj->shape_half_extents = vec3_create(extent_x, extent_y, extent_z);
    obj->mass = (float)obj->voxel_count * 0.1f;
    obj->inv_mass = (obj->mass > 0.0f) ? 1.0f / obj->mass : 0.0f;

    float inv_count = 1.0f / (float)counted;
    com_x *= inv_count;
    com_y *= inv_count;
    com_z *= inv_count;

    float half_size_full = obj->voxel_size * (float)VOBJ_GRID_SIZE * 0.5f;
    obj->center_of_mass_offset = vec3_create(
        com_x * obj->voxel_size - half_size_full,
        com_y * obj->voxel_size - half_size_full,
        com_z * obj->voxel_size - half_size_full);

    float max_dist_sq = 0.0f;
    for (int32_t z = 0; z < VOBJ_GRID_SIZE; z++)
    {
        for (int32_t y = 0; y < VOBJ_GRID_SIZE; y++)
        {
            for (int32_t x = 0; x < VOBJ_GRID_SIZE; x++)
            {
                if (obj->voxels[vobj_index(x, y, z)].material != 0)
                {
                    float vx = (float)x + 0.5f;
                    float vy = (float)y + 0.5f;
                    float vz = (float)z + 0.5f;
                    for (int32_t c = 0; c < 8; c++)
                    {
                        float cx = vx + ((c & 1) ? 0.5f : -0.5f);
                        float cy = vy + ((c & 2) ? 0.5f : -0.5f);
                        float cz = vz + ((c & 4) ? 0.5f : -0.5f);
                        float dx = (cx - com_x) * obj->voxel_size;
                        float dy = (cy - com_y) * obj->voxel_size;
                        float dz = (cz - com_z) * obj->voxel_size;
                        float dist_sq = dx * dx + dy * dy + dz * dz;
                        if (dist_sq > max_dist_sq) max_dist_sq = dist_sq;
                    }
                }
            }
        }
    }
    obj->radius = sqrtf(max_dist_sq);

    int32_t support_min_x = VOBJ_GRID_SIZE, support_max_x = 0;
    int32_t support_min_z = VOBJ_GRID_SIZE, support_max_z = 0;
    for (int32_t z = 0; z < VOBJ_GRID_SIZE; z++)
    {
        for (int32_t x = 0; x < VOBJ_GRID_SIZE; x++)
        {
            if (obj->voxels[vobj_index(x, min_y, z)].material != 0)
            {
                if (x < support_min_x) support_min_x = x;
                if (x > support_max_x) support_max_x = x;
                if (z < support_min_z) support_min_z = z;
                if (z > support_max_z) support_max_z = z;
            }
        }
    }

    float support_cx = ((float)support_min_x + (float)support_max_x + 1.0f) * 0.5f * obj->voxel_size - half_size_full;
    float support_cz = ((float)support_min_z + (float)support_max_z + 1.0f) * 0.5f * obj->voxel_size - half_size_full;
    float support_half_x = ((float)(support_max_x - support_min_x + 1) * 0.5f) * obj->voxel_size;
    float support_half_z = ((float)(support_max_z - support_min_z + 1) * 0.5f) * obj->voxel_size;
    obj->support_min = vec3_create(support_cx - support_half_x, 0.0f, support_cz - support_half_z);
    obj->support_max = vec3_create(support_cx + support_half_x, 0.0f, support_cz + support_half_z);
}

/*
 * Flood fill for island splitting (iterative with explicit stack)
 * Stack size bounded to VOBJ_TOTAL_VOXELS (worst case: all voxels connected)
 */
static void flood_fill_voxels(const VoxelObject *obj, uint8_t *visited,
                              int32_t start_x, int32_t start_y, int32_t start_z)
{
    /* Explicit stack: packed coordinates (x + y*16 + z*256 = index) */
    int32_t stack[VOBJ_TOTAL_VOXELS];
    int32_t stack_top = 0;

    /* Push starting voxel if valid */
    int32_t start_idx = vobj_index(start_x, start_y, start_z);
    if (start_x < 0 || start_x >= VOBJ_GRID_SIZE ||
        start_y < 0 || start_y >= VOBJ_GRID_SIZE ||
        start_z < 0 || start_z >= VOBJ_GRID_SIZE ||
        visited[start_idx] || obj->voxels[start_idx].material == 0)
        return;

    stack[stack_top++] = start_idx;
    visited[start_idx] = 1;

    /* 6-connected neighbor offsets */
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

            visited[nidx] = 1;
            stack[stack_top++] = nidx;
        }
    }
}

/*
 * Split disconnected islands (iterative with work queue)
 * Uses bounded queue sized to VOBJ_MAX_OBJECTS
 */
static void split_disconnected_islands(VoxelObjectWorld *world, int32_t obj_index)
{
    /* Work queue of object indices to process */
    int32_t work_queue[VOBJ_MAX_OBJECTS];
    int32_t queue_head = 0;
    int32_t queue_tail = 0;

    work_queue[queue_tail++] = obj_index;

    while (queue_head < queue_tail)
    {
        int32_t current_idx = work_queue[queue_head++];
        VoxelObject *obj = &world->objects[current_idx];

        if (!obj->active || obj->voxel_count <= 1)
            continue;

        uint8_t visited[VOBJ_TOTAL_VOXELS] = {0};

        /* Find first solid voxel */
        int32_t first_x = -1, first_y = -1, first_z = -1;
        for (int32_t i = 0; i < VOBJ_TOTAL_VOXELS && first_x < 0; i++)
        {
            if (obj->voxels[i].material != 0)
            {
                vobj_coords(i, &first_x, &first_y, &first_z);
            }
        }
        if (first_x < 0)
            continue;

        /* Flood fill from first voxel */
        flood_fill_voxels(obj, visited, first_x, first_y, first_z);

        /* Count unvisited solid voxels */
        int32_t unvisited_count = 0;
        for (int32_t i = 0; i < VOBJ_TOTAL_VOXELS; i++)
        {
            if (obj->voxels[i].material != 0 && !visited[i])
            {
                unvisited_count++;
            }
        }
        if (unvisited_count == 0)
            continue;

        /* Create new object for disconnected voxels */
        if (world->object_count >= VOBJ_MAX_OBJECTS)
            continue;

        VoxelObject *new_obj = &world->objects[world->object_count];
        memset(new_obj, 0, sizeof(VoxelObject));
        new_obj->position = obj->position;
        new_obj->velocity = obj->velocity;
        new_obj->angular_velocity = obj->angular_velocity;
        new_obj->orientation = obj->orientation;
        new_obj->rotation = obj->rotation;
        new_obj->voxel_size = obj->voxel_size;
        new_obj->active = true;
        new_obj->bounds_dirty = true;
        new_obj->voxel_count = 0;

        /* Move unvisited voxels to new object */
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

        int32_t new_obj_idx = world->object_count;
        world->object_count++;

        recalc_object_shape(obj);
        recalc_object_shape(new_obj);

        /* Apply separation impulse to prevent immediate re-collision */
        Vec3 obj_com = vec3_add(obj->position, obj->center_of_mass_offset);
        Vec3 new_com = vec3_add(new_obj->position, new_obj->center_of_mass_offset);
        Vec3 sep_dir = vec3_sub(new_com, obj_com);
        float sep_len = vec3_length(sep_dir);

        if (sep_len > 0.001f)
        {
            sep_dir = vec3_scale(sep_dir, 1.0f / sep_len);
            float impulse = SPLIT_SEPARATION_IMPULSE + vec3_length(obj->velocity) * 0.2f;
            float total_mass = obj->mass + new_obj->mass;

            obj->velocity = vec3_sub(obj->velocity,
                vec3_scale(sep_dir, impulse * new_obj->mass / total_mass));
            new_obj->velocity = vec3_add(new_obj->velocity,
                vec3_scale(sep_dir, impulse * obj->mass / total_mass));

            obj->velocity.y += 0.3f;
            new_obj->velocity.y += 0.3f;
        }

        /* Force inertia recomputation for both fragments */
        obj->inv_inertia_local[0] = 0.0f;
        new_obj->inv_inertia_local[0] = 0.0f;

        /* Add angular velocity so fragments tumble (like fresh spheres get random spin) */
        float ang_scale = 1.5f;
        Vec3 sep_cross = vec3_cross(sep_dir, vec3_create(0.0f, 1.0f, 0.0f));
        float cross_len = vec3_length(sep_cross);
        if (cross_len > 0.01f)
        {
            sep_cross = vec3_scale(sep_cross, ang_scale / cross_len);
        }
        else
        {
            sep_cross = vec3_create(ang_scale, 0.0f, 0.0f);
        }
        obj->angular_velocity = vec3_add(obj->angular_velocity, sep_cross);
        new_obj->angular_velocity = vec3_sub(new_obj->angular_velocity, sep_cross);

        /* Add both to work queue for further splitting (bounded check) */
        if (queue_tail < VOBJ_MAX_OBJECTS)
            work_queue[queue_tail++] = current_idx;
        if (queue_tail < VOBJ_MAX_OBJECTS)
            work_queue[queue_tail++] = new_obj_idx;
    }
}

/*
 * Public API
 */

VoxelObjectWorld *voxel_object_world_create(Bounds3D bounds, float voxel_size)
{
    VoxelObjectWorld *world = (VoxelObjectWorld *)calloc(1, sizeof(VoxelObjectWorld));
    if (!world)
        return NULL;

    world->bounds = bounds;
    world->voxel_size = voxel_size;
    world->gravity = vec3_create(0.0f, -10.0f, 0.0f);
    world->damping = 0.95f;         /* 5% linear damping coefficient */
    world->angular_damping = 0.70f; /* 30% angular damping coefficient */
    world->restitution = 0.25f;      /* Lower bounce for faster settling */
    world->floor_friction = 0.7f;    /* More friction = faster stop */
    world->object_count = 0;
    world->enable_object_collision = true;

    /* Cell size = 2x typical object radius for good distribution */
    float cell_size = 1.5f;
    spatial_hash_init(&world->collision_grid, cell_size, bounds);

    return world;
}

void voxel_object_world_destroy(VoxelObjectWorld *world)
{
    if (world)
    {
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
                                      float radius, uint8_t material, RngState *rng)
{
    int32_t slot = allocate_object_slot(world);
    if (slot < 0)
        return -1;

    VoxelObject *obj = &world->objects[slot];
    memset(obj, 0, sizeof(VoxelObject));

    obj->position = position;
    obj->velocity = vec3_zero();
    obj->orientation = quat_identity();
    obj->angular_velocity = vec3_create(
        rng_range_f32(rng, -0.5f, 0.5f),
        rng_range_f32(rng, -0.5f, 0.5f),
        rng_range_f32(rng, -0.5f, 0.5f));
    obj->rotation = vec3_zero();
    obj->active = true;
    obj->bounds_dirty = true;

    /* Use world's voxel_size for consistent cube sizes across scene */
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

    recalc_object_shape(obj);
    return slot;
}

int32_t voxel_object_world_add_box(VoxelObjectWorld *world, Vec3 position,
                                   Vec3 half_extents, uint8_t material, RngState *rng)
{
    int32_t slot = allocate_object_slot(world);
    if (slot < 0)
        return -1;

    VoxelObject *obj = &world->objects[slot];
    memset(obj, 0, sizeof(VoxelObject));

    obj->position = position;
    obj->velocity = vec3_zero();
    obj->orientation = quat_identity();
    obj->angular_velocity = vec3_create(
        rng_range_f32(rng, -0.3f, 0.3f),
        rng_range_f32(rng, -0.3f, 0.3f),
        rng_range_f32(rng, -0.3f, 0.3f));
    obj->rotation = vec3_zero();
    obj->active = true;
    obj->bounds_dirty = true;

    /* Use world's voxel_size for consistent cube sizes across scene */
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

    recalc_object_shape(obj);
    return slot;
}

VoxelObjectHit voxel_object_world_raycast(VoxelObjectWorld *world, Vec3 origin, Vec3 dir)
{
    PROFILE_BEGIN(PROFILE_VOXEL_RAYCAST);

    VoxelObjectHit result = {0};
    result.hit = false;
    result.object_index = -1;

    float closest_t = 1e30f;

    for (int32_t i = 0; i < world->object_count; i++)
    {
        VoxelObject *obj = &world->objects[i];
        if (!obj->active || obj->voxel_count == 0)
            continue;

        /* Sphere broadphase */
        Vec3 pivot = vec3_add(obj->position, obj->center_of_mass_offset);
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

        /* Transform ray to local space using quaternion rotation */
        float rot_mat[9], inv_rot_mat[9];
        quat_to_mat3(obj->orientation, rot_mat);
        mat3_transpose(rot_mat, inv_rot_mat);

        Vec3 local_origin = mat3_transform_vec3(inv_rot_mat, vec3_sub(origin, pivot));
        Vec3 local_dir = mat3_transform_vec3(inv_rot_mat, dir);

        float half_size = obj->voxel_size * (float)VOBJ_GRID_SIZE * 0.5f;
        local_origin = vec3_add(local_origin, vec3_create(
                                                  half_size + obj->center_of_mass_offset.x,
                                                  half_size + obj->center_of_mass_offset.y,
                                                  half_size + obj->center_of_mass_offset.z));

        /* DDA ray march */
        Vec3 inv_dir;
        inv_dir.x = (fabsf(local_dir.x) > 0.0001f) ? 1.0f / local_dir.x : 1e10f;
        inv_dir.y = (fabsf(local_dir.y) > 0.0001f) ? 1.0f / local_dir.y : 1e10f;
        inv_dir.z = (fabsf(local_dir.z) > 0.0001f) ? 1.0f / local_dir.z : 1e10f;

        float t_start = fmaxf(t_sphere - obj->radius * 0.2f, 0.0f);
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

        for (int32_t step = 0; step < VOBJ_GRID_SIZE * 6; step++)
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

int32_t voxel_object_destroy_at_point(VoxelObjectWorld *world, int32_t obj_index,
                                      Vec3 impact_point, float destroy_radius,
                                      Vec3 *out_positions, uint8_t *out_materials,
                                      int32_t max_output)
{
    PROFILE_BEGIN(PROFILE_SIM_VOXEL_UPDATE);

    if (obj_index < 0 || obj_index >= world->object_count)
    {
        PROFILE_END(PROFILE_SIM_VOXEL_UPDATE);
        return 0;
    }

    VoxelObject *obj = &world->objects[obj_index];
    if (!obj->active)
    {
        PROFILE_END(PROFILE_SIM_VOXEL_UPDATE);
        return 0;
    }

    int32_t destroyed_count = 0;
    float half_size = obj->voxel_size * (float)VOBJ_GRID_SIZE * 0.5f;
    float rot_mat[9];
    quat_to_mat3(obj->orientation, rot_mat);
    Vec3 pivot = vec3_add(obj->position, obj->center_of_mass_offset);

    for (int32_t z = 0; z < VOBJ_GRID_SIZE && destroyed_count < max_output; z++)
    {
        for (int32_t y = 0; y < VOBJ_GRID_SIZE && destroyed_count < max_output; y++)
        {
            for (int32_t x = 0; x < VOBJ_GRID_SIZE && destroyed_count < max_output; x++)
            {
                int32_t idx = vobj_index(x, y, z);
                if (obj->voxels[idx].material == 0)
                    continue;

                /* Compute world position of this voxel */
                Vec3 local_pos;
                local_pos.x = ((float)x + 0.5f) * obj->voxel_size - half_size - obj->center_of_mass_offset.x;
                local_pos.y = ((float)y + 0.5f) * obj->voxel_size - half_size - obj->center_of_mass_offset.y;
                local_pos.z = ((float)z + 0.5f) * obj->voxel_size - half_size - obj->center_of_mass_offset.z;

                Vec3 rotated = mat3_transform_vec3(rot_mat, local_pos);
                Vec3 voxel_pos = vec3_add(pivot, rotated);

                float dist = vec3_length(vec3_sub(voxel_pos, impact_point));

                if (dist < destroy_radius)
                {
                    if (out_positions)
                        out_positions[destroyed_count] = voxel_pos;
                    if (out_materials)
                        out_materials[destroyed_count] = obj->voxels[idx].material;

                    obj->voxels[idx].material = 0;
                    obj->voxel_count--;
                    destroyed_count++;
                }
            }
        }
    }

    if (obj->voxel_count <= 0)
    {
        obj->active = false;
    }
    else
    {
        recalc_object_shape(obj);
        split_disconnected_islands(world, obj_index);
    }

    PROFILE_END(PROFILE_SIM_VOXEL_UPDATE);
    return destroyed_count;
}

int32_t voxel_object_world_add_from_voxels(VoxelObjectWorld *world,
                                           const uint8_t *voxels,
                                           int32_t size_x, int32_t size_y, int32_t size_z,
                                           Vec3 origin, float voxel_size,
                                           Vec3 initial_velocity,
                                           RngState *rng)
{
    if (!world || !voxels || size_x <= 0 || size_y <= 0 || size_z <= 0)
        return -1;

    /* Check if island fits in VOBJ_GRID_SIZE */
    if (size_x > VOBJ_GRID_SIZE || size_y > VOBJ_GRID_SIZE || size_z > VOBJ_GRID_SIZE)
        return -1;

    int32_t slot = allocate_object_slot(world);
    if (slot < 0)
        return -1;

    VoxelObject *obj = &world->objects[slot];
    memset(obj, 0, sizeof(VoxelObject));

    obj->voxel_size = voxel_size;
    obj->voxel_count = 0;
    obj->active = true;
    obj->bounds_dirty = true;
    obj->velocity = initial_velocity;
    obj->orientation = quat_identity();
    obj->rotation = vec3_zero();
    obj->angular_velocity = vec3_create(
        rng_range_f32(rng, -1.0f, 1.0f),
        rng_range_f32(rng, -0.5f, 0.5f),
        rng_range_f32(rng, -1.0f, 1.0f));

    /* Copy voxels into object grid (centered) */
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

    /* Compute position: origin is corner of source voxels, adjust for centering */
    float src_center_x = origin.x + (float)size_x * voxel_size * 0.5f;
    float src_center_y = origin.y + (float)size_y * voxel_size * 0.5f;
    float src_center_z = origin.z + (float)size_z * voxel_size * 0.5f;

    obj->position = vec3_create(src_center_x, src_center_y, src_center_z);

    recalc_object_shape(obj);
    return slot;
}
