#ifndef PATCH_PHYSICS_BROADPHASE_H
#define PATCH_PHYSICS_BROADPHASE_H

#include "engine/core/types.h"
#include "engine/core/math.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define SAP_MAX_BODIES 512
#define SAP_MAX_ENDPOINTS (SAP_MAX_BODIES * 2)
#define SAP_MAX_PAIRS 1024

    typedef struct
    {
        float value;
        int16_t body_index;
        bool is_max;
    } SAPEndpoint;

    typedef struct
    {
        int16_t body_a; /* body_a < body_b */
        int16_t body_b;
    } SAPPair;

    /* Sweep and prune along x. The endpoint list stays sorted between queries, so with
     * frame-to-frame coherence the insertion sort that refreshes it is close to linear. */
    typedef struct
    {
        SAPEndpoint endpoints[SAP_MAX_ENDPOINTS];
        int32_t endpoint_count;

        float aabb_min[SAP_MAX_BODIES][3];
        float aabb_max[SAP_MAX_BODIES][3];
        bool body_active[SAP_MAX_BODIES]; /* has an AABB this frame */
        bool body_listed[SAP_MAX_BODIES]; /* has endpoints in the list */
    } SAPBroadphase;

    void sap_init(SAPBroadphase *sap);
    void sap_update_body(SAPBroadphase *sap, int32_t body_index, Vec3 aabb_min, Vec3 aabb_max);
    void sap_remove_body(SAPBroadphase *sap, int32_t body_index);

    /* All pairs of active bodies whose AABBs overlap (touching counts), up to max_pairs */
    int32_t sap_query_pairs(SAPBroadphase *sap, SAPPair *out_pairs, int32_t max_pairs);

#ifdef __cplusplus
}
#endif

#endif
