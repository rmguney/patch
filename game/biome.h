#ifndef PATCH_GAME_BIOME_H
#define PATCH_GAME_BIOME_H

#include "game/tree_gen.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        BIOME_GRASSLAND,
        BIOME_FOREST,
        BIOME_DESERT,
        BIOME_SNOW,
        BIOME_JUNGLE,
        BIOME_SWAMP,
        BIOME_SAVANNAH,
        BIOME_TAIGA,
        BIOME_MOUNTAIN,
        BIOME_COUNT
    } BiomeKind;

#define BIOME_MAX_TREES 7
#define BIOME_MAX_SHRUBS 2
#define BIOME_MAX_FLORA 3

    /* Above this terrain height over the terrain amplitude the mountain biome takes over */
#define BIOME_MOUNTAIN_ALTITUDE 0.7f

    typedef struct
    {
        TreeType species;
        float weight; /* Share among the biome's trees; 0 marks an unused slot */
    } BiomeTree;

    typedef enum
    {
        SHRUB_DOME,   /* Rounded leafy bush */
        SHRUB_COLUMN, /* One 2x2 voxel stem (cactus) */
        SHRUB_STALKS  /* Cluster of one-voxel stalks (reeds) */
    } ShrubShape;

    typedef struct
    {
        uint8_t material;
        uint8_t shape; /* ShrubShape */
        uint8_t min_height; /* Voxels */
        uint8_t max_height;
        float weight; /* Share among the biome's shrubs; 0 marks an unused slot */
    } BiomeShrub;

    /* One-voxel ground flora, grown on top of columns whose top voxel is on_material */
    typedef struct
    {
        uint8_t material;
        uint8_t on_material;
        float density;          /* Chance per matching column inside a patch; 0 marks an unused slot */
        float patch_wavelength; /* Metres; 0 spreads it evenly instead of in patches */
        float patch_threshold;  /* Patch noise (-1..1) below which it never grows */
    } BiomeFlora;

    /* What a biome looks like and grows. Material slots set to MAT_AIR are unused. */
    typedef struct
    {
        /* Ground, top down: surface (patch or cap in places), subsurface, rock. Steep
         * slopes show the rock. Depths are in voxels from the surface. */
        uint8_t surface;
        uint8_t patch;         /* Replaces the surface where the patch noise exceeds patch_threshold */
        float patch_threshold; /* -1..1, higher is rarer */
        uint8_t cap;           /* Replaces the surface above cap_altitude (relative altitude) */
        float cap_altitude;
        uint8_t subsurface;
        uint8_t rock;
        float surface_depth;
        float soil_depth;

        /* Standing water of its lakes and ponds: a transparent, liquid material, whose
         * colour sets how the water tints what lies under it (content/materials.h) */
        uint8_t water;
        /* Top voxel of standing water where the cover noise exceeds lake_cover_threshold
         * (-1 or below: everywhere) */
        uint8_t lake_cover;
        float lake_cover_threshold;

        float tree_chance; /* Chance that a tree candidate spot grows a tree */
        BiomeTree trees[BIOME_MAX_TREES];
        float shrub_chance; /* Chance that a shrub candidate spot grows a shrub */
        BiomeShrub shrubs[BIOME_MAX_SHRUBS];
        BiomeFlora flora[BIOME_MAX_FLORA];
    } BiomeDesc;

    extern const BiomeDesc g_biomes[BIOME_COUNT];

    /* Seed-derived constants of a world's biome field, made once per world and read-only
     * afterwards, so columns can be sampled in any order or in parallel */
    typedef struct
    {
        uint32_t seed;
        float trend_cos; /* Direction the temperature trend runs along; humidity runs across it */
        float trend_sin;
    } BiomeField;

    /* The smooth inputs of a column's biome. They vary over a metre or more, so they can
     * be sampled on a coarse grid and interpolated between. */
    typedef struct
    {
        float temperature; /* Cold -1 .. hot 1 */
        float humidity;    /* Dry -1 .. wet 1 */
        float edge;        /* Fine border noise (-1..1), also jitters the altitude lines */
        float patch;       /* Ground patch noise (about -1..1) */
    } BiomeSample;

    /* The biome and the top ground material of one terrain column */
    typedef struct
    {
        BiomeKind biome;
        uint8_t surface;
    } BiomeColumn;

    BiomeField biome_field_create(uint32_t seed);

    /* The biome inputs at a point. Temperature and humidity are a large-scale trend plus
     * noise, looked up through a domain warp so biome borders meander and fray instead of
     * following the noise lattice. */
    BiomeSample biome_field_sample(const BiomeField *field, float x, float z);

    /* Biome and top ground material for a sample; relative_altitude is the terrain height
     * over the terrain amplitude */
    BiomeColumn biome_resolve(BiomeSample sample, float relative_altitude);

    /* Biome of the nearest climate centroid, or the mountain above BIOME_MOUNTAIN_ALTITUDE */
    BiomeKind biome_classify(float temperature, float humidity, float relative_altitude);

    /* biome_field_sample's temperature alone, for the weather: rain or snow by location */
    float biome_field_temperature(const BiomeField *field, float x, float z);

#ifdef __cplusplus
}
#endif

#endif
