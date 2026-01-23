#ifndef PATCH_PHYSICS_RAGDOLL_H
#define PATCH_PHYSICS_RAGDOLL_H

#include "engine/core/types.h"
#include "engine/core/math.h"
#include "engine/voxel/volume.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define RAGDOLL_MAX_PARTS 6
#define RAGDOLL_MAX_CONSTRAINTS 5
#define RAGDOLL_MAX_RAGDOLLS 32
#define RAGDOLL_DAMPING 0.98f
#define RAGDOLL_GRAVITY -18.0f

    typedef enum
    {
        RAGDOLL_PART_HEAD,
        RAGDOLL_PART_TORSO,
        RAGDOLL_PART_LEFT_ARM,
        RAGDOLL_PART_RIGHT_ARM,
        RAGDOLL_PART_LEFT_LEG,
        RAGDOLL_PART_RIGHT_LEG
    } RagdollPartType;

    typedef enum
    {
        RAGDOLL_CONSTRAINT_DISTANCE,
        RAGDOLL_CONSTRAINT_BALL_SOCKET
    } RagdollConstraintType;

    typedef struct
    {
        Vec3 position;
        Vec3 prev_position;
        Vec3 velocity;
        Vec3 half_extents;
        float mass;
        float inv_mass;
    } RagdollPart;

    typedef struct
    {
        RagdollConstraintType type;
        int32_t part_a;
        int32_t part_b;
        Vec3 anchor_a;
        Vec3 anchor_b;
        float rest_length;
        float min_angle;
        float max_angle;
    } RagdollConstraint;

    typedef struct
    {
        RagdollPart parts[RAGDOLL_MAX_PARTS];
        RagdollConstraint constraints[RAGDOLL_MAX_CONSTRAINTS];
        int32_t part_count;
        int32_t constraint_count;
        bool active;
    } Ragdoll;

    typedef struct
    {
        Ragdoll ragdolls[RAGDOLL_MAX_RAGDOLLS];
        int32_t ragdoll_count;
        float gravity;
        float damping;
    } RagdollSystem;

    RagdollSystem *ragdoll_system_create(void);
    void ragdoll_system_destroy(RagdollSystem *system);

    int32_t ragdoll_spawn(RagdollSystem *system, Vec3 position, float scale);
    void ragdoll_despawn(RagdollSystem *system, int32_t ragdoll_index);

    void ragdoll_system_update(RagdollSystem *system, VoxelVolume *terrain, float dt);

    Ragdoll *ragdoll_get(RagdollSystem *system, int32_t ragdoll_index);
    int32_t ragdoll_system_active_count(RagdollSystem *system);

    void ragdoll_apply_impulse(RagdollSystem *system, int32_t ragdoll_index,
                               int32_t part_index, Vec3 impulse);

#ifdef __cplusplus
}
#endif

#endif
