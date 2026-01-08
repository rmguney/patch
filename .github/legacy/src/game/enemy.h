#ifndef PATCH_GAME_ENEMY_H
#define PATCH_GAME_ENEMY_H

#include "../core/types.h"
#include "../core/math.h"
#include "combat.h"
#include "humanoid.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ENEMY_PUNCH_COOLDOWN 0.4f
#define ENEMY_PUNCH_DAMAGE 15.0f
#define ENEMY_DEATH_MASS_RATIO 0.5f

#define ENEMY_ATTACK_DURATION 0.35f
#define ENEMY_ATTACK_WINDUP 0.10f

typedef enum {
    ENEMY_STATE_IDLE,
    ENEMY_STATE_WANDER,
    ENEMY_STATE_CHASE,
    ENEMY_STATE_ATTACK,
    ENEMY_STATE_STAGGER,
    ENEMY_STATE_HELD,
    ENEMY_STATE_DYING,
    ENEMY_STATE_DEAD
} EnemyState;

typedef struct {
    Vec3 position;
    Vec3 velocity;
    float yaw;
    float target_yaw;
    
    HumanoidModel model;
    
    EnemyState state;
    float state_timer;
    float attack_cooldown;
    float aggro_timer;
    
    float walk_cycle;
    float arm_swing;
    float leg_swing;
    
    float death_time;
    float collapse_progress;
    
    bool active;
    int32_t id;
    
    bool hit_this_punch;
    bool hit_this_attack;
    float attack_height_offset;
    float attack_side_offset;
    Vec3 steering;
} Enemy;

void enemy_init(Enemy* enemy, Vec3 position, int32_t id);

void enemy_update(Enemy* enemy, Vec3 player_pos, float dt);

Vec3 enemy_get_right_shoulder(const Enemy* enemy);

CapsuleHitbox enemy_get_punch_hitbox(const Enemy* enemy);

Vec3 enemy_get_head_position(const Enemy* enemy);

Vec3 enemy_get_body_center(const Enemy* enemy);

float enemy_get_collision_radius(const Enemy* enemy);

int32_t enemy_damage_at_point(Enemy* enemy, Vec3 hit_point, float damage, Vec3 hit_direction,
    Vec3* out_positions, Vec3* out_colors, int32_t max_out);

void enemy_reset_punch_state(Enemy* enemy);

float enemy_get_mass_ratio(const Enemy* enemy);

bool enemy_is_dead(const Enemy* enemy);

void enemy_start_dying(Enemy* enemy);

void enemy_update_death(Enemy* enemy, float floor_y, float dt);

bool enemy_update_held(Enemy* enemy, Vec3 hold_pos, Vec3 holder_velocity, float dt);

HumanoidPose enemy_get_pose(const Enemy* enemy);

#ifdef __cplusplus
}
#endif

#endif
