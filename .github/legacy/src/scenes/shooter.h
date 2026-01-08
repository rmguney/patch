#ifndef PATCH_SCENES_SHOOTER_H
#define PATCH_SCENES_SHOOTER_H

#include "../core/scene.h"
#include "../game/player.h"
#include "../game/enemy.h"
#include "../game/combat.h"
#include "../core/particles.h"
#include "../core/voxel_object.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHOOTER_MAX_ENEMIES 256
#define SHOOTER_MAX_PROJECTILES 256

typedef struct {
    Vec3 position;
    Vec3 velocity;
    float lifetime;
    float radius;
    bool active;
} Projectile;

typedef struct {
    Player player;
    PlayerInput input;

    Enemy enemies[SHOOTER_MAX_ENEMIES];
    int32_t enemy_count;
    int32_t next_enemy_id;

    ParticleSystem* particles;
    VoxelObjectWorld* vobj_world;

    int32_t spawned_chunks[1024];
    int32_t spawned_chunk_count;

    Projectile projectiles[SHOOTER_MAX_PROJECTILES];
    float shoot_cooldown;

    bool aiming;

    Vec3 aim_origin;
    Vec3 aim_dir;
    bool aim_valid;

    Vec3 destroyed_positions[64];
    Vec3 destroyed_colors[64];

    int32_t destroyed_cubes;
    int32_t dead_body_count;
    int32_t max_dead_bodies;
    float spawn_timer;
    float spawn_interval;
    float difficulty;
    float survival_time;
} ShooterData;

Scene* shooter_scene_create(Bounds3D bounds);

void shooter_set_input(Scene* scene, bool w, bool a, bool s, bool d, bool left_click, bool right_down);

void shooter_set_aim_ray(Scene* scene, Vec3 origin, Vec3 dir);

ShooterData* shooter_get_data(Scene* scene);

#ifdef __cplusplus
}
#endif

#endif
