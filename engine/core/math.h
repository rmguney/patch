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

/* Frustum plane indices */
#define FRUSTUM_LEFT 0
#define FRUSTUM_RIGHT 1
#define FRUSTUM_BOTTOM 2
#define FRUSTUM_TOP 3
#define FRUSTUM_NEAR 4
#define FRUSTUM_FAR 5

    /* Frustum culling result */
    typedef enum
    {
        FRUSTUM_OUTSIDE = 0,
        FRUSTUM_INTERSECT = 1,
        FRUSTUM_INSIDE = 2
    } FrustumResult;

    /* Frustum defined by 6 planes (ax + by + cz + d = 0, normal points inward) */
    typedef struct
    {
        Vec4 planes[6];
    } Frustum;

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

    static inline Mat4 mat4_identity(void)
    {
        Mat4 m = {0};
        m.m[0] = 1.0f;
        m.m[5] = 1.0f;
        m.m[10] = 1.0f;
        m.m[15] = 1.0f;
        return m;
    }

    static inline Mat4 mat4_translation(Vec3 t)
    {
        Mat4 m = mat4_identity();
        m.m[12] = t.x;
        m.m[13] = t.y;
        m.m[14] = t.z;
        return m;
    }

    static inline Mat4 mat4_scaling(Vec3 s)
    {
        Mat4 m = {0};
        m.m[0] = s.x;
        m.m[5] = s.y;
        m.m[10] = s.z;
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

    static inline Mat4 mat4_ortho(float left, float right, float bottom, float top, float near_val, float far_val)
    {
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

    static inline Vec3 mat4_transform_point(Mat4 m, Vec3 p)
    {
        return vec3_create(
            m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12],
            m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13],
            m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14]);
    }

    static inline Vec3 mat4_transform_direction(Mat4 m, Vec3 v)
    {
        return vec3_create(
            m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z,
            m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z,
            m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z);
    }

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

    static inline void mat3_identity(float m[9])
    {
        m[0] = 1.0f;
        m[1] = 0.0f;
        m[2] = 0.0f;
        m[3] = 0.0f;
        m[4] = 1.0f;
        m[5] = 0.0f;
        m[6] = 0.0f;
        m[7] = 0.0f;
        m[8] = 1.0f;
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

    static inline void mat3_multiply(const float a[9], const float b[9], float out[9])
    {
        float tmp[9];
        tmp[0] = a[0] * b[0] + a[1] * b[3] + a[2] * b[6];
        tmp[1] = a[0] * b[1] + a[1] * b[4] + a[2] * b[7];
        tmp[2] = a[0] * b[2] + a[1] * b[5] + a[2] * b[8];
        tmp[3] = a[3] * b[0] + a[4] * b[3] + a[5] * b[6];
        tmp[4] = a[3] * b[1] + a[4] * b[4] + a[5] * b[7];
        tmp[5] = a[3] * b[2] + a[4] * b[5] + a[5] * b[8];
        tmp[6] = a[6] * b[0] + a[7] * b[3] + a[8] * b[6];
        tmp[7] = a[6] * b[1] + a[7] * b[4] + a[8] * b[7];
        tmp[8] = a[6] * b[2] + a[7] * b[5] + a[8] * b[8];
        for (int i = 0; i < 9; i++)
            out[i] = tmp[i];
    }

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

    static inline Quat quat_from_axis_angle(Vec3 axis, float radians)
    {
        float half_angle = radians * 0.5f;
        float s = sinf(half_angle);
        Quat q;
        q.x = axis.x * s;
        q.y = axis.y * s;
        q.z = axis.z * s;
        q.w = cosf(half_angle);
        return q;
    }

    static inline Quat quat_multiply(Quat a, Quat b)
    {
        Quat q;
        q.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
        q.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y;
        q.y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x;
        q.z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w;
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

    static inline void quat_integrate(Quat *q, Vec3 w, float dt)
    {
        float omega = vec3_length(w);
        float angle = omega * dt;
        if (angle < 0.0001f)
            return;

        Vec3 axis = vec3_scale(w, 1.0f / omega);
        Quat delta = quat_from_axis_angle(axis, angle);
        *q = quat_multiply(delta, *q);
        *q = quat_normalize(*q);
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

    static inline void quat_to_mat4(Quat q, Mat4 *m)
    {
        float rot[9];
        quat_to_mat3(q, rot);
        m->m[0] = rot[0];
        m->m[1] = rot[3];
        m->m[2] = rot[6];
        m->m[3] = 0.0f;
        m->m[4] = rot[1];
        m->m[5] = rot[4];
        m->m[6] = rot[7];
        m->m[7] = 0.0f;
        m->m[8] = rot[2];
        m->m[9] = rot[5];
        m->m[10] = rot[8];
        m->m[11] = 0.0f;
        m->m[12] = 0.0f;
        m->m[13] = 0.0f;
        m->m[14] = 0.0f;
        m->m[15] = 1.0f;
    }

    static inline float lerpf(float a, float b, float t)
    {
        return a + (b - a) * t;
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

    /*
     * Extract frustum planes from view-projection matrix.
     * Planes are stored with normals pointing inward (toward visible space).
     * Uses Gribb/Hartmann method for clip-space plane extraction.
     */
    static inline Frustum frustum_from_view_proj(Mat4 vp)
    {
        Frustum f;
        float *m = vp.m;

        /* Left:   row3 + row0 */
        f.planes[FRUSTUM_LEFT].x = m[3] + m[0];
        f.planes[FRUSTUM_LEFT].y = m[7] + m[4];
        f.planes[FRUSTUM_LEFT].z = m[11] + m[8];
        f.planes[FRUSTUM_LEFT].w = m[15] + m[12];

        /* Right:  row3 - row0 */
        f.planes[FRUSTUM_RIGHT].x = m[3] - m[0];
        f.planes[FRUSTUM_RIGHT].y = m[7] - m[4];
        f.planes[FRUSTUM_RIGHT].z = m[11] - m[8];
        f.planes[FRUSTUM_RIGHT].w = m[15] - m[12];

        /* Bottom: row3 + row1 */
        f.planes[FRUSTUM_BOTTOM].x = m[3] + m[1];
        f.planes[FRUSTUM_BOTTOM].y = m[7] + m[5];
        f.planes[FRUSTUM_BOTTOM].z = m[11] + m[9];
        f.planes[FRUSTUM_BOTTOM].w = m[15] + m[13];

        /* Top:    row3 - row1 */
        f.planes[FRUSTUM_TOP].x = m[3] - m[1];
        f.planes[FRUSTUM_TOP].y = m[7] - m[5];
        f.planes[FRUSTUM_TOP].z = m[11] - m[9];
        f.planes[FRUSTUM_TOP].w = m[15] - m[13];

        /* Near:   row3 + row2 */
        f.planes[FRUSTUM_NEAR].x = m[3] + m[2];
        f.planes[FRUSTUM_NEAR].y = m[7] + m[6];
        f.planes[FRUSTUM_NEAR].z = m[11] + m[10];
        f.planes[FRUSTUM_NEAR].w = m[15] + m[14];

        /* Far:    row3 - row2 */
        f.planes[FRUSTUM_FAR].x = m[3] - m[2];
        f.planes[FRUSTUM_FAR].y = m[7] - m[6];
        f.planes[FRUSTUM_FAR].z = m[11] - m[10];
        f.planes[FRUSTUM_FAR].w = m[15] - m[14];

        /* Normalize planes for correct distance calculations */
        for (int i = 0; i < 6; i++)
        {
            float len = sqrtf(f.planes[i].x * f.planes[i].x +
                              f.planes[i].y * f.planes[i].y +
                              f.planes[i].z * f.planes[i].z);
            if (len > K_EPSILON)
            {
                float inv_len = 1.0f / len;
                f.planes[i].x *= inv_len;
                f.planes[i].y *= inv_len;
                f.planes[i].z *= inv_len;
                f.planes[i].w *= inv_len;
            }
        }

        return f;
    }

    /*
     * Test AABB against frustum.
     * Returns FRUSTUM_OUTSIDE if completely outside, FRUSTUM_INSIDE if completely inside,
     * FRUSTUM_INTERSECT if partially visible.
     */
    static inline FrustumResult frustum_test_aabb(const Frustum *f, Bounds3D bounds)
    {
        FrustumResult result = FRUSTUM_INSIDE;

        for (int i = 0; i < 6; i++)
        {
            Vec4 p = f->planes[i];

            /* Find p-vertex (vertex furthest along plane normal) */
            float px = (p.x >= 0.0f) ? bounds.max_x : bounds.min_x;
            float py = (p.y >= 0.0f) ? bounds.max_y : bounds.min_y;
            float pz = (p.z >= 0.0f) ? bounds.max_z : bounds.min_z;

            /* If p-vertex is outside, entire AABB is outside */
            float dist_p = p.x * px + p.y * py + p.z * pz + p.w;
            if (dist_p < 0.0f)
            {
                return FRUSTUM_OUTSIDE;
            }

            /* Find n-vertex (vertex closest along plane normal) */
            float nx = (p.x >= 0.0f) ? bounds.min_x : bounds.max_x;
            float ny = (p.y >= 0.0f) ? bounds.min_y : bounds.max_y;
            float nz = (p.z >= 0.0f) ? bounds.min_z : bounds.max_z;

            /* If n-vertex is outside, AABB intersects this plane */
            float dist_n = p.x * nx + p.y * ny + p.z * nz + p.w;
            if (dist_n < 0.0f)
            {
                result = FRUSTUM_INTERSECT;
            }
        }

        return result;
    }

    /*
     * Test if AABB is entirely behind a plane (camera near plane for "behind camera" test).
     * Plane defined as point + normal (normal points toward visible space).
     */
    static inline bool bounds_behind_plane(Bounds3D bounds, Vec3 plane_point, Vec3 plane_normal)
    {
        /* Find the vertex furthest along the plane normal */
        float px = (plane_normal.x >= 0.0f) ? bounds.max_x : bounds.min_x;
        float py = (plane_normal.y >= 0.0f) ? bounds.max_y : bounds.min_y;
        float pz = (plane_normal.z >= 0.0f) ? bounds.max_z : bounds.min_z;

        /* If even the furthest vertex is behind the plane, entire bounds is behind */
        Vec3 to_p = vec3_create(px - plane_point.x, py - plane_point.y, pz - plane_point.z);
        return vec3_dot(to_p, plane_normal) < 0.0f;
    }

    /*
     * Get chunk bounds in world space from chunk coordinates.
     */
    static inline Bounds3D chunk_world_bounds(int32_t cx, int32_t cy, int32_t cz,
                                              Vec3 volume_min, float voxel_size)
    {
        float chunk_size = 32.0f * voxel_size;
        Bounds3D b;
        b.min_x = volume_min.x + cx * chunk_size;
        b.min_y = volume_min.y + cy * chunk_size;
        b.min_z = volume_min.z + cz * chunk_size;
        b.max_x = b.min_x + chunk_size;
        b.max_y = b.min_y + chunk_size;
        b.max_z = b.min_z + chunk_size;
        return b;
    }

#ifdef __cplusplus
}
#endif

#endif
