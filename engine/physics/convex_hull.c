#include "convex_hull.h"
#include <string.h>
#include <stdlib.h>

#define QH_MAX_FACES 512
#define QH_EPSILON 1e-6f

typedef struct
{
    int32_t v[3];
    Vec3 normal;
    float dist;
    bool active;
} QHFace;

static Vec3 qh_compute_normal(Vec3 a, Vec3 b, Vec3 c)
{
    Vec3 ab = vec3_sub(b, a);
    Vec3 ac = vec3_sub(c, a);
    Vec3 n = vec3_cross(ab, ac);
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

static bool qh_build_initial_tetrahedron(const Vec3 *points, int32_t count,
                                          int32_t *out_indices, Vec3 *out_verts)
{
    if (count < 4)
        return false;

    int32_t idx0 = qh_find_extreme_point(points, count, vec3_create(1, 0, 0));
    int32_t idx1 = qh_find_extreme_point(points, count, vec3_create(-1, 0, 0));

    if (idx0 == idx1)
    {
        idx1 = qh_find_extreme_point(points, count, vec3_create(0, 1, 0));
    }

    Vec3 line_dir = vec3_normalize(vec3_sub(points[idx1], points[idx0]));
    int32_t idx2 = -1;
    float max_dist = 0.0f;

    for (int32_t i = 0; i < count; i++)
    {
        if (i == idx0 || i == idx1)
            continue;
        Vec3 to_p = vec3_sub(points[i], points[idx0]);
        Vec3 proj = vec3_scale(line_dir, vec3_dot(to_p, line_dir));
        Vec3 perp = vec3_sub(to_p, proj);
        float dist = vec3_length(perp);
        if (dist > max_dist)
        {
            max_dist = dist;
            idx2 = i;
        }
    }

    if (idx2 < 0 || max_dist < QH_EPSILON)
        return false;

    Vec3 plane_normal = qh_compute_normal(points[idx0], points[idx1], points[idx2]);
    int32_t idx3 = -1;
    max_dist = 0.0f;

    for (int32_t i = 0; i < count; i++)
    {
        if (i == idx0 || i == idx1 || i == idx2)
            continue;
        float dist = fabsf(qh_signed_distance(points[i], points[idx0], plane_normal));
        if (dist > max_dist)
        {
            max_dist = dist;
            idx3 = i;
        }
    }

    if (idx3 < 0 || max_dist < QH_EPSILON)
        return false;

    out_indices[0] = idx0;
    out_indices[1] = idx1;
    out_indices[2] = idx2;
    out_indices[3] = idx3;
    out_verts[0] = points[idx0];
    out_verts[1] = points[idx1];
    out_verts[2] = points[idx2];
    out_verts[3] = points[idx3];

    return true;
}

void convex_hull_build(const Vec3 *points, int32_t count, ConvexHull *out)
{
    memset(out, 0, sizeof(ConvexHull));

    if (count < 4)
    {
        for (int32_t i = 0; i < count && i < HULL_MAX_VERTICES; i++)
        {
            out->vertices[i] = points[i];
        }
        out->vertex_count = count < HULL_MAX_VERTICES ? count : HULL_MAX_VERTICES;
        return;
    }

    int32_t initial_idx[4];
    Vec3 initial_verts[4];
    if (!qh_build_initial_tetrahedron(points, count, initial_idx, initial_verts))
    {
        for (int32_t i = 0; i < count && i < HULL_MAX_VERTICES; i++)
        {
            out->vertices[i] = points[i];
        }
        out->vertex_count = count < HULL_MAX_VERTICES ? count : HULL_MAX_VERTICES;
        return;
    }

    for (int32_t i = 0; i < 4; i++)
    {
        out->vertices[i] = initial_verts[i];
    }
    out->vertex_count = 4;

    QHFace faces[QH_MAX_FACES];
    int32_t face_count = 0;

    Vec3 center = vec3_scale(vec3_add(vec3_add(initial_verts[0], initial_verts[1]),
                                       vec3_add(initial_verts[2], initial_verts[3])),
                             0.25f);

    int32_t tet_faces[4][3] = {
        {0, 1, 2},
        {0, 3, 1},
        {1, 3, 2},
        {2, 3, 0}};

    for (int32_t i = 0; i < 4; i++)
    {
        QHFace *f = &faces[face_count];
        f->v[0] = tet_faces[i][0];
        f->v[1] = tet_faces[i][1];
        f->v[2] = tet_faces[i][2];
        f->normal = qh_compute_normal(out->vertices[f->v[0]],
                                       out->vertices[f->v[1]],
                                       out->vertices[f->v[2]]);
        f->dist = vec3_dot(f->normal, out->vertices[f->v[0]]);
        f->active = true;

        Vec3 face_center = vec3_scale(vec3_add(vec3_add(out->vertices[f->v[0]],
                                                         out->vertices[f->v[1]]),
                                                out->vertices[f->v[2]]),
                                       1.0f / 3.0f);
        Vec3 to_center = vec3_sub(center, face_center);
        if (vec3_dot(f->normal, to_center) > 0.0f)
        {
            f->normal = vec3_neg(f->normal);
            f->dist = -f->dist;
            int32_t tmp = f->v[1];
            f->v[1] = f->v[2];
            f->v[2] = tmp;
        }

        face_count++;
    }

    bool *used = (bool *)calloc(count, sizeof(bool));
    for (int32_t i = 0; i < 4; i++)
    {
        used[initial_idx[i]] = true;
    }

    for (int32_t iter = 0; iter < count && out->vertex_count < HULL_MAX_VERTICES; iter++)
    {
        int32_t best_point = -1;
        float best_dist = QH_EPSILON;

        for (int32_t fi = 0; fi < face_count; fi++)
        {
            if (!faces[fi].active)
                continue;

            QHFace *f = &faces[fi];
            for (int32_t pi = 0; pi < count; pi++)
            {
                if (used[pi])
                    continue;

                float d = qh_signed_distance(points[pi], out->vertices[f->v[0]], f->normal);
                if (d > best_dist)
                {
                    best_dist = d;
                    best_point = pi;
                }
            }
        }

        if (best_point < 0)
            break;

        used[best_point] = true;

        int32_t new_vert = out->vertex_count++;
        out->vertices[new_vert] = points[best_point];

        bool visible[QH_MAX_FACES] = {false};
        for (int32_t fi = 0; fi < face_count; fi++)
        {
            if (!faces[fi].active)
                continue;
            QHFace *f = &faces[fi];
            float d = qh_signed_distance(points[best_point], out->vertices[f->v[0]], f->normal);
            if (d > QH_EPSILON)
            {
                visible[fi] = true;
            }
        }

        typedef struct
        {
            int32_t v0, v1;
            int32_t face;
        } HorizonEdge;
        HorizonEdge horizon[256];
        int32_t horizon_count = 0;

        for (int32_t fi = 0; fi < face_count && horizon_count < 256; fi++)
        {
            if (!faces[fi].active || !visible[fi])
                continue;

            QHFace *f = &faces[fi];
            int32_t edges[3][2] = {{f->v[0], f->v[1]}, {f->v[1], f->v[2]}, {f->v[2], f->v[0]}};

            for (int32_t e = 0; e < 3; e++)
            {
                int32_t ev0 = edges[e][0];
                int32_t ev1 = edges[e][1];

                bool edge_shared_with_invisible = false;
                for (int32_t fi2 = 0; fi2 < face_count; fi2++)
                {
                    if (fi2 == fi || !faces[fi2].active || visible[fi2])
                        continue;

                    QHFace *f2 = &faces[fi2];
                    for (int32_t e2 = 0; e2 < 3; e2++)
                    {
                        int32_t e2v0 = f2->v[e2];
                        int32_t e2v1 = f2->v[(e2 + 1) % 3];
                        if ((e2v0 == ev0 && e2v1 == ev1) || (e2v0 == ev1 && e2v1 == ev0))
                        {
                            edge_shared_with_invisible = true;
                            break;
                        }
                    }
                    if (edge_shared_with_invisible)
                        break;
                }

                if (edge_shared_with_invisible && horizon_count < 256)
                {
                    horizon[horizon_count].v0 = ev0;
                    horizon[horizon_count].v1 = ev1;
                    horizon[horizon_count].face = fi;
                    horizon_count++;
                }
            }
        }

        for (int32_t fi = 0; fi < face_count; fi++)
        {
            if (visible[fi])
                faces[fi].active = false;
        }

        for (int32_t hi = 0; hi < horizon_count && face_count < QH_MAX_FACES; hi++)
        {
            int32_t new_fi = face_count++;
            QHFace *nf = &faces[new_fi];
            nf->v[0] = horizon[hi].v1;
            nf->v[1] = horizon[hi].v0;
            nf->v[2] = new_vert;
            nf->normal = qh_compute_normal(out->vertices[nf->v[0]],
                                            out->vertices[nf->v[1]],
                                            out->vertices[nf->v[2]]);
            nf->dist = vec3_dot(nf->normal, out->vertices[nf->v[0]]);
            nf->active = true;

            Vec3 face_center = vec3_scale(vec3_add(vec3_add(out->vertices[nf->v[0]],
                                                             out->vertices[nf->v[1]]),
                                                    out->vertices[nf->v[2]]),
                                           1.0f / 3.0f);
            Vec3 to_center = vec3_sub(center, face_center);
            if (vec3_dot(nf->normal, to_center) > 0.0f)
            {
                nf->normal = vec3_neg(nf->normal);
                nf->dist = -nf->dist;
                int32_t tmp = nf->v[1];
                nf->v[1] = nf->v[2];
                nf->v[2] = tmp;
            }
        }
    }

    free(used);

    for (int32_t i = 0; i < out->vertex_count; i++)
    {
        out->adj_count[i] = 0;
    }

    for (int32_t fi = 0; fi < face_count; fi++)
    {
        if (!faces[fi].active)
            continue;
        QHFace *f = &faces[fi];
        for (int32_t e = 0; e < 3; e++)
        {
            int32_t v0 = f->v[e];
            int32_t v1 = f->v[(e + 1) % 3];

            bool found = false;
            for (int32_t a = 0; a < out->adj_count[v0]; a++)
            {
                if (out->adjacency[v0][a] == v1)
                {
                    found = true;
                    break;
                }
            }
            if (!found && out->adj_count[v0] < HULL_MAX_ADJACENCY)
            {
                out->adjacency[v0][out->adj_count[v0]++] = v1;
            }

            found = false;
            for (int32_t a = 0; a < out->adj_count[v1]; a++)
            {
                if (out->adjacency[v1][a] == v0)
                {
                    found = true;
                    break;
                }
            }
            if (!found && out->adj_count[v1] < HULL_MAX_ADJACENCY)
            {
                out->adjacency[v1][out->adj_count[v1]++] = v0;
            }
        }
    }
}

int32_t convex_hull_support(const ConvexHull *hull, Vec3 dir, int32_t hint)
{
    if (hull->vertex_count == 0)
        return -1;

    if (hint < 0 || hint >= hull->vertex_count)
        hint = 0;

    int32_t best = hint;
    float best_dot = vec3_dot(hull->vertices[best], dir);

    for (int32_t iter = 0; iter < hull->vertex_count; iter++)
    {
        bool improved = false;
        for (int32_t a = 0; a < hull->adj_count[best]; a++)
        {
            int32_t neighbor = hull->adjacency[best][a];
            float d = vec3_dot(hull->vertices[neighbor], dir);
            if (d > best_dot)
            {
                best_dot = d;
                best = neighbor;
                improved = true;
            }
        }
        if (!improved)
            break;
    }

    return best;
}

Vec3 convex_hull_support_point(const ConvexHull *hull, Vec3 dir, Vec3 position, Quat orientation)
{
    Quat inv_orient = quat_conjugate(orientation);
    Vec3 local_dir = quat_rotate_vec3(inv_orient, dir);

    int32_t idx = convex_hull_support(hull, local_dir, 0);
    if (idx < 0)
        return position;

    Vec3 local_point = hull->vertices[idx];
    Vec3 world_point = quat_rotate_vec3(orientation, local_point);
    return vec3_add(position, world_point);
}
