#include "combat.h"
#include <math.h>

Vec3 combat_closest_point_on_segment(Vec3 point, Vec3 seg_start, Vec3 seg_end) {
    Vec3 ab = vec3_sub(seg_end, seg_start);
    Vec3 ap = vec3_sub(point, seg_start);
    float ab_len_sq = vec3_dot(ab, ab);
    
    if (ab_len_sq < 0.0001f) {
        return seg_start;
    }
    
    float t = clampf(vec3_dot(ap, ab) / ab_len_sq, 0.0f, 1.0f);
    return vec3_add(seg_start, vec3_scale(ab, t));
}

float combat_point_to_segment_dist(Vec3 point, Vec3 seg_start, Vec3 seg_end) {
    Vec3 closest = combat_closest_point_on_segment(point, seg_start, seg_end);
    return vec3_length(vec3_sub(point, closest));
}

CapsuleHitbox combat_get_punch_hitbox(Vec3 shoulder, Vec3 forward, float arm_length) {
    CapsuleHitbox hitbox;
    
    hitbox.start = vec3_add(shoulder, vec3_scale(forward, arm_length * 0.3f));
    hitbox.end = vec3_add(shoulder, vec3_scale(forward, arm_length + HAND_HITBOX_LENGTH));
    hitbox.radius = HAND_HITBOX_RADIUS;
    
    return hitbox;
}

CapsuleHitbox combat_get_grab_hitbox(Vec3 shoulder, Vec3 forward, float arm_length) {
    CapsuleHitbox hitbox;
    
    hitbox.start = vec3_add(shoulder, vec3_scale(forward, arm_length * 0.5f));
    hitbox.end = vec3_add(shoulder, vec3_scale(forward, arm_length + HAND_HITBOX_LENGTH * 0.8f));
    hitbox.radius = GRAB_HITBOX_RADIUS;
    
    return hitbox;
}

bool combat_capsule_vs_sphere(const CapsuleHitbox* capsule, Vec3 sphere_center, float sphere_radius) {
    float dist = combat_point_to_segment_dist(sphere_center, capsule->start, capsule->end);
    return dist < (capsule->radius + sphere_radius);
}

bool combat_capsule_vs_capsule(const CapsuleHitbox* a, const CapsuleHitbox* b) {
    Vec3 d1 = vec3_sub(a->end, a->start);
    Vec3 d2 = vec3_sub(b->end, b->start);
    Vec3 r = vec3_sub(a->start, b->start);
    
    float a_len_sq = vec3_dot(d1, d1);
    float e = vec3_dot(d2, d2);
    float f = vec3_dot(d2, r);
    
    float s, t;
    
    if (a_len_sq < 0.0001f && e < 0.0001f) {
        s = t = 0.0f;
    } else if (a_len_sq < 0.0001f) {
        s = 0.0f;
        t = clampf(f / e, 0.0f, 1.0f);
    } else {
        float c = vec3_dot(d1, r);
        if (e < 0.0001f) {
            t = 0.0f;
            s = clampf(-c / a_len_sq, 0.0f, 1.0f);
        } else {
            float b_val = vec3_dot(d1, d2);
            float denom = a_len_sq * e - b_val * b_val;
            
            if (denom != 0.0f) {
                s = clampf((b_val * f - c * e) / denom, 0.0f, 1.0f);
            } else {
                s = 0.0f;
            }
            
            t = (b_val * s + f) / e;
            
            if (t < 0.0f) {
                t = 0.0f;
                s = clampf(-c / a_len_sq, 0.0f, 1.0f);
            } else if (t > 1.0f) {
                t = 1.0f;
                s = clampf((b_val - c) / a_len_sq, 0.0f, 1.0f);
            }
        }
    }
    
    Vec3 c1 = vec3_add(a->start, vec3_scale(d1, s));
    Vec3 c2 = vec3_add(b->start, vec3_scale(d2, t));
    
    float dist = vec3_length(vec3_sub(c1, c2));
    return dist < (a->radius + b->radius);
}

bool combat_sphere_vs_sphere(Vec3 a_center, float a_radius, Vec3 b_center, float b_radius) {
    float dist = vec3_length(vec3_sub(a_center, b_center));
    return dist < (a_radius + b_radius);
}
