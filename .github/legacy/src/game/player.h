#ifndef PATCH_GAME_PLAYER_H
#define PATCH_GAME_PLAYER_H

#include "../core/types.h"
#include "../core/math.h"
#include "combat.h"
#include "humanoid.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PLAYER_PUNCH_COOLDOWN 0.3f
#define PLAYER_PUNCH_DAMAGE 25.0f
#define PLAYER_DEATH_MASS_RATIO 0.3f

typedef struct {
    Vec3 position;
    Vec3 velocity;
    float yaw;
    
    HumanoidModel model;
    
    float body_width;
    float body_height;
    float body_depth;
    
    float head_size;
    float arm_width;
    float arm_length;
    float leg_width;
    float leg_length;
    
    float move_speed;
    float punch_cooldown;
    float punch_timer;
    bool is_punching;
    
    int32_t held_enemy_id;
    bool is_holding;
    
    float arm_swing;
    float leg_swing;
    float walk_cycle;
    
    bool is_dead;
} Player;

typedef struct {
    bool move_forward;
    bool move_backward;
    bool move_left;
    bool move_right;
    bool punch;
    bool grab;
} PlayerInput;

void player_init(Player* player, Vec3 position);

void player_update(Player* player, const PlayerInput* input, float dt);

Vec3 player_get_right_shoulder(const Player* player);

Vec3 player_get_right_hand(const Player* player);

CapsuleHitbox player_get_punch_hitbox(const Player* player);

CapsuleHitbox player_get_grab_hitbox(const Player* player);

Vec3 player_get_head_position(const Player* player);

Vec3 player_get_body_center(const Player* player);

float player_get_collision_radius(const Player* player);

void player_start_punch(Player* player);

bool player_can_punch(const Player* player);

int32_t player_damage_at_point(Player* player, Vec3 hit_point, float damage, Vec3 hit_direction,
    Vec3* out_positions, Vec3* out_colors, int32_t max_out);

float player_get_mass_ratio(const Player* player);

HumanoidPose player_get_pose(const Player* player);

#ifdef __cplusplus
}
#endif

#endif
