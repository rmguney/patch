#include "convex_hull.h"
#include <stdlib.h>
#include <string.h>

#define QH_MAX_FACES 512
#define QH_MAX_HORIZON 256
#define QH_EPSILON 1e-6f

/* Triangle wound counter-clockwise seen from outside: normal = (v1 - v0) x (v2 - v0) */
typedef struct
{
    int32_t v[3];
    Vec3 normal;
} QHFace;

typedef struct
{
    int32_t v0, v1;
} QHEdge;

static Vec3 qh_compute_normal(Vec3 a, Vec3 b, Vec3 c)
{
    Vec3 n = vec3_cross(vec3_sub(b, a), vec3_sub(c, a));
    float len = vec3_length(n);
    if (len > QH_EPSILON)
        return vec3_scale(n, 1.0f / len);
    return vec3_create(0.0f, 1.0f, 0.0f);
}

static float qh_signed_distance(Vec3 p, Vec3 face_point, Vec3 normal)
{
    return vec3_dot(vec3_sub(p, face_point), normal);
}

static int32_t qh_find_extreme_point(const Vec3 *points, int32_t count, Vec3 dir)
{
    int32_t best = 0;
    float best_dot = vec3_dot(points[0], dir);
    for (int32_t i = 1; i < count; i++)
    {
        float d = vec3_dot(points[i], dir);
        if (d > best_dot)
        {
            best_dot = d;
            best = i;
        }
    }
    return best;
}

static bool qh_build_initial_tetrahedron(const Vec3 *points, int32_t count, int32_t out_indices[4])
{
    int32_t idx0 = qh_find_extreme_point(points, count, vec3_create(1.0f, 0.0f, 0.0f));
    int32_t idx1 = qh_find_extreme_point(points, count, vec3_create(-1.0f, 0.0f, 0.0f));
    if (idx0 == idx1)
        idx1 = qh_find_extreme_point(points, count, vec3_create(0.0f, 1.0f, 0.0f));

    Vec3 line = vec3_sub(points[idx1], points[idx0]);
    float line_len = vec3_length(line);
    if (line_len < QH_EPSILON)
        return false;
    Vec3 line_dir = vec3_scale(line, 1.0f / line_len);

    int32_t idx2 = -1;
    float max_dist = QH_EPSILON;
    for (int32_t i = 0; i < count; i++)
    {
        Vec3 to_p = vec3_sub(points[i], points[idx0]);
        Vec3 perp = vec3_sub(to_p, vec3_scale(line_dir, vec3_dot(to_p, line_dir)));
        float dist = vec3_length(perp);
        if (dist > max_dist)
        {
            max_dist = dist;
            idx2 = i;
        }
    }
    if (idx2 < 0)
        return false;

    Vec3 plane_normal = qh_compute_normal(points[idx0], points[idx1], points[idx2]);
    int32_t idx3 = -1;
    max_dist = QH_EPSILON;
    for (int32_t i = 0; i < count; i++)
    {
        float dist = fabsf(qh_signed_distance(points[i], points[idx0], plane_normal));
        if (dist > max_dist)
        {
            max_dist = dist;
            idx3 = i;
        }
    }
    if (idx3 < 0)
        return false;

    out_indices[0] = idx0;
    out_indices[1] = idx1;
    out_indices[2] = idx2;
    out_indices[3] = idx3;
    return true;
}

static void hull_copy_points(const Vec3 *points, int32_t count, ConvexHull *out)
{
    out->vertex_count = count < HULL_MAX_VERTICES ? count : HULL_MAX_VERTICES;
    for (int32_t i = 0; i < out->vertex_count; i++)
        out->vertices[i] = points[i];
}

static bool qh_edge_shared(const QHFace *f, int32_t a, int32_t b)
{
    for (int32_t e = 0; e < 3; e++)
    {
        int32_t v0 = f->v[e];
        int32_t v1 = f->v[(e + 1) % 3];
        if ((v0 == a && v1 == b) || (v0 == b && v1 == a))
            return true;
    }
    return false;
}

void convex_hull_build(const Vec3 *points, int32_t count, ConvexHull *out)
{
    memset(out, 0, sizeof(ConvexHull));

    int32_t initial[4];
    if (count < 4 || !qh_build_initial_tetrahedron(points, count, initial))
    {
        hull_copy_points(points, count, out);
        return;
    }

    for (int32_t i = 0; i < 4; i++)
        out->vertices[i] = points[initial[i]];
    out->vertex_count = 4;

    QHFace faces[QH_MAX_FACES];
    int32_t face_count = 0;

    /* Orient the tetrahedron's faces away from its centroid */
    Vec3 center = vec3_scale(vec3_add(vec3_add(out->vertices[0], out->vertices[1]),
                                      vec3_add(out->vertices[2], out->vertices[3])),
                             0.25f);
    static const int32_t tet_faces[4][3] = {{0, 1, 2}, {0, 3, 1}, {1, 3, 2}, {2, 3, 0}};
    for (int32_t i = 0; i < 4; i++)
    {
        QHFace *f = &faces[face_count++];
        f->v[0] = tet_faces[i][0];
        f->v[1] = tet_faces[i][1];
        f->v[2] = tet_faces[i][2];
        f->normal = qh_compute_normal(out->vertices[f->v[0]], out->vertices[f->v[1]], out->vertices[f->v[2]]);
        if (qh_signed_distance(center, out->vertices[f->v[0]], f->normal) > 0.0f)
        {
            f->normal = vec3_neg(f->normal);
            int32_t tmp = f->v[1];
            f->v[1] = f->v[2];
            f->v[2] = tmp;
        }
    }

    /* done[i]: point i is a hull vertex, or inside the hull. The hull only grows, so a
     * point once inside stays inside and is never looked at again. */
    bool *done = (bool *)calloc((size_t)count, sizeof(bool));
    if (!done)
        return; /* keeps the tetrahedron, a valid if smaller hull */
    for (int32_t i = 0; i < 4; i++)
        done[initial[i]] = true;

    while (out->vertex_count < HULL_MAX_VERTICES)
    {
        /* Furthest point outside any face */
        int32_t best_point = -1;
        float best_dist = QH_EPSILON;
        for (int32_t pi = 0; pi < count; pi++)
        {
            if (done[pi])
                continue;

            float max_d = 0.0f;
            for (int32_t fi = 0; fi < face_count; fi++)
            {
                float d = qh_signed_distance(points[pi], out->vertices[faces[fi].v[0]], faces[fi].normal);
                if (d > max_d)
                    max_d = d;
            }

            if (max_d <= QH_EPSILON)
                done[pi] = true;
            else if (max_d > best_dist)
            {
                best_dist = max_d;
                best_point = pi;
            }
        }

        if (best_point < 0)
            break;

        done[best_point] = true;
        int32_t new_vert = out->vertex_count++;
        Vec3 p = points[best_point];
        out->vertices[new_vert] = p;

        bool visible[QH_MAX_FACES];
        for (int32_t fi = 0; fi < face_count; fi++)
            visible[fi] = qh_signed_distance(p, out->vertices[faces[fi].v[0]], faces[fi].normal) > QH_EPSILON;

        /* Horizon: edges of visible faces that a hidden face shares, kept in the visible
         * face's winding so the new faces come out wound the same way */
        QHEdge horizon[QH_MAX_HORIZON];
        int32_t horizon_count = 0;
        for (int32_t fi = 0; fi < face_count; fi++)
        {
            if (!visible[fi])
                continue;

            for (int32_t e = 0; e < 3 && horizon_count < QH_MAX_HORIZON; e++)
            {
                int32_t a = faces[fi].v[e];
                int32_t b = faces[fi].v[(e + 1) % 3];
                for (int32_t fj = 0; fj < face_count; fj++)
                {
                    if (!visible[fj] && qh_edge_shared(&faces[fj], a, b))
                    {
                        horizon[horizon_count++] = (QHEdge){a, b};
                        break;
                    }
                }
            }
        }

        /* Drop the visible faces and compact, so retired faces do not use up
         * QH_MAX_FACES and silently stop new faces from being added */
        int32_t write_idx = 0;
        for (int32_t fi = 0; fi < face_count; fi++)
        {
            if (!visible[fi])
                faces[write_idx++] = faces[fi];
        }
        face_count = write_idx;

        for (int32_t hi = 0; hi < horizon_count && face_count < QH_MAX_FACES; hi++)
        {
            QHFace *nf = &faces[face_count++];
            nf->v[0] = horizon[hi].v0;
            nf->v[1] = horizon[hi].v1;
            nf->v[2] = new_vert;
            nf->normal = qh_compute_normal(out->vertices[nf->v[0]], out->vertices[nf->v[1]], p);
        }
    }

    free(done);
}

int32_t convex_hull_support(const ConvexHull *hull, Vec3 dir)
{
    if (hull->vertex_count == 0)
        return -1;

    /* Linear scan over at most HULL_MAX_VERTICES points: hill-climbing over an adjacency
     * graph stalls at local maxima on vertices with many neighbours, and degenerate
     * (flat) inputs have no adjacency at all. */
    int32_t best = 0;
    float best_dot = vec3_dot(hull->vertices[0], dir);
    for (int32_t i = 1; i < hull->vertex_count; i++)
    {
        float d = vec3_dot(hull->vertices[i], dir);
        if (d > best_dot)
        {
            best_dot = d;
            best = i;
        }
    }
    return best;
}

Vec3 convex_hull_support_point(const ConvexHull *hull, Vec3 dir, Vec3 position, Quat orientation)
{
    Vec3 local_dir = quat_rotate_vec3(quat_conjugate(orientation), dir);

    int32_t idx = convex_hull_support(hull, local_dir);
    if (idx < 0)
        return position;

    Vec3 local_point = hull->vertices[idx];
    if (hull->margin > 0.0f)
    {
        float dir_len = vec3_length(local_dir);
        if (dir_len > QH_EPSILON)
            local_point = vec3_add(local_point, vec3_scale(local_dir, hull->margin / dir_len));
    }

    return vec3_add(position, quat_rotate_vec3(orientation, local_point));
}
