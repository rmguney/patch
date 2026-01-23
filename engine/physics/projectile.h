#ifndef PATCH_PHYSICS_PROJECTILE_H
#define PATCH_PHYSICS_PROJECTILE_H

#include "engine/core/types.h"
#include "engine/core/math.h"
#include "engine/voxel/volume.h"
#include "engine/voxel/voxel_object.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define PROJ_MAX_PROJECTILES 256
#define PROJ_MAX_DISTANCE 500.0f
#define PROJ_DAMAGE_FACTOR 1.0f

    typedef enum
    {
        PROJ_TYPE_HITSCAN,
        PROJ_TYPE_BALLISTIC
    } ProjectileType;

    typedef struct
    {
        Vec3 position;
        Vec3 velocity;
        Vec3 prev_position;
        float mass;
        float radius;
        float lifetime;
        float max_lifetime;
        ProjectileType type;
        bool active;
        int32_t owner_id;
    } Projectile;

    typedef struct
    {
        bool hit;
        Vec3 hit_point;
        Vec3 hit_normal;
        int32_t hit_object_index;
        bool hit_terrain;
        float damage;
    } ProjectileHitResult;

    typedef struct ProjectileSystem ProjectileSystem;

    ProjectileSystem *projectile_system_create(void);
    void projectile_system_destroy(ProjectileSystem *system);

    int32_t projectile_fire_hitscan(ProjectileSystem *system,
                                    VoxelVolume *terrain,
                                    VoxelObjectWorld *objects,
                                    Vec3 origin,
                                    Vec3 direction,
                                    float damage,
                                    ProjectileHitResult *out_result);

    int32_t projectile_fire_ballistic(ProjectileSystem *system,
                                      Vec3 origin,
                                      Vec3 velocity,
                                      float mass,
                                      float radius,
                                      float max_lifetime);

    void projectile_system_update(ProjectileSystem *system,
                                  VoxelVolume *terrain,
                                  VoxelObjectWorld *objects,
                                  float dt,
                                  ProjectileHitResult *out_results,
                                  int32_t *out_result_count,
                                  int32_t max_results);

    Projectile *projectile_system_get(ProjectileSystem *system, int32_t index);
    int32_t projectile_system_active_count(ProjectileSystem *system);

#ifdef __cplusplus
}
#endif

#endif
