#ifndef PATCH_CORE_SPATIAL_HASH_H
#define PATCH_CORE_SPATIAL_HASH_H

#include "engine/core/types.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Spatial Hash Grid for O(n) broadphase collision detection
 *
 * Usage:
 *   1. spatial_hash_clear() at start of each frame
 *   2. spatial_hash_insert() for each object
 *   3. spatial_hash_query() or spatial_hash_for_each_pair() for collision pairs
 *
 * Cell size should be ~2x the largest object radius for best performance.
 * Objects spanning multiple cells are inserted into all overlapping cells.
 */

#define SPATIAL_HASH_MAX_ENTRIES 262144
#define SPATIAL_HASH_BUCKET_COUNT 32768
#define SPATIAL_HASH_MAX_PER_CELL 128
#define SPATIAL_HASH_MAX_OBJECTS 65536

typedef struct {
    int32_t object_index;
    int32_t next;  /* Index of next entry in bucket chain, -1 = end */
} SpatialHashEntry;

typedef struct {
    SpatialHashEntry entries[SPATIAL_HASH_MAX_ENTRIES];
    int32_t bucket_heads[SPATIAL_HASH_BUCKET_COUNT];  /* -1 = empty */
    int32_t entry_count;

    float cell_size;
    float inv_cell_size;
    Bounds3D bounds;

    /* Generation-based duplicate detection (O(1) per check, no clearing needed) */
    uint32_t query_generation;
    uint32_t object_seen_gen[SPATIAL_HASH_MAX_OBJECTS];
} SpatialHashGrid;

/* Initialize grid with cell size and world bounds */
void spatial_hash_init(SpatialHashGrid *grid, float cell_size, Bounds3D bounds);

/* Clear all entries (call each frame before inserting) */
void spatial_hash_clear(SpatialHashGrid *grid);

/* Insert object at position with radius (handles multi-cell objects) */
void spatial_hash_insert(SpatialHashGrid *grid, int32_t object_index, Vec3 position, float radius);

/* Insert object covering an AABB (for non-spherical objects) */
void spatial_hash_insert_aabb(SpatialHashGrid *grid, int32_t object_index, Vec3 min, Vec3 max);

/* Query all objects in cells near position+radius, returns count */
int32_t spatial_hash_query(const SpatialHashGrid *grid, Vec3 position, float radius,
                           int32_t *out_indices, int32_t max_results);

/* Callback for pair iteration */
typedef void (*SpatialHashPairCallback)(int32_t index_a, int32_t index_b, void *user_data);

/* Iterate all potential collision pairs (each pair reported once) */
void spatial_hash_for_each_pair(const SpatialHashGrid *grid, SpatialHashPairCallback callback, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* PATCH_CORE_SPATIAL_HASH_H */
