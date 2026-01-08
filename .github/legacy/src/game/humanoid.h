#ifndef PATCH_GAME_HUMANOID_H
#define PATCH_GAME_HUMANOID_H

#include "../core/types.h"
#include "../core/math.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HUMANOID_MAX_VOXELS 256
#define HUMANOID_VOXEL_SIZE 0.1f

typedef enum {
    HUMANOID_PART_HEAD,
    HUMANOID_PART_BODY,
    HUMANOID_PART_ARM_LEFT,
    HUMANOID_PART_ARM_RIGHT,
    HUMANOID_PART_LEG_LEFT,
    HUMANOID_PART_LEG_RIGHT
} HumanoidPart;

typedef struct {
    Vec3 local_offset;
    Vec3 color_override;
    HumanoidPart part;
    float mass;
    bool active;
    bool has_color_override;
} HumanoidVoxel;

typedef struct {
    float body_width;
    float body_height;
    float body_depth;
    float head_size;
    float arm_width;
    float arm_length;
    float leg_width;
    float leg_length;
} HumanoidDimensions;

typedef struct {
    float yaw;
    float arm_swing;
    float leg_swing;
    float punch_swing;
} HumanoidPose;

typedef struct {
    Vec3 position;
    Vec3 velocity;
    Vec3 rotation;
    Vec3 angular_velocity;
} RagdollLimb;

typedef struct {
    Vec3 position;
    Vec3 velocity;
    Vec3 rotation;
    Vec3 angular_velocity;
    bool ragdoll_active;
    float ragdoll_time;
    RagdollLimb head;
    RagdollLimb torso;
    RagdollLimb arm_left;
    RagdollLimb arm_right;
    RagdollLimb leg_left;
    RagdollLimb leg_right;
} HumanoidRagdollState;

typedef struct {
    HumanoidVoxel voxels[HUMANOID_MAX_VOXELS];
    int32_t voxel_count;
    float total_mass;
    float current_mass;
    HumanoidDimensions dims;
    HumanoidRagdollState ragdoll;
    Vec3 last_hit_direction;
    Vec3 center_of_mass_offset;
} HumanoidModel;

static inline Vec3 humanoid_get_forward(float yaw) {
    return vec3_create(-sinf(yaw), 0.0f, cosf(yaw));
}

static inline Vec3 humanoid_get_shoulder(Vec3 position, const HumanoidDimensions* dims, 
                                          float yaw, bool right) {
    float sin_yaw = sinf(yaw);
    float cos_yaw = cosf(yaw);
    
    float shoulder_y = position.y + dims->leg_length + dims->body_height * 0.85f;
    float arm_side_offset = dims->body_width * 0.5f + dims->arm_width * 0.5f;
    
    Vec3 shoulder = position;
    if (right) {
        shoulder.x += cos_yaw * arm_side_offset;
        shoulder.z -= sin_yaw * arm_side_offset;
    } else {
        shoulder.x -= cos_yaw * arm_side_offset;
        shoulder.z += sin_yaw * arm_side_offset;
    }
    shoulder.y = shoulder_y;
    return shoulder;
}

static inline Vec3 humanoid_get_head_center(Vec3 position, const HumanoidDimensions* dims) {
    Vec3 head = position;
    head.y += dims->leg_length + dims->body_height + dims->head_size * 0.5f;
    return head;
}

static inline Vec3 humanoid_get_body_center(Vec3 position, const HumanoidDimensions* dims) {
    Vec3 body = position;
    body.y += dims->leg_length + dims->body_height * 0.5f;
    return body;
}

static inline float humanoid_get_collision_radius(const HumanoidDimensions* dims) {
    float max_dim = dims->body_width > dims->body_depth ? dims->body_width : dims->body_depth;
    return max_dim * 0.6f;
}

static inline HumanoidPose humanoid_make_pose(float yaw, float arm_swing, float leg_swing, 
                                               float punch_swing) {
    HumanoidPose pose;
    pose.yaw = yaw;
    pose.arm_swing = arm_swing;
    pose.leg_swing = leg_swing;
    pose.punch_swing = punch_swing;
    return pose;
}

static inline float humanoid_calculate_punch_swing(bool is_punching, float punch_timer, 
                                                    float punch_cooldown, float max_swing) {
    if (!is_punching) return 0.0f;
    float t = punch_timer / punch_cooldown;
    return sinf((1.0f - t) * K_PI) * max_swing;
}

void humanoid_model_init(HumanoidModel* model, const HumanoidDimensions* dims);

void humanoid_model_build_voxels(HumanoidModel* model);

bool humanoid_damage_at_point(HumanoidModel* model, Vec3 base_pos, const HumanoidPose* pose,
                               Vec3 world_hit, float damage, Vec3 hit_direction, Vec3 base_color,
                               Vec3* out_positions, Vec3* out_colors, int32_t max_out, int32_t max_destroy, int32_t* out_count);

float humanoid_get_mass_ratio(const HumanoidModel* model);

Vec3 humanoid_transform_voxel(const HumanoidVoxel* voxel, Vec3 base_pos, 
                               const HumanoidDimensions* dims, const HumanoidPose* pose);

Vec3 humanoid_get_voxel_rotation(const HumanoidVoxel* voxel, const HumanoidPose* pose);

void humanoid_start_ragdoll(HumanoidModel* model, Vec3 position, Vec3 velocity, Vec3 hit_direction);

void humanoid_update_ragdoll(HumanoidModel* model, float floor_y, float dt);

Vec3 humanoid_calculate_center_of_mass(const HumanoidModel* model);

int32_t humanoid_check_connectivity(HumanoidModel* model, Vec3 base_pos, const HumanoidPose* pose,
                                     Vec3 base_color, Vec3* out_positions, Vec3* out_colors, int32_t max_out);

bool humanoid_head_connected(const HumanoidModel* model);

bool humanoid_should_die(const HumanoidModel* model);

bool humanoid_heal_voxel(HumanoidModel* model, Vec3 color);

Vec3 humanoid_transform_voxel_ragdoll(const HumanoidVoxel* voxel, const HumanoidModel* model);

#ifdef __cplusplus
}
#endif

#endif
