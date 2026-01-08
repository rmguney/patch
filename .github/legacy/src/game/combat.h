#ifndef PATCH_GAME_COMBAT_H
#define PATCH_GAME_COMBAT_H

#include "../core/types.h"
#include "../core/math.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HAND_HITBOX_RADIUS 0.25f
#define HAND_HITBOX_LENGTH 0.4f
#define GRAB_HITBOX_RADIUS 0.4f

typedef struct {
    Vec3 position;
    float radius;
    bool active;
} Hitbox;

typedef struct {
    Vec3 start;
    Vec3 end;
    float radius;
} CapsuleHitbox;

CapsuleHitbox combat_get_punch_hitbox(Vec3 shoulder, Vec3 forward, float arm_length);

CapsuleHitbox combat_get_grab_hitbox(Vec3 shoulder, Vec3 forward, float arm_length);

bool combat_capsule_vs_sphere(const CapsuleHitbox* capsule, Vec3 sphere_center, float sphere_radius);

bool combat_capsule_vs_capsule(const CapsuleHitbox* a, const CapsuleHitbox* b);

bool combat_sphere_vs_sphere(Vec3 a_center, float a_radius, Vec3 b_center, float b_radius);

float combat_point_to_segment_dist(Vec3 point, Vec3 seg_start, Vec3 seg_end);

Vec3 combat_closest_point_on_segment(Vec3 point, Vec3 seg_start, Vec3 seg_end);

#ifdef __cplusplus
}
#endif

#endif
