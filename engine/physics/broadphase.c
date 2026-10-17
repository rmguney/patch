#include "broadphase.h"
#include <assert.h>
#include <string.h>

void sap_init(SAPBroadphase *sap)
{
    memset(sap, 0, sizeof(SAPBroadphase));
}

void sap_update_body(SAPBroadphase *sap, int32_t body_index, Vec3 aabb_min, Vec3 aabb_max)
{
    if (body_index < 0 || body_index >= SAP_MAX_BODIES)
        return;

    sap->aabb_min[body_index][0] = aabb_min.x;
    sap->aabb_min[body_index][1] = aabb_min.y;
    sap->aabb_min[body_index][2] = aabb_min.z;
    sap->aabb_max[body_index][0] = aabb_max.x;
    sap->aabb_max[body_index][1] = aabb_max.y;
    sap->aabb_max[body_index][2] = aabb_max.z;
    sap->body_active[body_index] = true;
}

void sap_remove_body(SAPBroadphase *sap, int32_t body_index)
{
    if (body_index < 0 || body_index >= SAP_MAX_BODIES)
        return;

    sap->body_active[body_index] = false;
}

/* Ascending by value; at equal values min endpoints come first so touching boxes pair up */
static bool endpoint_after(const SAPEndpoint *a, const SAPEndpoint *b)
{
    return a->value > b->value || (a->value == b->value && a->is_max && !b->is_max);
}

static void insertion_sort_endpoints(SAPEndpoint *endpoints, int32_t count)
{
    for (int32_t i = 1; i < count; i++)
    {
        SAPEndpoint key = endpoints[i];
        int32_t j = i - 1;
        while (j >= 0 && endpoint_after(&endpoints[j], &key))
        {
            endpoints[j + 1] = endpoints[j];
            j--;
        }
        endpoints[j + 1] = key;
    }
}

/* Refreshes the endpoint values in their previous order, drops bodies that went inactive
 * and appends new ones, so the list is nearly sorted before the sort. */
static void refresh_endpoints(SAPBroadphase *sap)
{
    int32_t write = 0;
    for (int32_t i = 0; i < sap->endpoint_count; i++)
    {
        SAPEndpoint ep = sap->endpoints[i];
        if (!sap->body_active[ep.body_index])
        {
            sap->body_listed[ep.body_index] = false;
            continue;
        }
        ep.value = ep.is_max ? sap->aabb_max[ep.body_index][0] : sap->aabb_min[ep.body_index][0];
        sap->endpoints[write++] = ep;
    }

    for (int32_t b = 0; b < SAP_MAX_BODIES; b++)
    {
        if (!sap->body_active[b] || sap->body_listed[b])
            continue;
        sap->endpoints[write++] = (SAPEndpoint){sap->aabb_min[b][0], (int16_t)b, false};
        sap->endpoints[write++] = (SAPEndpoint){sap->aabb_max[b][0], (int16_t)b, true};
        sap->body_listed[b] = true;
    }

    sap->endpoint_count = write;
}

static bool overlap_yz(const SAPBroadphase *sap, int32_t a, int32_t b)
{
    return sap->aabb_min[a][1] <= sap->aabb_max[b][1] && sap->aabb_min[b][1] <= sap->aabb_max[a][1] &&
           sap->aabb_min[a][2] <= sap->aabb_max[b][2] && sap->aabb_min[b][2] <= sap->aabb_max[a][2];
}

int32_t sap_query_pairs(SAPBroadphase *sap, SAPPair *out_pairs, int32_t max_pairs)
{
    refresh_endpoints(sap);
    insertion_sort_endpoints(sap->endpoints, sap->endpoint_count);

    /* Bodies whose x interval is open at the sweep position, and where each one sits */
    int16_t open[SAP_MAX_BODIES];
    int16_t open_slot[SAP_MAX_BODIES];
    memset(open_slot, 0xff, sizeof(open_slot)); /* -1: not open */
    int32_t open_count = 0;
    int32_t pair_count = 0;

    for (int32_t i = 0; i < sap->endpoint_count; i++)
    {
        const SAPEndpoint *ep = &sap->endpoints[i];
        int16_t body = ep->body_index;

        if (ep->is_max)
        {
            int16_t slot = open_slot[body];
            if (slot < 0)
                continue;
            assert(slot < open_count);
            open[slot] = open[--open_count];
            open_slot[open[slot]] = slot;
            open_slot[body] = -1;
            continue;
        }

        for (int32_t k = 0; k < open_count && pair_count < max_pairs; k++)
        {
            int16_t other = open[k];
            if (!overlap_yz(sap, body, other))
                continue;
            out_pairs[pair_count].body_a = body < other ? body : other;
            out_pairs[pair_count].body_b = body < other ? other : body;
            pair_count++;
        }

        open_slot[body] = (int16_t)open_count;
        open[open_count++] = body;
    }

    return pair_count;
}
