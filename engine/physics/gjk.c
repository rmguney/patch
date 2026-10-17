#include "gjk.h"

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

/* (a x b) x c */
static Vec3 triple_product(Vec3 a, Vec3 b, Vec3 c)
{
    return vec3_sub(vec3_scale(b, vec3_dot(a, c)), vec3_scale(a, vec3_dot(b, c)));
}

/* The newest vertex is always last; each case keeps the feature closest to the origin
 * and points dir at the origin from it. Returns true once the origin is enclosed. */
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
            /* Origin on the line: any perpendicular will do */
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

    if (vec3_dot(vec3_cross(ab, abc), ao) > 0.0f)
    {
        s->vertices[0] = s->vertices[1];
        s->vertices[1] = s->vertices[2];
        s->count = 2;
        *dir = triple_product(ab, ao, ab);
        return false;
    }

    if (vec3_dot(vec3_cross(abc, ac), ao) > 0.0f)
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
        /* Rewind so the triangle faces the origin */
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

    /* Outward normals of the three faces through a (face bcd was the previous triangle) */
    if (vec3_dot(vec3_cross(ab, ac), ao) > 0.0f)
    {
        /* Face ABC as {c, b, a} */
        s->vertices[0] = s->vertices[1];
        s->vertices[1] = s->vertices[2];
        s->vertices[2] = s->vertices[3];
        s->count = 3;
        return gjk_do_simplex_triangle(s, dir);
    }

    if (vec3_dot(vec3_cross(ac, ad), ao) > 0.0f)
    {
        /* Face ACD as {d, c, a}: vertices[0] = d and vertices[1] = c stay */
        s->vertices[2] = s->vertices[3];
        s->count = 3;
        return gjk_do_simplex_triangle(s, dir);
    }

    if (vec3_dot(vec3_cross(ad, ab), ao) > 0.0f)
    {
        /* Face ADB as {d, b, a}: vertices[0] = d stays */
        s->vertices[1] = s->vertices[2];
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

    /* Small skew so symmetric configurations do not start on a degenerate axis */
    Vec3 dir = vec3_add(vec3_sub(pos_b, pos_a), vec3_create(0.00091f, 0.00037f, 0.00071f));

    GJKSimplex simplex;
    simplex.vertices[0] = gjk_support(hull_a, pos_a, rot_a, hull_b, pos_b, rot_b, dir);
    simplex.count = 1;
    dir = vec3_neg(simplex.vertices[0].minkowski);

    for (int32_t iter = 0; iter < GJK_MAX_ITERATIONS; iter++)
    {
        float dir_len = vec3_length(dir);
        if (dir_len < GJK_EPSILON)
        {
            /* Origin on the simplex: touching */
            *out_simplex = simplex;
            return true;
        }
        dir = vec3_scale(dir, 1.0f / dir_len);

        GJKVertex support = gjk_support(hull_a, pos_a, rot_a, hull_b, pos_b, rot_b, dir);
        if (vec3_dot(support.minkowski, dir) < GJK_EPSILON)
            return false; /* support does not pass the origin: separated */

        simplex.vertices[simplex.count++] = support;

        if (gjk_do_simplex(&simplex, &dir))
        {
            *out_simplex = simplex;
            return true;
        }
    }

    return false;
}

/* Triangle wound counter-clockwise seen from outside, with its outward unit normal and
 * the distance of its plane from the origin */
typedef struct
{
    int32_t v[3];
    Vec3 normal;
    float dist;
} EPAFace;

typedef struct
{
    int32_t v0, v1;
} EPAEdge;

static EPAFace epa_make_face(const GJKVertex *vertices, int32_t a, int32_t b, int32_t c)
{
    EPAFace f = {{a, b, c}, vec3_create(0.0f, 1.0f, 0.0f), 0.0f};
    Vec3 pa = vertices[a].minkowski;
    Vec3 n = vec3_cross(vec3_sub(vertices[b].minkowski, pa), vec3_sub(vertices[c].minkowski, pa));
    float len = vec3_length(n);
    if (len > GJK_EPSILON)
        f.normal = vec3_scale(n, 1.0f / len);
    f.dist = vec3_dot(f.normal, pa);
    return f;
}

static int32_t epa_closest_face(const EPAFace *faces, int32_t face_count)
{
    int32_t closest = 0;
    for (int32_t i = 1; i < face_count; i++)
    {
        if (faces[i].dist < faces[closest].dist)
            closest = i;
    }
    return closest;
}

static bool epa_edge_shared(const EPAFace *f, int32_t a, int32_t b)
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

/* Barycentric weights of p's projection onto triangle abc (centroid if degenerate) */
static Vec3 epa_barycentric(Vec3 p, Vec3 a, Vec3 b, Vec3 c)
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
        return vec3_create(1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f);

    float v = clampf((d11 * d20 - d01 * d21) / denom, 0.0f, 1.0f);
    float w = clampf((d00 * d21 - d01 * d20) / denom, 0.0f, 1.0f);
    float u = clampf(1.0f - v - w, 0.0f, 1.0f);
    float sum = u + v + w;
    return vec3_scale(vec3_create(u, v, w), 1.0f / sum);
}

static void epa_fill_result(const GJKVertex *vertices, const EPAFace *f, EPAResult *out)
{
    const GJKVertex *a = &vertices[f->v[0]];
    const GJKVertex *b = &vertices[f->v[1]];
    const GJKVertex *c = &vertices[f->v[2]];

    /* The origin's projection onto the face, mapped back onto both shapes */
    Vec3 bary = epa_barycentric(vec3_scale(f->normal, f->dist), a->minkowski, b->minkowski, c->minkowski);

    out->normal = f->normal;
    out->depth = f->dist;
    out->contact_a = vec3_add(vec3_add(vec3_scale(a->point_a, bary.x), vec3_scale(b->point_a, bary.y)),
                              vec3_scale(c->point_a, bary.z));
    out->contact_b = vec3_add(vec3_add(vec3_scale(a->point_b, bary.x), vec3_scale(b->point_b, bary.y)),
                              vec3_scale(c->point_b, bary.z));
}

bool epa_penetration(
    const ConvexHull *hull_a, Vec3 pos_a, Quat rot_a,
    const ConvexHull *hull_b, Vec3 pos_b, Quat rot_b,
    const GJKSimplex *simplex,
    EPAResult *out)
{
    if (simplex->count < 4)
        return false;

    GJKVertex vertices[EPA_MAX_VERTICES];
    int32_t vertex_count = 4;
    for (int32_t i = 0; i < 4; i++)
        vertices[i] = simplex->vertices[i];

    /* Initial tetrahedron, wound so every normal points away from its centroid */
    EPAFace faces[EPA_MAX_FACES];
    int32_t face_count = 0;
    Vec3 center = vec3_scale(vec3_add(vec3_add(vertices[0].minkowski, vertices[1].minkowski),
                                      vec3_add(vertices[2].minkowski, vertices[3].minkowski)),
                             0.25f);
    static const int32_t tet_faces[4][3] = {{0, 1, 2}, {0, 3, 1}, {1, 3, 2}, {2, 3, 0}};
    for (int32_t i = 0; i < 4; i++)
    {
        EPAFace f = epa_make_face(vertices, tet_faces[i][0], tet_faces[i][1], tet_faces[i][2]);
        if (vec3_dot(f.normal, vec3_sub(center, vertices[f.v[0]].minkowski)) > 0.0f)
            f = epa_make_face(vertices, f.v[0], f.v[2], f.v[1]);
        faces[face_count++] = f;
    }

    for (int32_t iter = 0; iter < EPA_MAX_ITERATIONS; iter++)
    {
        const EPAFace *closest = &faces[epa_closest_face(faces, face_count)];
        GJKVertex support = gjk_support(hull_a, pos_a, rot_a, hull_b, pos_b, rot_b, closest->normal);
        if (vec3_dot(support.minkowski, closest->normal) - closest->dist < EPA_EPSILON)
            break; /* the polytope reached the Minkowski boundary along this face */

        bool visible[EPA_MAX_FACES];
        int32_t visible_count = 0;
        for (int32_t i = 0; i < face_count; i++)
        {
            visible[i] = vec3_dot(support.minkowski, faces[i].normal) - faces[i].dist > GJK_EPSILON;
            visible_count += visible[i] ? 1 : 0;
        }

        /* Horizon: edges of visible faces that a hidden face shares, in the visible
         * face's winding so the new faces come out wound the same way */
        EPAEdge horizon[EPA_MAX_FACES];
        int32_t horizon_count = 0;
        for (int32_t fi = 0; fi < face_count; fi++)
        {
            if (!visible[fi])
                continue;
            for (int32_t e = 0; e < 3 && horizon_count < EPA_MAX_FACES; e++)
            {
                int32_t a = faces[fi].v[e];
                int32_t b = faces[fi].v[(e + 1) % 3];
                for (int32_t fj = 0; fj < face_count; fj++)
                {
                    if (!visible[fj] && epa_edge_shared(&faces[fj], a, b))
                    {
                        horizon[horizon_count++] = (EPAEdge){a, b};
                        break;
                    }
                }
            }
        }

        /* Out of room: stop with the current polytope rather than leave a hole in it */
        if (face_count - visible_count + horizon_count > EPA_MAX_FACES)
            break;

        int32_t new_vert = vertex_count++;
        vertices[new_vert] = support;

        int32_t write_idx = 0;
        for (int32_t i = 0; i < face_count; i++)
        {
            if (!visible[i])
                faces[write_idx++] = faces[i];
        }
        face_count = write_idx;

        for (int32_t hi = 0; hi < horizon_count; hi++)
            faces[face_count++] = epa_make_face(vertices, horizon[hi].v0, horizon[hi].v1, new_vert);
    }

    epa_fill_result(vertices, &faces[epa_closest_face(faces, face_count)], out);
    return true;
}
