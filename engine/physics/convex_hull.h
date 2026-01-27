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
#define HULL_MAX_FACES 256
#define HULL_MAX_ADJACENCY 12

    typedef struct
    {
        Vec3 vertices[HULL_MAX_VERTICES];
        int32_t vertex_count;
        int32_t adjacency[HULL_MAX_VERTICES][HULL_MAX_ADJACENCY];
        int32_t adj_count[HULL_MAX_VERTICES];
        float margin;
    } ConvexHull;

    void convex_hull_build(const Vec3 *points, int32_t count, ConvexHull *out);

    int32_t convex_hull_support(const ConvexHull *hull, Vec3 dir, int32_t hint);

    Vec3 convex_hull_support_point(const ConvexHull *hull, Vec3 dir, Vec3 position, Quat orientation);

#ifdef __cplusplus
}
#endif

#endif
