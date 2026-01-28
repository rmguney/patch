#include "broadphase.h"
#include <string.h>

void sap_init(SAPBroadphase *sap)
{
    if (!sap)
        return;

    memset(sap, 0, sizeof(SAPBroadphase));
    sap->endpoint_count = 0;
}

void sap_update_body(SAPBroadphase *sap, int32_t body_index,
                     Vec3 aabb_min, Vec3 aabb_max, bool active)
{
    if (!sap || body_index < 0 || body_index >= SAP_MAX_BODIES)
        return;

    sap->aabb_min[body_index][0] = aabb_min.x;
    sap->aabb_min[body_index][1] = aabb_min.y;
    sap->aabb_min[body_index][2] = aabb_min.z;
    sap->aabb_max[body_index][0] = aabb_max.x;
    sap->aabb_max[body_index][1] = aabb_max.y;
    sap->aabb_max[body_index][2] = aabb_max.z;
    sap->body_active[body_index] = active;
}

void sap_remove_body(SAPBroadphase *sap, int32_t body_index)
{
    if (!sap || body_index < 0 || body_index >= SAP_MAX_BODIES)
        return;

    sap->body_active[body_index] = false;
}

static void insertion_sort_endpoints(SAPEndpoint *endpoints, int32_t count)
{
    for (int32_t i = 1; i < count; i++)
    {
        SAPEndpoint key = endpoints[i];
        int32_t j = i - 1;
        while (j >= 0 && endpoints[j].value > key.value)
        {
            endpoints[j + 1] = endpoints[j];
            j--;
        }
        endpoints[j + 1] = key;
    }
}

static bool aabb_overlap_1d(float min_a, float max_a, float min_b, float max_b)
{
    return min_a <= max_b && min_b <= max_a;
}

int32_t sap_query_pairs(SAPBroadphase *sap, SAPPair *out_pairs, int32_t max_pairs)
{
    if (!sap || !out_pairs || max_pairs <= 0)
        return 0;

    int32_t active_count = 0;
    int16_t active_bodies[SAP_MAX_BODIES];

    for (int32_t i = 0; i < SAP_MAX_BODIES; i++)
    {
        if (sap->body_active[i])
            active_bodies[active_count++] = (int16_t)i;
    }

    if (active_count < 2)
        return 0;

    int32_t ep_count = active_count * 2;
    SAPEndpoint *eps_x = sap->endpoints_x;

    for (int32_t i = 0; i < active_count; i++)
    {
        int16_t idx = active_bodies[i];
        eps_x[i * 2].value = sap->aabb_min[idx][0];
        eps_x[i * 2].body_index = idx;
        eps_x[i * 2].is_max = 0;
        eps_x[i * 2 + 1].value = sap->aabb_max[idx][0];
        eps_x[i * 2 + 1].body_index = idx;
        eps_x[i * 2 + 1].is_max = 1;
    }

    insertion_sort_endpoints(eps_x, ep_count);

    int32_t pair_count = 0;
    bool active_set[SAP_MAX_BODIES];
    memset(active_set, 0, sizeof(active_set));

    for (int32_t i = 0; i < ep_count && pair_count < max_pairs; i++)
    {
        SAPEndpoint *ep = &eps_x[i];
        int16_t body_idx = ep->body_index;

        if (ep->is_max == 0)
        {
            for (int32_t j = 0; j < SAP_MAX_BODIES && pair_count < max_pairs; j++)
            {
                if (!active_set[j] || j == body_idx)
                    continue;

                if (!aabb_overlap_1d(sap->aabb_min[body_idx][1], sap->aabb_max[body_idx][1],
                                     sap->aabb_min[j][1], sap->aabb_max[j][1]))
                    continue;

                if (!aabb_overlap_1d(sap->aabb_min[body_idx][2], sap->aabb_max[body_idx][2],
                                     sap->aabb_min[j][2], sap->aabb_max[j][2]))
                    continue;

                int16_t a = body_idx < j ? body_idx : (int16_t)j;
                int16_t b = body_idx < j ? (int16_t)j : body_idx;

                out_pairs[pair_count].body_a = a;
                out_pairs[pair_count].body_b = b;
                pair_count++;
            }

            active_set[body_idx] = true;
        }
        else
        {
            active_set[body_idx] = false;
        }
    }

    sap->endpoint_count = ep_count;
    return pair_count;
}
