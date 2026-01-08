#ifndef PATCH_SCENES_MELEE_H
#define PATCH_SCENES_MELEE_H

#include "../core/scene.h"
#include "../game/player.h"
#include "../game/enemy.h"
#include "../game/combat.h"
#include "../core/particles.h"
#include "../core/voxel_object.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MELEE_MAX_ENEMIES 256
#define MELEE_DEFAULT_MAX_DEAD_BODIES 100

typedef struct {
    Player player;
    PlayerInput input;

    Enemy enemies[MELEE_MAX_ENEMIES];
    int32_t enemy_count;
    int32_t next_enemy_id;

    ParticleSystem* particles;
    VoxelObjectWorld* vobj_world;

    int32_t spawned_chunks[1024];
    int32_t spawned_chunk_count;
    int32_t current_chunk_x;
    int32_t current_chunk_z;
    bool prop_hit_this_punch[256];

    Vec3 destroyed_positions[64];
    Vec3 destroyed_colors[64];

    int32_t destroyed_cubes;

    int32_t score;
    int32_t kills;
    int32_t dead_body_count;
    int32_t max_dead_bodies;
    float spawn_timer;
    float spawn_interval;
    float difficulty;
    float survival_time;
} MeleeData;

Scene* melee_scene_create(Bounds3D bounds);

void melee_set_input(Scene* scene, bool w, bool a, bool s, bool d, bool left_click, bool right_click);

MeleeData* melee_get_data(Scene* scene);

#ifdef __cplusplus
}
#endif

#endif
