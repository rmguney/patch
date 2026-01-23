#include "projectile.h"
#include "rigidbody.h"
#include <stdlib.h>
#include <string.h>

struct ProjectileSystem
{
    Projectile projectiles[PROJ_MAX_PROJECTILES];
    int32_t active_count;
    int32_t next_slot;
};

ProjectileSystem *projectile_system_create(void)
{
    ProjectileSystem *system = (ProjectileSystem *)calloc(1, sizeof(ProjectileSystem));
    if (!system)
        return NULL;

    system->active_count = 0;
    system->next_slot = 0;

    for (int32_t i = 0; i < PROJ_MAX_PROJECTILES; i++)
    {
        system->projectiles[i].active = false;
    }

    return system;
}

void projectile_system_destroy(ProjectileSystem *system)
{
    if (system)
        free(system);
}

static int32_t find_free_slot(ProjectileSystem *system)
{
    for (int32_t i = 0; i < PROJ_MAX_PROJECTILES; i++)
    {
        int32_t idx = (system->next_slot + i) % PROJ_MAX_PROJECTILES;
        if (!system->projectiles[idx].active)
        {
            system->next_slot = (idx + 1) % PROJ_MAX_PROJECTILES;
            return idx;
        }
    }

    int32_t oldest = system->next_slot;
    system->next_slot = (oldest + 1) % PROJ_MAX_PROJECTILES;
    return oldest;
}

static bool raycast_terrain(VoxelVolume *terrain, Vec3 origin, Vec3 dir, float max_dist,
                            Vec3 *out_hit, Vec3 *out_normal, float *out_dist)
{
    if (!terrain)
        return false;

    uint8_t hit_mat;
    float dist = volume_raycast(terrain, origin, dir, max_dist, out_hit, out_normal, &hit_mat);

    if (dist >= 0.0f && hit_mat != 0)
    {
        *out_dist = dist;
        return true;
    }

    return false;
}

static bool raycast_objects(VoxelObjectWorld *objects, Vec3 origin, Vec3 dir, float max_dist,
                            Vec3 *out_hit, Vec3 *out_normal, int32_t *out_obj_index, float *out_dist)
{
    if (!objects)
        return false;

    VoxelObjectHit hit = voxel_object_world_raycast(objects, origin, dir);

    if (hit.hit)
    {
        Vec3 delta = vec3_sub(hit.impact_point, origin);
        float dist = vec3_length(delta);

        if (dist <= max_dist)
        {
            *out_hit = hit.impact_point;
            *out_normal = hit.impact_normal;
            *out_obj_index = hit.object_index;
            *out_dist = dist;
            return true;
        }
    }

    return false;
}

int32_t projectile_fire_hitscan(ProjectileSystem *system,
                                VoxelVolume *terrain,
                                VoxelObjectWorld *objects,
                                Vec3 origin,
                                Vec3 direction,
                                float damage,
                                ProjectileHitResult *out_result)
{
    if (!system || !out_result)
        return -1;

    direction = vec3_normalize(direction);

    memset(out_result, 0, sizeof(ProjectileHitResult));

    Vec3 terrain_hit, terrain_normal;
    float terrain_dist = PROJ_MAX_DISTANCE + 1.0f;
    bool hit_terrain = raycast_terrain(terrain, origin, direction, PROJ_MAX_DISTANCE,
                                       &terrain_hit, &terrain_normal, &terrain_dist);

    Vec3 object_hit, object_normal;
    int32_t object_index = -1;
    float object_dist = PROJ_MAX_DISTANCE + 1.0f;
    bool hit_object = raycast_objects(objects, origin, direction, PROJ_MAX_DISTANCE,
                                      &object_hit, &object_normal, &object_index, &object_dist);

    if (hit_terrain && (!hit_object || terrain_dist < object_dist))
    {
        out_result->hit = true;
        out_result->hit_point = terrain_hit;
        out_result->hit_normal = terrain_normal;
        out_result->hit_terrain = true;
        out_result->hit_object_index = -1;
        out_result->damage = damage;
        return 0;
    }
    else if (hit_object)
    {
        out_result->hit = true;
        out_result->hit_point = object_hit;
        out_result->hit_normal = object_normal;
        out_result->hit_terrain = false;
        out_result->hit_object_index = object_index;
        out_result->damage = damage;
        return 0;
    }

    out_result->hit = false;
    return -1;
}

int32_t projectile_fire_ballistic(ProjectileSystem *system,
                                  Vec3 origin,
                                  Vec3 velocity,
                                  float mass,
                                  float radius,
                                  float max_lifetime)
{
    if (!system)
        return -1;

    int32_t slot = find_free_slot(system);
    Projectile *proj = &system->projectiles[slot];

    proj->position = origin;
    proj->prev_position = origin;
    proj->velocity = velocity;
    proj->mass = mass;
    proj->radius = radius;
    proj->lifetime = 0.0f;
    proj->max_lifetime = max_lifetime;
    proj->type = PROJ_TYPE_BALLISTIC;
    proj->active = true;
    proj->owner_id = -1;

    system->active_count++;
    return slot;
}

static void update_ballistic(ProjectileSystem *system,
                             Projectile *proj,
                             VoxelVolume *terrain,
                             VoxelObjectWorld *objects,
                             float dt,
                             ProjectileHitResult *out_results,
                             int32_t *result_count,
                             int32_t max_results)
{
    proj->prev_position = proj->position;

    proj->velocity = vec3_add(proj->velocity, vec3_scale(vec3_create(0.0f, PHYS_GRAVITY_Y, 0.0f), dt));
    Vec3 new_pos = vec3_add(proj->position, vec3_scale(proj->velocity, dt));

    Vec3 move_dir = vec3_sub(new_pos, proj->prev_position);
    float move_dist = vec3_length(move_dir);

    if (move_dist > K_EPSILON)
    {
        Vec3 dir = vec3_scale(move_dir, 1.0f / move_dist);

        Vec3 terrain_hit, terrain_normal;
        float terrain_dist = move_dist + 1.0f;
        bool hit_terrain = raycast_terrain(terrain, proj->prev_position, dir, move_dist,
                                           &terrain_hit, &terrain_normal, &terrain_dist);

        Vec3 object_hit, object_normal;
        int32_t object_index = -1;
        float object_dist = move_dist + 1.0f;
        bool hit_object = raycast_objects(objects, proj->prev_position, dir, move_dist,
                                          &object_hit, &object_normal, &object_index, &object_dist);

        bool hit = false;
        ProjectileHitResult result = {0};

        if (hit_terrain && (!hit_object || terrain_dist < object_dist))
        {
            hit = true;
            result.hit = true;
            result.hit_point = terrain_hit;
            result.hit_normal = terrain_normal;
            result.hit_terrain = true;
            result.hit_object_index = -1;
            result.damage = proj->mass * vec3_length(proj->velocity) * PROJ_DAMAGE_FACTOR;
        }
        else if (hit_object)
        {
            hit = true;
            result.hit = true;
            result.hit_point = object_hit;
            result.hit_normal = object_normal;
            result.hit_terrain = false;
            result.hit_object_index = object_index;
            result.damage = proj->mass * vec3_length(proj->velocity) * PROJ_DAMAGE_FACTOR;
        }

        if (hit)
        {
            if (out_results && *result_count < max_results)
            {
                out_results[*result_count] = result;
                (*result_count)++;
            }

            proj->active = false;
            system->active_count--;
            return;
        }
    }

    proj->position = new_pos;
    proj->lifetime += dt;

    if (proj->lifetime >= proj->max_lifetime)
    {
        proj->active = false;
        system->active_count--;
    }
}

void projectile_system_update(ProjectileSystem *system,
                              VoxelVolume *terrain,
                              VoxelObjectWorld *objects,
                              float dt,
                              ProjectileHitResult *out_results,
                              int32_t *out_result_count,
                              int32_t max_results)
{
    if (!system)
        return;

    int32_t result_count = 0;

    for (int32_t i = 0; i < PROJ_MAX_PROJECTILES; i++)
    {
        Projectile *proj = &system->projectiles[i];
        if (!proj->active)
            continue;

        if (proj->type == PROJ_TYPE_BALLISTIC)
        {
            update_ballistic(system, proj, terrain, objects, dt,
                             out_results, &result_count, max_results);
        }
    }

    if (out_result_count)
        *out_result_count = result_count;
}

Projectile *projectile_system_get(ProjectileSystem *system, int32_t index)
{
    if (!system || index < 0 || index >= PROJ_MAX_PROJECTILES)
        return NULL;
    return &system->projectiles[index];
}

int32_t projectile_system_active_count(ProjectileSystem *system)
{
    return system ? system->active_count : 0;
}
