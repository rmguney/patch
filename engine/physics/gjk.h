#ifndef PATCH_PHYSICS_GJK_H
#define PATCH_PHYSICS_GJK_H

#include "engine/core/types.h"
#include "engine/core/math.h"
#include "convex_hull.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define GJK_MAX_ITERATIONS 64
#define EPA_MAX_ITERATIONS 64
#define EPA_MAX_VERTICES (EPA_MAX_ITERATIONS + 4)
#define EPA_MAX_FACES (2 * EPA_MAX_VERTICES) /* a closed triangle mesh has 2V - 4 faces */
#define GJK_EPSILON 1e-6f
#define EPA_EPSILON 1e-4f

    typedef struct
    {
        Vec3 point_a;
        Vec3 point_b;
        Vec3 minkowski; /* point_a - point_b */
    } GJKVertex;

    typedef struct
    {
        GJKVertex vertices[4];
        int32_t count;
    } GJKSimplex;

    typedef struct
    {
        Vec3 normal;     /* Unit vector from A towards B */
        float depth;     /* Overlap along normal, hull margins included */
        Vec3 contact_a;  /* Deepest point of A inside B */
        Vec3 contact_b;  /* Deepest point of B inside A */
    } EPAResult;

    /* True when the (margin-rounded) hulls overlap. On a hit, out_simplex holds the
     * final simplex, a tetrahedron around the origin unless the shapes only touch. */
    bool gjk_intersect(
        const ConvexHull *hull_a, Vec3 pos_a, Quat rot_a,
        const ConvexHull *hull_b, Vec3 pos_b, Quat rot_b,
        GJKSimplex *out_simplex);

    /* Penetration depth and normal from a GJK tetrahedron; false for a smaller simplex */
    bool epa_penetration(
        const ConvexHull *hull_a, Vec3 pos_a, Quat rot_a,
        const ConvexHull *hull_b, Vec3 pos_b, Quat rot_b,
        const GJKSimplex *simplex,
        EPAResult *out);

#ifdef __cplusplus
}
#endif

#endif
