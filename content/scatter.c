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
};

const int32_t g_scatter_config_count = sizeof(g_scatter_configs) / sizeof(g_scatter_configs[0]);
