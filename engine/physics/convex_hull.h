#ifndef PATCH_PHYSICS_CONVEX_HULL_H
#define PATCH_PHYSICS_CONVEX_HULL_H

#include "engine/core/types.h"
#include "engine/core/math.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define HULL_MAX_VERTICES 128

    typedef struct
    {
        Vec3 vertices[HULL_MAX_VERTICES];
        int32_t vertex_count;
        float margin; /* Rounds the hull off by this radius in GJK/EPA support queries */
    } ConvexHull;

    /* Quickhull over the points. Inputs with fewer than 4 points, or flat ones, keep the
     * points themselves (up to HULL_MAX_VERTICES), which still give the right support. */
    void convex_hull_build(const Vec3 *points, int32_t count, ConvexHull *out);

    /* Index of the vertex furthest along dir (-1 for an empty hull) */
    int32_t convex_hull_support(const ConvexHull *hull, Vec3 dir);

    /* World-space support point of the hull placed at position/orientation, margin included */
    Vec3 convex_hull_support_point(const ConvexHull *hull, Vec3 dir, Vec3 position, Quat orientation);

#ifdef __cplusplus
}
#endif

#endif
