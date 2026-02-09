#include "content/scatter.h"
#include "content/materials.h"

const ScatterConfig g_scatter_configs[] = {
    /* Flowers on grass — temperate/humid biomes */
    {MAT_FLOWER_RED, MAT_GRASS, 0.025f, 8.0f, 0.3f, 0.0f, 0.7f, 0.3f, 0.5f},
    {MAT_FLOWER_BLUE, MAT_GRASS, 0.02f, 10.0f, 0.35f, -0.1f, 0.6f, 0.5f, 0.4f},
    {MAT_FLOWER_YELLOW, MAT_GRASS, 0.02f, 12.0f, 0.3f, 0.3f, 0.6f, 0.2f, 0.5f},

    /* Mushrooms on dirt — cool/humid biomes */
    {MAT_MUSHROOM, MAT_DIRT, 0.015f, 5.0f, 0.5f, -0.2f, 0.5f, 0.5f, 0.4f},
    {MAT_MUSHROOM, MAT_GRASS, 0.008f, 6.0f, 0.55f, -0.3f, 0.4f, 0.6f, 0.3f},

    /* Moss on stone/dirt — cool+humid biomes (swamp/taiga/forest) */
    {MAT_MOSS, MAT_STONE, 0.02f, 7.0f, 0.4f, -0.2f, 0.5f, 0.5f, 0.4f},
    {MAT_MOSS, MAT_DIRT, 0.015f, 9.0f, 0.35f, 0.0f, 0.6f, 0.4f, 0.4f},

    /* Glowing mushrooms on stone — rare, cool/dark biomes */
    {MAT_GLOWING_MUSHROOM, MAT_STONE, 0.005f, 4.0f, 0.6f, -0.3f, 0.4f, 0.6f, 0.3f},

    /* Gravel on sand — desert/river edges */
    {MAT_GRAVEL, MAT_SAND, 0.018f, 6.0f, 0.4f, 0.4f, 0.5f, 0.1f, 0.4f},

    /* Savannah flowers — hot/dry grassland */
    {MAT_FLOWER_RED, MAT_GRASS, 0.012f, 14.0f, 0.35f, 0.4f, 0.4f, 0.15f, 0.3f},

    /* Alpine flowers — rare on snow */
    {MAT_FLOWER_YELLOW, MAT_SNOW, 0.006f, 15.0f, 0.5f, -0.3f, 0.3f, 0.3f, 0.3f},
};

const int32_t g_scatter_config_count = sizeof(g_scatter_configs) / sizeof(g_scatter_configs[0]);
