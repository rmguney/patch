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
#define EPA_MAX_FACES 128
#define GJK_EPSILON 1e-6f
#define EPA_EPSILON 1e-4f

    typedef struct
    {
        Vec3 point_a;
        Vec3 point_b;
        Vec3 minkowski;
    } GJKVertex;

    typedef struct
    {
        GJKVertex vertices[4];
        int32_t count;
    } GJKSimplex;

    typedef struct
    {
        Vec3 normal;
        float depth;
        Vec3 contact_a;
        Vec3 contact_b;
    } EPAResult;

    bool gjk_intersect(
        const ConvexHull *hull_a, Vec3 pos_a, Quat rot_a,
        const ConvexHull *hull_b, Vec3 pos_b, Quat rot_b,
        GJKSimplex *out_simplex);

    bool epa_penetration(
        const ConvexHull *hull_a, Vec3 pos_a, Quat rot_a,
        const ConvexHull *hull_b, Vec3 pos_b, Quat rot_b,
        const GJKSimplex *simplex,
        EPAResult *out);

#ifdef __cplusplus
}
#endif

#endif
