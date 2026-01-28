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
#define SAP_MAX_PAIRS 256

typedef struct {
    float value;
    int16_t body_index;
    uint8_t is_max;
    uint8_t _pad;
} SAPEndpoint;

typedef struct {
    int16_t body_a;
    int16_t body_b;
} SAPPair;

typedef struct {
    SAPEndpoint endpoints_x[SAP_MAX_ENDPOINTS];
    SAPEndpoint endpoints_y[SAP_MAX_ENDPOINTS];
    SAPEndpoint endpoints_z[SAP_MAX_ENDPOINTS];
    int32_t endpoint_count;

    float aabb_min[SAP_MAX_BODIES][3];
    float aabb_max[SAP_MAX_BODIES][3];
    bool body_active[SAP_MAX_BODIES];
} SAPBroadphase;

void sap_init(SAPBroadphase *sap);
void sap_update_body(SAPBroadphase *sap, int32_t body_index,
                     Vec3 aabb_min, Vec3 aabb_max, bool active);
void sap_remove_body(SAPBroadphase *sap, int32_t body_index);
int32_t sap_query_pairs(SAPBroadphase *sap, SAPPair *out_pairs, int32_t max_pairs);

#ifdef __cplusplus
}
#endif

#endif
