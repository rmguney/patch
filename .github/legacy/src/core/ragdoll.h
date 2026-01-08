#ifndef PATCH_CORE_RAGDOLL_H
#define PATCH_CORE_RAGDOLL_H

#include "types.h"
#include "math.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RAGDOLL_MAX_LIMBS 8

typedef struct {
    Vec3 position;
    Vec3 velocity;
    Vec3 rotation;
    Vec3 angular_velocity;
    float mass;
    float constraint_distance;
    int32_t parent_index;
    bool grounded;
} RagdollBone;

typedef struct {
    float gravity;
    float bounce;
    float friction;
    float linear_damping;
    float angular_damping;
    float constraint_stiffness;
} RagdollConfig;

typedef struct {
    RagdollBone bones[RAGDOLL_MAX_LIMBS];
    int32_t bone_count;
    Vec3 root_position;
    Vec3 root_velocity;
    Vec3 root_rotation;
    Vec3 root_angular_velocity;
    RagdollConfig config;
    bool active;
    float time;
} RagdollSystem;

static inline RagdollConfig ragdoll_config_default(void) {
    RagdollConfig cfg;
    cfg.gravity = -25.0f;
    cfg.bounce = 0.25f;
    cfg.friction = 0.7f;
    cfg.linear_damping = 0.97f;
    cfg.angular_damping = 0.90f;
    cfg.constraint_stiffness = 0.5f;
    return cfg;
}

static inline void ragdoll_system_init(RagdollSystem* sys) {
    for (int32_t i = 0; i < RAGDOLL_MAX_LIMBS; i++) {
        sys->bones[i].position = vec3_zero();
        sys->bones[i].velocity = vec3_zero();
        sys->bones[i].rotation = vec3_zero();
        sys->bones[i].angular_velocity = vec3_zero();
        sys->bones[i].mass = 1.0f;
        sys->bones[i].constraint_distance = 0.5f;
        sys->bones[i].parent_index = -1;
        sys->bones[i].grounded = false;
    }
    sys->bone_count = 0;
    sys->root_position = vec3_zero();
    sys->root_velocity = vec3_zero();
    sys->root_rotation = vec3_zero();
    sys->root_angular_velocity = vec3_zero();
    sys->config = ragdoll_config_default();
    sys->active = false;
    sys->time = 0.0f;
}

static inline void ragdoll_bone_apply_gravity(RagdollBone* bone, float gravity, float dt) {
    bone->velocity.y += gravity * dt;
}

static inline void ragdoll_bone_integrate(RagdollBone* bone, float dt) {
    bone->position = vec3_add(bone->position, vec3_scale(bone->velocity, dt));
    bone->rotation = vec3_add(bone->rotation, vec3_scale(bone->angular_velocity, dt));
}

static inline void ragdoll_bone_constrain(RagdollBone* bone, Vec3 anchor, float stiffness) {
    Vec3 to_anchor = vec3_sub(anchor, bone->position);
    float dist = vec3_length(to_anchor);
    
    if (dist > bone->constraint_distance && dist > 0.001f) {
        float correction = (dist - bone->constraint_distance) * stiffness;
        Vec3 dir = vec3_scale(to_anchor, 1.0f / dist);
        bone->position = vec3_add(bone->position, vec3_scale(dir, correction));
        
        float vel_along = vec3_dot(bone->velocity, dir);
        if (vel_along < 0.0f) {
            bone->velocity = vec3_add(bone->velocity, vec3_scale(dir, -vel_along * 0.8f));
        }
    }
}

static inline void ragdoll_bone_floor_collision(RagdollBone* bone, float floor_y, float bounce, float friction) {
    if (bone->position.y < floor_y) {
        bone->position.y = floor_y;
        bone->grounded = true;
        
        if (bone->velocity.y < -0.5f) {
            bone->velocity.y = -bone->velocity.y * bounce;
            bone->angular_velocity = vec3_scale(bone->angular_velocity, 0.7f);
        } else {
            bone->velocity.y = 0.0f;
        }
        bone->velocity.x *= friction;
        bone->velocity.z *= friction;
    } else {
        bone->grounded = false;
    }
}

static inline void ragdoll_bone_apply_damping(RagdollBone* bone, float linear_damping, float angular_damping) {
    bone->velocity = vec3_scale(bone->velocity, linear_damping);
    bone->angular_velocity = vec3_scale(bone->angular_velocity, angular_damping);
}

static inline void ragdoll_system_update(RagdollSystem* sys, float floor_y, float dt) {
    if (!sys->active) return;
    
    sys->time += dt;
    const RagdollConfig* cfg = &sys->config;
    
    sys->root_velocity.y += cfg->gravity * dt;
    sys->root_position = vec3_add(sys->root_position, vec3_scale(sys->root_velocity, dt));
    sys->root_rotation = vec3_add(sys->root_rotation, vec3_scale(sys->root_angular_velocity, dt));
    
    float collapse_factor = sys->time * 2.0f;
    if (collapse_factor > 1.0f) collapse_factor = 1.0f;
    float ground_offset = 0.3f * (1.0f - collapse_factor * 0.7f);
    
    if (sys->root_position.y < floor_y + ground_offset) {
        sys->root_position.y = floor_y + ground_offset;
        
        if (sys->root_velocity.y < -0.5f) {
            sys->root_velocity.y = -sys->root_velocity.y * cfg->bounce;
            sys->root_angular_velocity.x += sys->root_velocity.z * 2.0f;
            sys->root_angular_velocity.z -= sys->root_velocity.x * 2.0f;
        } else {
            sys->root_velocity.y = 0.0f;
        }
        sys->root_velocity.x *= cfg->friction;
        sys->root_velocity.z *= cfg->friction;
        sys->root_angular_velocity = vec3_scale(sys->root_angular_velocity, cfg->friction);
    }
    
    sys->root_angular_velocity = vec3_scale(sys->root_angular_velocity, cfg->angular_damping);
    sys->root_velocity.x *= cfg->linear_damping;
    sys->root_velocity.z *= cfg->linear_damping;
    
    for (int32_t i = 0; i < sys->bone_count; i++) {
        RagdollBone* bone = &sys->bones[i];
        
        ragdoll_bone_apply_gravity(bone, cfg->gravity, dt);
        ragdoll_bone_integrate(bone, dt);
        
        Vec3 anchor;
        if (bone->parent_index < 0) {
            anchor = sys->root_position;
        } else {
            anchor = sys->bones[bone->parent_index].position;
        }
        
        ragdoll_bone_constrain(bone, anchor, cfg->constraint_stiffness);
        ragdoll_bone_floor_collision(bone, floor_y, cfg->bounce, cfg->friction);
        ragdoll_bone_apply_damping(bone, cfg->linear_damping, cfg->angular_damping);
    }
}

static inline bool ragdoll_system_is_settled(const RagdollSystem* sys) {
    if (!sys->active) return true;
    
    float vel_threshold = 0.1f;
    float ang_threshold = 0.3f;
    
    float root_speed = vec3_length(sys->root_velocity);
    float root_ang_speed = vec3_length(sys->root_angular_velocity);
    
    if (root_speed > vel_threshold || root_ang_speed > ang_threshold) {
        return false;
    }
    
    for (int32_t i = 0; i < sys->bone_count; i++) {
        float bone_speed = vec3_length(sys->bones[i].velocity);
        float bone_ang_speed = vec3_length(sys->bones[i].angular_velocity);
        
        if (bone_speed > vel_threshold || bone_ang_speed > ang_threshold) {
            return false;
        }
    }
    
    return true;
}

#ifdef __cplusplus
}
#endif

#endif
