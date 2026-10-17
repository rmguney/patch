#ifndef PATCH_CORE_MATH_H
#define PATCH_CORE_MATH_H

#include "types.h"
#include <math.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define K_PI 3.14159265358979323846f
#define K_DEG_TO_RAD 0.01745329251994329577f
#define K_EPSILON 0.0001f

    static inline Vec3 vec3_create(float x, float y, float z)
    {
        Vec3 v;
        v.x = x;
        v.y = y;
        v.z = z;
        return v;
    }

    static inline Vec3 vec3_zero(void)
    {
        return vec3_create(0.0f, 0.0f, 0.0f);
    }

    static inline Vec3 vec3_add(Vec3 a, Vec3 b)
    {
        return vec3_create(a.x + b.x, a.y + b.y, a.z + b.z);
    }

    static inline Vec3 vec3_sub(Vec3 a, Vec3 b)
    {
        return vec3_create(a.x - b.x, a.y - b.y, a.z - b.z);
    }

    static inline Vec3 vec3_scale(Vec3 v, float s)
    {
        return vec3_create(v.x * s, v.y * s, v.z * s);
    }

    static inline Vec3 vec3_lerp(Vec3 a, Vec3 b, float t)
    {
        return vec3_create(
            a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t);
    }

    static inline float vec3_dot(Vec3 a, Vec3 b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    static inline Vec3 vec3_cross(Vec3 a, Vec3 b)
    {
        return vec3_create(
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x);
    }

    static inline float vec3_length_sq(Vec3 v)
    {
        return vec3_dot(v, v);
    }

    static inline float vec3_length(Vec3 v)
    {
        return sqrtf(vec3_length_sq(v));
    }

    static inline Vec3 vec3_normalize(Vec3 v)
    {
        float len = vec3_length(v);
        if (len > K_EPSILON)
        {
            return vec3_scale(v, 1.0f / len);
        }
        return v;
    }

    static inline float clampf(float value, float min_val, float max_val)
    {
        if (value < min_val)
            return min_val;
        if (value > max_val)
            return max_val;
        return value;
    }

    static inline float minf(float a, float b)
    {
        return a < b ? a : b;
    }

    static inline float maxf(float a, float b)
    {
        return a > b ? a : b;
    }

    /* sRGB-encoded colour component (0-1, as colour pickers and the material table author
     * it) to linear light, the space the shaders light in */
    static inline float srgb_to_linear(float c)
    {
        if (c <= 0.04045f)
            return c / 12.92f;
        return powf((c + 0.055f) / 1.055f, 2.4f);
    }

    static inline Vec3 vec3_srgb_to_linear(Vec3 c)
    {
        return vec3_create(srgb_to_linear(c.x), srgb_to_linear(c.y), srgb_to_linear(c.z));
    }

    static inline Mat4 mat4_identity(void)
    {
        Mat4 m = {0};
        m.m[0] = 1.0f;
        m.m[5] = 1.0f;
        m.m[10] = 1.0f;
        m.m[15] = 1.0f;
        return m;
    }

    static inline Mat4 mat4_multiply(Mat4 a, Mat4 b)
    {
        Mat4 result = {0};
        for (int col = 0; col < 4; col++)
        {
            for (int row = 0; row < 4; row++)
            {
                float sum = 0.0f;
                for (int k = 0; k < 4; k++)
                {
                    sum += a.m[k * 4 + row] * b.m[col * 4 + k];
                }
                result.m[col * 4 + row] = sum;
            }
        }
        return result;
    }

    static inline Mat4 mat4_perspective(float fov_y_radians, float aspect, float near_val, float far_val)
    {
        float f = 1.0f / tanf(fov_y_radians * 0.5f);
        Mat4 m = {0};
        m.m[0] = f / aspect;
        m.m[5] = -f;
        m.m[10] = far_val / (near_val - far_val);
        m.m[11] = -1.0f;
        m.m[14] = (far_val * near_val) / (near_val - far_val);
        return m;
    }

    static inline Mat4 mat4_look_at(Vec3 eye, Vec3 center, Vec3 up)
    {
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

    static inline Vec3 mat4_transform_direction(Mat4 m, Vec3 v)
    {
        return vec3_create(
            m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z,
            m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z,
            m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z);
    }

    /* Inverse for rigid transforms (rotation + translation only); see mat4_inverse otherwise */
    static inline Mat4 mat4_inverse_rigid(Mat4 m)
    {
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

    /*
     * 3x3 rotation matrix helpers (compact rotation-only, no translation).
     * Used by renderers to transform voxel positions efficiently.
     * m[0-8] stored row-major: m[0],m[1],m[2] = row 0, etc.
     */
    static inline Vec3 mat3_transform_vec3(const float m[9], Vec3 p)
    {
        return vec3_create(
            m[0] * p.x + m[1] * p.y + m[2] * p.z,
            m[3] * p.x + m[4] * p.y + m[5] * p.z,
            m[6] * p.x + m[7] * p.y + m[8] * p.z);
    }

    static inline void mat3_transpose(const float m[9], float out[9])
    {
        out[0] = m[0];
        out[1] = m[3];
        out[2] = m[6];
        out[3] = m[1];
        out[4] = m[4];
        out[5] = m[7];
        out[6] = m[2];
        out[7] = m[5];
        out[8] = m[8];
    }

    /* Quaternion utilities for orientation storage and rotation matrices */
    static inline Quat quat_identity(void)
    {
        Quat q;
        q.x = 0.0f;
        q.y = 0.0f;
        q.z = 0.0f;
        q.w = 1.0f;
        return q;
    }

    static inline Quat quat_create(float x, float y, float z, float w)
    {
        Quat q;
        q.x = x;
        q.y = y;
        q.z = z;
        q.w = w;
        return q;
    }

    static inline float quat_length_sq(Quat q)
    {
        return q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    }

    static inline float quat_length(Quat q)
    {
        return sqrtf(quat_length_sq(q));
    }

    static inline Quat quat_normalize(Quat q)
    {
        float len = quat_length(q);
        if (len > K_EPSILON)
        {
            float inv = 1.0f / len;
            q.x *= inv;
            q.y *= inv;
            q.z *= inv;
            q.w *= inv;
        }
        return q;
    }

    /* Normalized linear blend from a to b along the shorter arc: for the small turns between
     * two simulation steps it follows slerp closely, at a fraction of the cost */
    static inline Quat quat_nlerp(Quat a, Quat b, float t)
    {
        const float sign = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0.0f ? -1.0f : 1.0f;
        return quat_normalize(quat_create(a.x + (b.x * sign - a.x) * t, a.y + (b.y * sign - a.y) * t,
                                          a.z + (b.z * sign - a.z) * t, a.w + (b.w * sign - a.w) * t));
    }

    static inline void quat_to_mat3(Quat q, float m[9])
    {
        float xx = q.x * q.x;
        float yy = q.y * q.y;
        float zz = q.z * q.z;
        float xy = q.x * q.y;
        float xz = q.x * q.z;
        float yz = q.y * q.z;
        float wx = q.w * q.x;
        float wy = q.w * q.y;
        float wz = q.w * q.z;

        m[0] = 1.0f - 2.0f * (yy + zz);
        m[1] = 2.0f * (xy - wz);
        m[2] = 2.0f * (xz + wy);
        m[3] = 2.0f * (xy + wz);
        m[4] = 1.0f - 2.0f * (xx + zz);
        m[5] = 2.0f * (yz - wx);
        m[6] = 2.0f * (xz - wy);
        m[7] = 2.0f * (yz + wx);
        m[8] = 1.0f - 2.0f * (xx + yy);
    }

    static inline float lerpf(float a, float b, float t)
    {
        return a + (b - a) * t;
    }

    static inline Vec3 vec3_clamp_length(Vec3 v, float max_len)
    {
        float len_sq = vec3_length_sq(v);
        if (len_sq > max_len * max_len && len_sq > K_EPSILON * K_EPSILON)
        {
            float inv_len = 1.0f / sqrtf(len_sq);
            return vec3_scale(v, max_len * inv_len);
        }
        return v;
    }

    static inline Vec3 vec3_neg(Vec3 v)
    {
        return vec3_create(-v.x, -v.y, -v.z);
    }

    static inline Vec3 vec3_mul(Vec3 a, Vec3 b)
    {
        return vec3_create(a.x * b.x, a.y * b.y, a.z * b.z);
    }

    static inline Vec3 quat_rotate_vec3(Quat q, Vec3 v)
    {
        Vec3 u = vec3_create(q.x, q.y, q.z);
        float s = q.w;
        Vec3 uv = vec3_cross(u, v);
        Vec3 uuv = vec3_cross(u, uv);
        return vec3_add(v, vec3_scale(vec3_add(vec3_scale(uv, s), uuv), 2.0f));
    }

    static inline Quat quat_integrate(Quat q, Vec3 omega, float dt)
    {
        float half_dt = dt * 0.5f;
        Quat dq;
        dq.x = half_dt * (omega.x * q.w + omega.y * q.z - omega.z * q.y);
        dq.y = half_dt * (omega.y * q.w + omega.z * q.x - omega.x * q.z);
        dq.z = half_dt * (omega.z * q.w + omega.x * q.y - omega.y * q.x);
        dq.w = half_dt * (-omega.x * q.x - omega.y * q.y - omega.z * q.z);
        Quat result;
        result.x = q.x + dq.x;
        result.y = q.y + dq.y;
        result.z = q.z + dq.z;
        result.w = q.w + dq.w;
        return quat_normalize(result);
    }

    static inline Quat quat_conjugate(Quat q)
    {
        return quat_create(-q.x, -q.y, -q.z, q.w);
    }

    /* World-space inverse inertia applied to v: R * diag(inv_inertia_local) * R^T * v,
     * where R (local -> world) is the rotation quat_rotate_vec3(q, .) applies. */
    static inline Vec3 quat_apply_inv_inertia(Quat q, Vec3 inv_inertia_local, Vec3 v)
    {
        Vec3 local = quat_rotate_vec3(quat_conjugate(q), v);
        return quat_rotate_vec3(q, vec3_mul(local, inv_inertia_local));
    }

    /* General 4x4 matrix inverse using cofactor expansion */
    static inline Mat4 mat4_inverse(Mat4 m)
    {
        float *a = m.m;
        Mat4 inv;
        float *o = inv.m;

        float s0 = a[0] * a[5] - a[4] * a[1];
        float s1 = a[0] * a[6] - a[4] * a[2];
        float s2 = a[0] * a[7] - a[4] * a[3];
        float s3 = a[1] * a[6] - a[5] * a[2];
        float s4 = a[1] * a[7] - a[5] * a[3];
        float s5 = a[2] * a[7] - a[6] * a[3];

        float c5 = a[10] * a[15] - a[14] * a[11];
        float c4 = a[9] * a[15] - a[13] * a[11];
        float c3 = a[9] * a[14] - a[13] * a[10];
        float c2 = a[8] * a[15] - a[12] * a[11];
        float c1 = a[8] * a[14] - a[12] * a[10];
        float c0 = a[8] * a[13] - a[12] * a[9];

        float det = s0 * c5 - s1 * c4 + s2 * c3 + s3 * c2 - s4 * c1 + s5 * c0;
        if (fabsf(det) < 1e-10f)
        {
            return mat4_identity();
        }
        float invdet = 1.0f / det;

        o[0] = (a[5] * c5 - a[6] * c4 + a[7] * c3) * invdet;
        o[1] = (-a[1] * c5 + a[2] * c4 - a[3] * c3) * invdet;
        o[2] = (a[13] * s5 - a[14] * s4 + a[15] * s3) * invdet;
        o[3] = (-a[9] * s5 + a[10] * s4 - a[11] * s3) * invdet;

        o[4] = (-a[4] * c5 + a[6] * c2 - a[7] * c1) * invdet;
        o[5] = (a[0] * c5 - a[2] * c2 + a[3] * c1) * invdet;
        o[6] = (-a[12] * s5 + a[14] * s2 - a[15] * s1) * invdet;
        o[7] = (a[8] * s5 - a[10] * s2 + a[11] * s1) * invdet;

        o[8] = (a[4] * c4 - a[5] * c2 + a[7] * c0) * invdet;
        o[9] = (-a[0] * c4 + a[1] * c2 - a[3] * c0) * invdet;
        o[10] = (a[12] * s4 - a[13] * s2 + a[15] * s0) * invdet;
        o[11] = (-a[8] * s4 + a[9] * s2 - a[11] * s0) * invdet;

        o[12] = (-a[4] * c3 + a[5] * c1 - a[6] * c0) * invdet;
        o[13] = (a[0] * c3 - a[1] * c1 + a[2] * c0) * invdet;
        o[14] = (-a[12] * s3 + a[13] * s1 - a[14] * s0) * invdet;
        o[15] = (a[8] * s3 - a[9] * s1 + a[10] * s0) * invdet;

        return inv;
    }

#ifdef __cplusplus
}
#endif

#endif
