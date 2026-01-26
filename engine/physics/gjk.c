#include "gjk.h"
#include <string.h>

static GJKVertex gjk_support(
    const ConvexHull *hull_a, Vec3 pos_a, Quat rot_a,
    const ConvexHull *hull_b, Vec3 pos_b, Quat rot_b,
    Vec3 dir)
{
    GJKVertex v;
    v.point_a = convex_hull_support_point(hull_a, dir, pos_a, rot_a);
    v.point_b = convex_hull_support_point(hull_b, vec3_neg(dir), pos_b, rot_b);
    v.minkowski = vec3_sub(v.point_a, v.point_b);
    return v;
}

static Vec3 triple_product(Vec3 a, Vec3 b, Vec3 c)
{
    return vec3_sub(vec3_scale(b, vec3_dot(a, c)), vec3_scale(a, vec3_dot(b, c)));
}

static bool gjk_do_simplex_line(GJKSimplex *s, Vec3 *dir)
{
    Vec3 a = s->vertices[1].minkowski;
    Vec3 b = s->vertices[0].minkowski;

    Vec3 ab = vec3_sub(b, a);
    Vec3 ao = vec3_neg(a);

    if (vec3_dot(ab, ao) > 0.0f)
    {
        *dir = triple_product(ab, ao, ab);
        if (vec3_length_sq(*dir) < GJK_EPSILON * GJK_EPSILON)
        {
            *dir = vec3_cross(ab, vec3_create(1.0f, 0.0f, 0.0f));
            if (vec3_length_sq(*dir) < GJK_EPSILON * GJK_EPSILON)
                *dir = vec3_cross(ab, vec3_create(0.0f, 1.0f, 0.0f));
        }
    }
    else
    {
        s->vertices[0] = s->vertices[1];
        s->count = 1;
        *dir = ao;
    }
    return false;
}

static bool gjk_do_simplex_triangle(GJKSimplex *s, Vec3 *dir)
{
    Vec3 a = s->vertices[2].minkowski;
    Vec3 b = s->vertices[1].minkowski;
    Vec3 c = s->vertices[0].minkowski;

    Vec3 ab = vec3_sub(b, a);
    Vec3 ac = vec3_sub(c, a);
    Vec3 ao = vec3_neg(a);

    Vec3 abc = vec3_cross(ab, ac);

    Vec3 ab_perp = vec3_cross(ab, abc);
    if (vec3_dot(ab_perp, ao) > 0.0f)
    {
        s->vertices[0] = s->vertices[1];
        s->vertices[1] = s->vertices[2];
        s->count = 2;
        *dir = triple_product(ab, ao, ab);
        return false;
    }

    Vec3 ac_perp = vec3_cross(abc, ac);
    if (vec3_dot(ac_perp, ao) > 0.0f)
    {
        s->vertices[1] = s->vertices[2];
        s->count = 2;
        *dir = triple_product(ac, ao, ac);
        return false;
    }

    if (vec3_dot(abc, ao) > 0.0f)
    {
        *dir = abc;
    }
    else
    {
        GJKVertex tmp = s->vertices[0];
        s->vertices[0] = s->vertices[1];
        s->vertices[1] = tmp;
        *dir = vec3_neg(abc);
    }
    return false;
}

static bool gjk_do_simplex_tetrahedron(GJKSimplex *s, Vec3 *dir)
{
    Vec3 a = s->vertices[3].minkowski;
    Vec3 b = s->vertices[2].minkowski;
    Vec3 c = s->vertices[1].minkowski;
    Vec3 d = s->vertices[0].minkowski;

    Vec3 ab = vec3_sub(b, a);
    Vec3 ac = vec3_sub(c, a);
    Vec3 ad = vec3_sub(d, a);
    Vec3 ao = vec3_neg(a);

    Vec3 abc = vec3_cross(ab, ac);
    Vec3 acd = vec3_cross(ac, ad);
    Vec3 adb = vec3_cross(ad, ab);

    if (vec3_dot(abc, ao) > 0.0f)
    {
        s->vertices[0] = s->vertices[1];
        s->vertices[1] = s->vertices[2];
        s->vertices[2] = s->vertices[3];
        s->count = 3;
        return gjk_do_simplex_triangle(s, dir);
    }

    if (vec3_dot(acd, ao) > 0.0f)
    {
        s->vertices[1] = s->vertices[3];
        s->count = 3;
        return gjk_do_simplex_triangle(s, dir);
    }

    if (vec3_dot(adb, ao) > 0.0f)
    {
        s->vertices[0] = s->vertices[2];
        s->vertices[1] = s->vertices[0];
        s->vertices[1] = s->vertices[3];
        s->vertices[2] = s->vertices[3];
        s->count = 3;
        return gjk_do_simplex_triangle(s, dir);
    }

    return true;
}

static bool gjk_do_simplex(GJKSimplex *s, Vec3 *dir)
{
    switch (s->count)
    {
    case 2:
        return gjk_do_simplex_line(s, dir);
    case 3:
        return gjk_do_simplex_triangle(s, dir);
    case 4:
        return gjk_do_simplex_tetrahedron(s, dir);
    default:
        return false;
    }
}

bool gjk_intersect(
    const ConvexHull *hull_a, Vec3 pos_a, Quat rot_a,
    const ConvexHull *hull_b, Vec3 pos_b, Quat rot_b,
    GJKSimplex *out_simplex)
{
    if (hull_a->vertex_count == 0 || hull_b->vertex_count == 0)
        return false;

    Vec3 dir = vec3_sub(pos_b, pos_a);
    if (vec3_length_sq(dir) < GJK_EPSILON * GJK_EPSILON)
        dir = vec3_create(1.0f, 0.0f, 0.0f);

    GJKSimplex simplex;
    simplex.count = 0;

    GJKVertex support = gjk_support(hull_a, pos_a, rot_a, hull_b, pos_b, rot_b, dir);
    simplex.vertices[simplex.count++] = support;

    dir = vec3_neg(support.minkowski);

    for (int32_t iter = 0; iter < GJK_MAX_ITERATIONS; iter++)
    {
        float dir_len = vec3_length(dir);
        if (dir_len < GJK_EPSILON)
        {
            if (out_simplex)
                *out_simplex = simplex;
            return true;
        }
        dir = vec3_scale(dir, 1.0f / dir_len);

        support = gjk_support(hull_a, pos_a, rot_a, hull_b, pos_b, rot_b, dir);

        if (vec3_dot(support.minkowski, dir) < GJK_EPSILON)
        {
            return false;
        }

        simplex.vertices[simplex.count++] = support;

        if (gjk_do_simplex(&simplex, &dir))
        {
            if (out_simplex)
                *out_simplex = simplex;
            return true;
        }
    }

    return false;
}

typedef struct
{
    int32_t v[3];
    Vec3 normal;
    float dist;
} EPAFace;

static Vec3 epa_compute_normal(Vec3 a, Vec3 b, Vec3 c)
{
    Vec3 ab = vec3_sub(b, a);
    Vec3 ac = vec3_sub(c, a);
    Vec3 n = vec3_cross(ab, ac);
    float len = vec3_length(n);
    if (len > GJK_EPSILON)
        return vec3_scale(n, 1.0f / len);
    return vec3_create(0.0f, 1.0f, 0.0f);
}

static void epa_barycentric(Vec3 p, Vec3 a, Vec3 b, Vec3 c, float *u, float *v, float *w)
{
    Vec3 v0 = vec3_sub(b, a);
    Vec3 v1 = vec3_sub(c, a);
    Vec3 v2 = vec3_sub(p, a);

    float d00 = vec3_dot(v0, v0);
    float d01 = vec3_dot(v0, v1);
    float d11 = vec3_dot(v1, v1);
    float d20 = vec3_dot(v2, v0);
    float d21 = vec3_dot(v2, v1);

    float denom = d00 * d11 - d01 * d01;
    if (fabsf(denom) < GJK_EPSILON)
    {
        *u = 1.0f / 3.0f;
        *v = 1.0f / 3.0f;
        *w = 1.0f / 3.0f;
        return;
    }

    *v = (d11 * d20 - d01 * d21) / denom;
    *w = (d00 * d21 - d01 * d20) / denom;
    *u = 1.0f - *v - *w;
}

bool epa_penetration(
    const ConvexHull *hull_a, Vec3 pos_a, Quat rot_a,
    const ConvexHull *hull_b, Vec3 pos_b, Quat rot_b,
    const GJKSimplex *simplex,
    EPAResult *out)
{
    if (!simplex || simplex->count < 4)
        return false;

    GJKVertex vertices[128];
    int32_t vertex_count = 4;
    for (int32_t i = 0; i < 4; i++)
    {
        vertices[i] = simplex->vertices[i];
    }

    EPAFace faces[EPA_MAX_FACES];
    int32_t face_count = 0;

    int32_t tet_faces[4][3] = {
        {0, 1, 2},
        {0, 3, 1},
        {1, 3, 2},
        {2, 3, 0}};

    Vec3 center = vec3_scale(
        vec3_add(vec3_add(vertices[0].minkowski, vertices[1].minkowski),
                 vec3_add(vertices[2].minkowski, vertices[3].minkowski)),
        0.25f);

    for (int32_t i = 0; i < 4; i++)
    {
        EPAFace *f = &faces[face_count++];
        f->v[0] = tet_faces[i][0];
        f->v[1] = tet_faces[i][1];
        f->v[2] = tet_faces[i][2];
        f->normal = epa_compute_normal(vertices[f->v[0]].minkowski,
                                        vertices[f->v[1]].minkowski,
                                        vertices[f->v[2]].minkowski);
        f->dist = vec3_dot(f->normal, vertices[f->v[0]].minkowski);

        Vec3 face_center = vec3_scale(
            vec3_add(vec3_add(vertices[f->v[0]].minkowski,
                              vertices[f->v[1]].minkowski),
                     vertices[f->v[2]].minkowski),
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
    }

    for (int32_t iter = 0; iter < EPA_MAX_ITERATIONS; iter++)
    {
        int32_t closest_face = 0;
        float closest_dist = faces[0].dist;
        for (int32_t i = 1; i < face_count; i++)
        {
            if (faces[i].dist < closest_dist)
            {
                closest_dist = faces[i].dist;
                closest_face = i;
            }
        }

        EPAFace *cf = &faces[closest_face];
        GJKVertex support = gjk_support(hull_a, pos_a, rot_a, hull_b, pos_b, rot_b, cf->normal);

        float support_dist = vec3_dot(support.minkowski, cf->normal);
        if (support_dist - closest_dist < EPA_EPSILON)
        {
            out->normal = cf->normal;
            out->depth = closest_dist;

            Vec3 proj = vec3_scale(cf->normal, closest_dist);
            float u, v, w;
            epa_barycentric(proj,
                            vertices[cf->v[0]].minkowski,
                            vertices[cf->v[1]].minkowski,
                            vertices[cf->v[2]].minkowski,
                            &u, &v, &w);

            u = clampf(u, 0.0f, 1.0f);
            v = clampf(v, 0.0f, 1.0f);
            w = clampf(w, 0.0f, 1.0f);
            float sum = u + v + w;
            if (sum > GJK_EPSILON)
            {
                u /= sum;
                v /= sum;
                w /= sum;
            }
            else
            {
                u = 1.0f / 3.0f;
                v = 1.0f / 3.0f;
                w = 1.0f / 3.0f;
            }

            out->contact_a = vec3_add(
                vec3_add(vec3_scale(vertices[cf->v[0]].point_a, u),
                         vec3_scale(vertices[cf->v[1]].point_a, v)),
                vec3_scale(vertices[cf->v[2]].point_a, w));
            out->contact_b = vec3_add(
                vec3_add(vec3_scale(vertices[cf->v[0]].point_b, u),
                         vec3_scale(vertices[cf->v[1]].point_b, v)),
                vec3_scale(vertices[cf->v[2]].point_b, w));

            return true;
        }

        if (vertex_count >= 128)
            break;

        int32_t new_vert = vertex_count++;
        vertices[new_vert] = support;

        typedef struct
        {
            int32_t v0, v1;
        } Edge;
        Edge horizon[256];
        int32_t horizon_count = 0;

        bool visible[EPA_MAX_FACES] = {false};
        for (int32_t i = 0; i < face_count; i++)
        {
            EPAFace *f = &faces[i];
            float d = vec3_dot(support.minkowski, f->normal) - f->dist;
            if (d > GJK_EPSILON)
                visible[i] = true;
        }

        for (int32_t fi = 0; fi < face_count && horizon_count < 256; fi++)
        {
            if (!visible[fi])
                continue;

            EPAFace *f = &faces[fi];
            int32_t edges[3][2] = {{f->v[0], f->v[1]}, {f->v[1], f->v[2]}, {f->v[2], f->v[0]}};

            for (int32_t e = 0; e < 3; e++)
            {
                int32_t ev0 = edges[e][0];
                int32_t ev1 = edges[e][1];

                bool shared_with_invisible = false;
                for (int32_t fi2 = 0; fi2 < face_count; fi2++)
                {
                    if (fi2 == fi || visible[fi2])
                        continue;

                    EPAFace *f2 = &faces[fi2];
                    for (int32_t e2 = 0; e2 < 3; e2++)
                    {
                        int32_t e2v0 = f2->v[e2];
                        int32_t e2v1 = f2->v[(e2 + 1) % 3];
                        if ((e2v0 == ev0 && e2v1 == ev1) || (e2v0 == ev1 && e2v1 == ev0))
                        {
                            shared_with_invisible = true;
                            break;
                        }
                    }
                    if (shared_with_invisible)
                        break;
                }

                if (shared_with_invisible && horizon_count < 256)
                {
                    horizon[horizon_count].v0 = ev0;
                    horizon[horizon_count].v1 = ev1;
                    horizon_count++;
                }
            }
        }

        int32_t write_idx = 0;
        for (int32_t i = 0; i < face_count; i++)
        {
            if (!visible[i])
            {
                if (write_idx != i)
                    faces[write_idx] = faces[i];
                write_idx++;
            }
        }
        face_count = write_idx;

        for (int32_t hi = 0; hi < horizon_count && face_count < EPA_MAX_FACES; hi++)
        {
            EPAFace *nf = &faces[face_count++];
            nf->v[0] = horizon[hi].v1;
            nf->v[1] = horizon[hi].v0;
            nf->v[2] = new_vert;
            nf->normal = epa_compute_normal(vertices[nf->v[0]].minkowski,
                                             vertices[nf->v[1]].minkowski,
                                             vertices[nf->v[2]].minkowski);
            nf->dist = vec3_dot(nf->normal, vertices[nf->v[0]].minkowski);

            if (nf->dist < 0.0f)
            {
                nf->normal = vec3_neg(nf->normal);
                nf->dist = -nf->dist;
                int32_t tmp = nf->v[1];
                nf->v[1] = nf->v[2];
                nf->v[2] = tmp;
            }
        }
    }

    if (face_count > 0)
    {
        int32_t closest_face = 0;
        float closest_dist = faces[0].dist;
        for (int32_t i = 1; i < face_count; i++)
        {
            if (faces[i].dist < closest_dist)
            {
                closest_dist = faces[i].dist;
                closest_face = i;
            }
        }

        EPAFace *cf = &faces[closest_face];
        out->normal = cf->normal;
        out->depth = closest_dist;
        out->contact_a = vec3_scale(
            vec3_add(vec3_add(vertices[cf->v[0]].point_a,
                              vertices[cf->v[1]].point_a),
                     vertices[cf->v[2]].point_a),
            1.0f / 3.0f);
        out->contact_b = vec3_scale(
            vec3_add(vec3_add(vertices[cf->v[0]].point_b,
                              vertices[cf->v[1]].point_b),
                     vertices[cf->v[2]].point_b),
            1.0f / 3.0f);
        return true;
    }

    return false;
}
