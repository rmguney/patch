#include "game/biome.h"
#include "content/materials.h"
#include "engine/core/math.h"
#include "engine/core/noise.h"
#include <math.h>

/* Noise seed offsets (added to the world seed) */
#define TEMPERATURE_SEED 3000u
#define HUMIDITY_SEED 4000u
#define WARP_X_SEED 7000u
#define WARP_Z_SEED 8000u
#define EDGE_X_SEED 9000u
#define EDGE_Z_SEED 10000u
#define PATCH_SEED 11000u
#define TREND_SEED 13000u

/* Temperature and humidity follow a triangle-wave trend along two perpendicular,
 * seed-rotated directions (extremes TREND_SPAN metres from the temperate middle, so the
 * ~154 m world spans the whole climate range) plus value noise for irregular regions */
#define TREND_SPAN 64.0f
#define TREND_WEIGHT 0.85f
#define CLIMATE_NOISE_WEIGHT 0.45f
#define CLIMATE_NOISE_WAVELENGTH 34.0f

/* The climate is looked up through two warps: a broad one bends the borders into
 * meanders, a fine one frays them so neighbouring biomes interleave over about a metre */
#define WARP_WAVELENGTH 22.0f
#define WARP_AMPLITUDE 8.0f
#define EDGE_WAVELENGTH 1.6f
#define EDGE_AMPLITUDE 0.9f
/* The fine warp also jitters the mountain line and the snow caps (relative altitude) */
#define ALTITUDE_JITTER 0.05f

#define PATCH_WAVELENGTH 5.0f

/* Every biome but the mountain owns the climates closest to its centroid (temperature
 * cold -1 .. hot 1, humidity dry -1 .. wet 1) */
static const struct
{
    BiomeKind biome;
    float temperature;
    float humidity;
} CLIMATE_CENTROIDS[] = {
    {BIOME_SNOW, -0.75f, -0.45f},
    {BIOME_TAIGA, -0.70f, 0.50f},
    {BIOME_GRASSLAND, 0.00f, -0.55f},
    {BIOME_FOREST, -0.05f, 0.25f},
    {BIOME_SWAMP, 0.30f, 0.85f},
    {BIOME_SAVANNAH, 0.65f, -0.10f},
    {BIOME_DESERT, 0.80f, -0.75f},
    {BIOME_JUNGLE, 0.80f, 0.65f},
};
#define CLIMATE_CENTROID_COUNT (int32_t)(sizeof(CLIMATE_CENTROIDS) / sizeof(CLIMATE_CENTROIDS[0]))
STATIC_ASSERT(CLIMATE_CENTROID_COUNT == BIOME_COUNT - 1, "Every biome but the mountain needs a climate centroid");

const BiomeDesc g_biomes[BIOME_COUNT] = {
    [BIOME_GRASSLAND] = {
        .surface = MAT_GRASS,
        .subsurface = MAT_DIRT,
        .rock = MAT_STONE,
        .surface_depth = 1.5f,
        .soil_depth = 4.0f,
        .water = MAT_WATER,
        /* Open meadow: scattered broadleaf and blossom trees */
        .tree_chance = 0.15f,
        .trees = {{TREE_OAK, 4.0f}, {TREE_CHERRY, 2.0f}, {TREE_BIRCH, 2.0f}, {TREE_CHESTNUT, 2.0f}},
        .shrub_chance = 0.3f,
        .shrubs = {{MAT_OAK_LEAF, SHRUB_DOME, 2, 3, 3.0f}, {MAT_CHERRY_LEAF, SHRUB_DOME, 2, 4, 1.0f}},
        .flora = {{MAT_FLOWER_RED, MAT_GRASS, 0.04f, 8.0f, 0.1f},
                  {MAT_FLOWER_YELLOW, MAT_GRASS, 0.04f, 11.0f, 0.1f},
                  {MAT_FLOWER_BLUE, MAT_GRASS, 0.03f, 9.0f, 0.2f}},
    },
    [BIOME_FOREST] = {
        .surface = MAT_MOSS,
        .patch = MAT_DIRT,
        .patch_threshold = 0.35f,
        .subsurface = MAT_DIRT,
        .rock = MAT_STONE,
        .surface_depth = 1.5f,
        .soil_depth = 4.0f,
        .water = MAT_WATER,
        /* Dense mixed broadleaf forest in autumn colours, the odd redwood towering over it */
        .tree_chance = 0.95f,
        .trees = {{TREE_OAK, 3.0f}, {TREE_MAPLE, 3.0f}, {TREE_AUTUMN, 3.0f}, {TREE_BIRCH, 2.0f},
                  {TREE_CHESTNUT, 2.0f}, {TREE_REDWOOD, 0.5f}},
        .shrub_chance = 0.35f,
        .shrubs = {{MAT_OAK_LEAF, SHRUB_DOME, 2, 3, 2.0f}, {MAT_BIRCH_LEAF, SHRUB_DOME, 2, 3, 1.0f}},
        .flora = {{MAT_MUSHROOM, MAT_MOSS, 0.02f, 5.0f, 0.0f},
                  {MAT_FLOWER_BLUE, MAT_MOSS, 0.01f, 10.0f, 0.2f},
                  {MAT_MUSHROOM, MAT_DIRT, 0.015f, 0.0f, 0.0f}},
    },
    [BIOME_DESERT] = {
        .surface = MAT_SAND,
        .subsurface = MAT_SANDSTONE,
        .rock = MAT_SANDSTONE,
        .surface_depth = 3.0f,
        .soil_depth = 5.0f,
        .water = MAT_WATER,
        /* Too dry for the living species of the other biomes: only bleached dead snags and
         * the odd palm */
        .tree_chance = 0.06f,
        .trees = {{TREE_DEAD, 3.0f}, {TREE_PALM, 1.0f}},
        .shrub_chance = 0.2f,
        .shrubs = {{MAT_CACTUS, SHRUB_COLUMN, 4, 8, 2.0f}, {MAT_DEAD_WOOD, SHRUB_DOME, 1, 2, 1.0f}},
        .flora = {{MAT_GRAVEL, MAT_SAND, 0.012f, 6.0f, 0.0f},
                  {MAT_DEAD_WOOD, MAT_SAND, 0.002f, 0.0f, 0.0f}},
    },
    [BIOME_SNOW] = {
        .surface = MAT_SNOW,
        .patch = MAT_GRAVEL,
        .patch_threshold = 0.6f,
        .subsurface = MAT_GRAVEL,
        .rock = MAT_STONE,
        .surface_depth = 2.0f,
        .soil_depth = 5.0f,
        .water = MAT_WATER,
        .lake_cover = MAT_ICE,
        .lake_cover_threshold = -1.0f,
        /* Tundra: a few frost pines and dead trunks */
        .tree_chance = 0.08f,
        .trees = {{TREE_FROSTPINE, 4.0f}, {TREE_DEAD, 1.0f}},
        .shrub_chance = 0.12f,
        .shrubs = {{MAT_PINE_LEAF, SHRUB_DOME, 1, 2, 1.0f}},
        .flora = {{MAT_STONE, MAT_SNOW, 0.004f, 0.0f, 0.0f},
                  {MAT_FLOWER_YELLOW, MAT_SNOW, 0.003f, 12.0f, 0.3f}},
    },
    [BIOME_JUNGLE] = {
        .surface = MAT_JUNGLE_GRASS,
        .patch = MAT_MUD,
        .patch_threshold = 0.5f,
        .subsurface = MAT_MUD,
        .rock = MAT_STONE,
        .surface_depth = 1.5f,
        .soil_depth = 5.0f,
        .water = MAT_WATER,
        /* Rainforest canopy with rare giants */
        .tree_chance = 1.0f,
        .trees = {{TREE_JUNGLE, 5.0f}, {TREE_PALM, 2.0f}, {TREE_GIANT, 0.5f}},
        .shrub_chance = 0.5f,
        .shrubs = {{MAT_JUNGLE_LEAF, SHRUB_DOME, 3, 5, 3.0f}, {MAT_OAK_LEAF, SHRUB_DOME, 2, 3, 1.0f}},
        .flora = {{MAT_FLOWER_RED, MAT_JUNGLE_GRASS, 0.02f, 7.0f, 0.1f},
                  {MAT_GLOWING_MUSHROOM, MAT_JUNGLE_GRASS, 0.004f, 5.0f, 0.3f},
                  {MAT_MUSHROOM, MAT_MUD, 0.02f, 0.0f, 0.0f}},
    },
    [BIOME_SWAMP] = {
        .surface = MAT_MUD,
        .patch = MAT_MOSS,
        .patch_threshold = 0.2f,
        .subsurface = MAT_MUD,
        .rock = MAT_STONE,
        .surface_depth = 2.0f,
        .soil_depth = 6.0f,
        .water = MAT_SWAMP_WATER,
        /* Duckweed mats on the ponds */
        .lake_cover = MAT_MOSS,
        .lake_cover_threshold = 0.25f,
        .tree_chance = 0.55f,
        .trees = {{TREE_SWAMP, 4.0f}, {TREE_MANGROVE, 3.0f}, {TREE_DEAD, 1.0f}},
        .shrub_chance = 0.4f,
        .shrubs = {{MAT_DRY_GRASS, SHRUB_STALKS, 3, 6, 3.0f}, {MAT_OAK_LEAF, SHRUB_DOME, 2, 3, 1.0f}},
        .flora = {{MAT_GLOWING_MUSHROOM, MAT_MUD, 0.006f, 5.0f, 0.2f},
                  {MAT_MUSHROOM, MAT_MUD, 0.015f, 6.0f, 0.0f},
                  {MAT_MUSHROOM, MAT_MOSS, 0.01f, 0.0f, 0.0f}},
    },
    [BIOME_SAVANNAH] = {
        .surface = MAT_DRY_GRASS,
        .patch = MAT_DIRT,
        .patch_threshold = 0.3f,
        .subsurface = MAT_DIRT,
        .rock = MAT_STONE,
        .surface_depth = 1.5f,
        .soil_depth = 4.0f,
        .water = MAT_WATER,
        /* Lone flat-topped acacias and baobabs */
        .tree_chance = 0.25f,
        .trees = {{TREE_ACACIA, 5.0f}, {TREE_BAOBAB, 2.0f}},
        .shrub_chance = 0.25f,
        .shrubs = {{MAT_AUTUMN_LEAF, SHRUB_DOME, 1, 2, 2.0f}, {MAT_OAK_LEAF, SHRUB_DOME, 1, 2, 1.0f}},
        .flora = {{MAT_DRY_GRASS, MAT_DRY_GRASS, 0.05f, 3.0f, -0.2f},
                  {MAT_FLOWER_YELLOW, MAT_DRY_GRASS, 0.008f, 14.0f, 0.1f}},
    },
    [BIOME_TAIGA] = {
        .surface = MAT_PODZOL,
        .patch = MAT_SNOW,
        .patch_threshold = 0.15f,
        .subsurface = MAT_DIRT,
        .rock = MAT_STONE,
        .surface_depth = 1.5f,
        .soil_depth = 4.0f,
        .water = MAT_WATER,
        /* Dense conifers */
        .tree_chance = 0.9f,
        .trees = {{TREE_PINE, 4.0f}, {TREE_FROSTPINE, 2.0f}, {TREE_CEDAR, 2.0f}, {TREE_BIRCH, 1.0f},
                  {TREE_REDWOOD, 1.0f}},
        .shrub_chance = 0.3f,
        .shrubs = {{MAT_PINE_LEAF, SHRUB_DOME, 2, 3, 1.0f}},
        .flora = {{MAT_MUSHROOM, MAT_PODZOL, 0.015f, 6.0f, 0.0f},
                  {MAT_MOSS, MAT_PODZOL, 0.03f, 5.0f, 0.1f}},
    },
    [BIOME_MOUNTAIN] = {
        .surface = MAT_STONE,
        .patch = MAT_GRAVEL,
        .patch_threshold = 0.25f,
        .cap = MAT_SNOW,
        .cap_altitude = 1.15f,
        .subsurface = MAT_GRAVEL,
        .rock = MAT_STONE,
        .surface_depth = 2.0f,
        .soil_depth = 4.0f,
        .water = MAT_WATER,
        /* Sparse conifers on the slopes */
        .tree_chance = 0.06f,
        .trees = {{TREE_CEDAR, 2.0f}, {TREE_PINE, 2.0f}, {TREE_FROSTPINE, 1.0f}},
        .shrub_chance = 0.1f,
        .shrubs = {{MAT_PINE_LEAF, SHRUB_DOME, 1, 2, 1.0f}},
        .flora = {{MAT_MOSS, MAT_STONE, 0.015f, 7.0f, 0.0f},
                  {MAT_FLOWER_BLUE, MAT_GRAVEL, 0.006f, 9.0f, 0.2f}},
    },
};

/* Triangle wave through 0 at 0, 1 at TREND_SPAN and -1 at -TREND_SPAN */
static float climate_trend(float u)
{
    float p = u * (0.25f / TREND_SPAN) + 0.25f;
    return 1.0f - 4.0f * fabsf(p - floorf(p) - 0.5f);
}

/* Temperature at a warped point */
static float climate_temperature(const BiomeField *field, float wx, float wz)
{
    const float nf = 1.0f / CLIMATE_NOISE_WAVELENGTH;
    float u = wx * field->trend_cos + wz * field->trend_sin;
    return clampf(TREND_WEIGHT * climate_trend(u) +
                      CLIMATE_NOISE_WEIGHT * noise_value_2d(wx * nf, wz * nf, field->seed + TEMPERATURE_SEED),
                  -1.0f, 1.0f);
}

/* Humidity at a warped point */
static float climate_humidity(const BiomeField *field, float wx, float wz)
{
    const float nf = 1.0f / CLIMATE_NOISE_WAVELENGTH;
    float v = wz * field->trend_cos - wx * field->trend_sin;
    return clampf(TREND_WEIGHT * climate_trend(v) +
                      CLIMATE_NOISE_WEIGHT * noise_value_2d(wx * nf, wz * nf, field->seed + HUMIDITY_SEED),
                  -1.0f, 1.0f);
}

/* The warped point the climate of (x, z) is looked up at; *edge_x is the fine warp's x
 * noise (BiomeSample.edge) */
static void climate_lookup_point(const BiomeField *field, float x, float z, float *wx, float *wz, float *edge_x)
{
    const float wf = 1.0f / WARP_WAVELENGTH;
    const float ef = 1.0f / EDGE_WAVELENGTH;
    *edge_x = noise_value_2d(x * ef, z * ef, field->seed + EDGE_X_SEED);
    float edge_z = noise_value_2d(x * ef, z * ef, field->seed + EDGE_Z_SEED);
    *wx = x + noise_value_2d(x * wf, z * wf, field->seed + WARP_X_SEED) * WARP_AMPLITUDE + *edge_x * EDGE_AMPLITUDE;
    *wz = z + noise_value_2d(x * wf, z * wf, field->seed + WARP_Z_SEED) * WARP_AMPLITUDE + edge_z * EDGE_AMPLITUDE;
}

BiomeField biome_field_create(uint32_t seed)
{
    /* The noise hash at a lattice point doubles as a seed hash */
    float angle = K_PI * noise_value_2d(0.0f, 0.0f, seed + TREND_SEED);
    BiomeField field;
    field.seed = seed;
    field.trend_cos = cosf(angle);
    field.trend_sin = sinf(angle);
    return field;
}

BiomeSample biome_field_sample(const BiomeField *field, float x, float z)
{
    const float pf = 1.0f / PATCH_WAVELENGTH;
    BiomeSample s;
    float wx, wz;
    climate_lookup_point(field, x, z, &wx, &wz, &s.edge);
    s.temperature = climate_temperature(field, wx, wz);
    s.humidity = climate_humidity(field, wx, wz);
    /* Two octaves peak at about 1.5: scaled to about -1..1 */
    s.patch = noise_fbm_2d(x * pf, z * pf, field->seed + PATCH_SEED, 2, 2.0f, 0.5f) * (1.0f / 1.5f);
    return s;
}

BiomeKind biome_classify(float temperature, float humidity, float relative_altitude)
{
    if (relative_altitude > BIOME_MOUNTAIN_ALTITUDE)
        return BIOME_MOUNTAIN;

    BiomeKind best = CLIMATE_CENTROIDS[0].biome;
    float best_d2 = INFINITY;
    for (int32_t i = 0; i < CLIMATE_CENTROID_COUNT; i++)
    {
        float dt = temperature - CLIMATE_CENTROIDS[i].temperature;
        float dh = humidity - CLIMATE_CENTROIDS[i].humidity;
        float d2 = dt * dt + dh * dh;
        if (d2 < best_d2)
        {
            best_d2 = d2;
            best = CLIMATE_CENTROIDS[i].biome;
        }
    }
    return best;
}

BiomeColumn biome_resolve(BiomeSample sample, float relative_altitude)
{
    float altitude = relative_altitude + sample.edge * ALTITUDE_JITTER;
    BiomeColumn col;
    col.biome = biome_classify(sample.temperature, sample.humidity, altitude);

    const BiomeDesc *desc = &g_biomes[col.biome];
    col.surface = desc->surface;
    if (desc->cap != MAT_AIR && altitude > desc->cap_altitude)
        col.surface = desc->cap;
    else if (desc->patch != MAT_AIR && sample.patch > desc->patch_threshold)
        col.surface = desc->patch;
    return col;
}

float biome_field_temperature(const BiomeField *field, float x, float z)
{
    float wx, wz, edge_x;
    climate_lookup_point(field, x, z, &wx, &wz, &edge_x);
    return climate_temperature(field, wx, wz);
}
