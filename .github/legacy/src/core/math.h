#ifndef PATCH_CORE_MATH_H
#define PATCH_CORE_MATH_H

#include "types.h"
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

#define K_PI 3.14159265358979323846f
#define K_DEG_TO_RAD 0.01745329251994329577f
#define K_EPSILON 0.0001f

static inline Vec3 vec3_create(float x, float y, float z) {
    Vec3 v;
    v.x = x;
    v.y = y;
    v.z = z;
    return v;
}

static inline Vec3 vec3_zero(void) {
    return vec3_create(0.0f, 0.0f, 0.0f);
}

static inline Vec3 vec3_add(Vec3 a, Vec3 b) {
    return vec3_create(a.x + b.x, a.y + b.y, a.z + b.z);
}

static inline Vec3 vec3_sub(Vec3 a, Vec3 b) {
    return vec3_create(a.x - b.x, a.y - b.y, a.z - b.z);
}

static inline Vec3 vec3_scale(Vec3 v, float s) {
    return vec3_create(v.x * s, v.y * s, v.z * s);
}

static inline Vec3 vec3_negate(Vec3 v) {
    return vec3_create(-v.x, -v.y, -v.z);
}

static inline float vec3_dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static inline Vec3 vec3_cross(Vec3 a, Vec3 b) {
    return vec3_create(
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    );
}

static inline float vec3_length_sq(Vec3 v) {
    return vec3_dot(v, v);
}

static inline float vec3_length(Vec3 v) {
    return sqrtf(vec3_length_sq(v));
}

static inline Vec3 vec3_normalize(Vec3 v) {
    float len = vec3_length(v);
    if (len > K_EPSILON) {
        return vec3_scale(v, 1.0f / len);
    }
    return v;
}

static inline float clampf(float value, float min_val, float max_val) {
    if (value < min_val) return min_val;
    if (value > max_val) return max_val;
    return value;
}

static inline float minf(float a, float b) {
    return a < b ? a : b;
}

static inline float maxf(float a, float b) {
    return a > b ? a : b;
}

static inline Mat4 mat4_identity(void) {
    Mat4 m = {0};
    m.m[0] = 1.0f;
    m.m[5] = 1.0f;
    m.m[10] = 1.0f;
    m.m[15] = 1.0f;
    return m;
}

static inline Mat4 mat4_translation(Vec3 t) {
    Mat4 m = mat4_identity();
    m.m[12] = t.x;
    m.m[13] = t.y;
    m.m[14] = t.z;
    return m;
}

static inline Mat4 mat4_scaling(Vec3 s) {
    Mat4 m = {0};
    m.m[0] = s.x;
    m.m[5] = s.y;
    m.m[10] = s.z;
    m.m[15] = 1.0f;
    return m;
}

static inline Mat4 mat4_multiply(Mat4 a, Mat4 b) {
    Mat4 result = {0};
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            float sum = 0.0f;
            for (int k = 0; k < 4; k++) {
                sum += a.m[k * 4 + row] * b.m[col * 4 + k];
            }
            result.m[col * 4 + row] = sum;
        }
    }
    return result;
}

static inline Mat4 mat4_ortho(float left, float right, float bottom, float top, float near_val, float far_val) {
    Mat4 m = {0};
    m.m[0] = 2.0f / (right - left);
    m.m[5] = -2.0f / (top - bottom);
    m.m[10] = -1.0f / (far_val - near_val);
    m.m[12] = -(right + left) / (right - left);
    m.m[13] = -(top + bottom) / (top - bottom);
    m.m[14] = -near_val / (far_val - near_val);
    m.m[15] = 1.0f;
    return m;
}

static inline Mat4 mat4_look_at(Vec3 eye, Vec3 center, Vec3 up) {
    Vec3 f = vec3_normalize(vec3_sub(center, eye));
    Vec3 s = vec3_normalize(vec3_cross(f, up));
    Vec3 u = vec3_cross(s, f);

    Mat4 m = mat4_identity();
    m.m[0] = s.x;
    m.m[4] = s.y;
    m.m[8] = s.z;
    m.m[1] = u.x;
    m.m[5] = u.y;
    m.m[9] = u.z;
    m.m[2] = -f.x;
    m.m[6] = -f.y;
    m.m[10] = -f.z;
    m.m[12] = -vec3_dot(s, eye);
    m.m[13] = -vec3_dot(u, eye);
    m.m[14] = vec3_dot(f, eye);
    return m;
}

static inline Vec3 mat4_transform_point(Mat4 m, Vec3 p) {
    return vec3_create(
        m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12],
        m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13],
        m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14]
    );
}

static inline Vec3 mat4_transform_direction(Mat4 m, Vec3 v) {
    return vec3_create(
        m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z,
        m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z,
        m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z
    );
}

static inline Mat4 mat4_inverse_rigid(Mat4 m) {
    Mat4 inv = mat4_identity();

    inv.m[0] = m.m[0];
    inv.m[1] = m.m[4];
    inv.m[2] = m.m[8];
    inv.m[4] = m.m[1];
    inv.m[5] = m.m[5];
    inv.m[6] = m.m[9];
    inv.m[8] = m.m[2];
    inv.m[9] = m.m[6];
    inv.m[10] = m.m[10];

    Vec3 t = vec3_create(m.m[12], m.m[13], m.m[14]);
    inv.m[12] = -(inv.m[0] * t.x + inv.m[4] * t.y + inv.m[8] * t.z);
    inv.m[13] = -(inv.m[1] * t.x + inv.m[5] * t.y + inv.m[9] * t.z);
    inv.m[14] = -(inv.m[2] * t.x + inv.m[6] * t.y + inv.m[10] * t.z);

    return inv;
}

static inline Mat4 mat4_rotation_x(float angle) {
    Mat4 m = mat4_identity();
    float c = cosf(angle);
    float s = sinf(angle);
    m.m[5] = c;
    m.m[6] = s;
    m.m[9] = -s;
    m.m[10] = c;
    return m;
}

static inline Mat4 mat4_rotation_y(float angle) {
    Mat4 m = mat4_identity();
    float c = cosf(angle);
    float s = sinf(angle);
    m.m[0] = c;
    m.m[2] = -s;
    m.m[8] = s;
    m.m[10] = c;
    return m;
}

static inline Mat4 mat4_rotation_z(float angle) {
    Mat4 m = mat4_identity();
    float c = cosf(angle);
    float s = sinf(angle);
    m.m[0] = c;
    m.m[1] = s;
    m.m[4] = -s;
    m.m[5] = c;
    return m;
}

static inline Mat4 mat4_rotation_euler(Vec3 r) {
    Mat4 rx = mat4_rotation_x(r.x);
    Mat4 ry = mat4_rotation_y(r.y);
    Mat4 rz = mat4_rotation_z(r.z);
    return mat4_multiply(mat4_multiply(rz, ry), rx);
}

static inline float angle_wrap(float angle) {
    while (angle > K_PI) angle -= 2.0f * K_PI;
    while (angle < -K_PI) angle += 2.0f * K_PI;
    return angle;
}

static inline float lerp_angle(float from, float to, float t) {
    float diff = angle_wrap(to - from);
    return from + diff * t;
}

static inline float lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

#ifdef __cplusplus
}
#endif

#endif
