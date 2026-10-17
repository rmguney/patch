#ifndef PATCH_CONTENT_MATERIALS_H
#define PATCH_CONTENT_MATERIALS_H

#include "engine/core/types.h"
#include "engine/core/math.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Maximum number of materials (must match engine constant) */
#define MATERIAL_MAX_COUNT VOXEL_MATERIAL_MAX

    /* Material flags. The values reach the GPU palette as a float: the shaders treat
     * flags >= MAT_FLAG_TRANSPARENT as see-through (shaders/include/materials.glsl).
     *
     * Ground, rock, wood, leaves and props are SOLID (and all but metal BREAKABLE). Ground
     * flora (flowers, mushrooms) is BREAKABLE only: bodies and debris pass through it,
     * though it still rides along when the ground under it breaks loose. Water is
     * TRANSPARENT and LIQUID: nothing collides with it, it never joins a loose island or a
     * body, and picking rays pass through it. Air is TRANSPARENT only. */
    typedef enum
    {
        MAT_FLAG_SOLID = 1 << 0,       /* Rigid bodies and debris collide with it */
        MAT_FLAG_BREAKABLE = 1 << 1,   /* Player destruction removes it */
        MAT_FLAG_TRANSPARENT = 1 << 4, /* Light passes through; rendered as water (see below) */
        MAT_FLAG_LIQUID = 1 << 5,      /* Stays where it is: never part of an island or a body */
    } MaterialFlags;

    /* A transparent material is rendered as water whose colour is the light a white beam
     * keeps after 10 metres of it: the lighting derives the water's absorption per metre from
     * it (shaders/include/materials.glsl, WATER_COLOUR_DEPTH), and a water surface with
     * nothing opaque under it shows it. */

    /*
     * MaterialDescriptor: immutable definition of a material type.
     * RGB color is stored here, not per-voxel.
     */
    typedef struct
    {
        uint8_t r, g, b;   /* Base RGB color (sRGB) */
        uint8_t flags;     /* MaterialFlags */
        float density;     /* Mass per voxel unit (voxel object mass) */
        float friction;    /* Surface friction coefficient (rigid body ground contact) */
        float emissive;    /* Emissive intensity (0-1+, can exceed 1 for glow) */
        float roughness;   /* Surface roughness for specular (0=mirror, 1=matte) */
        float metallic;    /* Metallic factor for PBR (0=dielectric, 1=metal) */
    } MaterialDescriptor;

    /*
     * Global material table - explicit registration via pointer array.
     * Defined in materials.c, declared here for engine access.
     * Individual materials defined in content/materials/ (one file per material).
     */
    extern const MaterialDescriptor *const g_materials[MATERIAL_MAX_COUNT];
    extern const int32_t g_material_count;

    /* Lookup material by ID. Unregistered IDs return NULL. */
    static inline const MaterialDescriptor *material_get(uint8_t id)
    {
        return g_materials[id];
    }

    /* Get material color as Vec3 (0-1 range) */
    static inline Vec3 material_get_color(uint8_t id)
    {
        const MaterialDescriptor *mat = material_get(id);
        if (!mat)
            return vec3_create(0.0f, 0.0f, 0.0f);
        return vec3_create(
            (float)mat->r / 255.0f,
            (float)mat->g / 255.0f,
            (float)mat->b / 255.0f);
    }

    static inline bool material_is_breakable(uint8_t id)
    {
        const MaterialDescriptor *mat = material_get(id);
        return mat && (mat->flags & MAT_FLAG_BREAKABLE) != 0;
    }

    static inline bool material_is_solid(uint8_t id)
    {
        const MaterialDescriptor *mat = material_get(id);
        return mat && (mat->flags & MAT_FLAG_SOLID) != 0;
    }

    static inline bool material_is_liquid(uint8_t id)
    {
        const MaterialDescriptor *mat = material_get(id);
        return mat && (mat->flags & MAT_FLAG_LIQUID) != 0;
    }

/* Material IDs: indices into g_materials (registered in materials.c) */
#define MAT_AIR VOXEL_MATERIAL_EMPTY /* Canonical: 0 */
#define MAT_STONE 1
#define MAT_DIRT 2
#define MAT_GRASS 3
#define MAT_METAL 4
#define MAT_PINK 5
#define MAT_CYAN 6
#define MAT_PEACH 7
#define MAT_MINT 8
#define MAT_LAVENDER 9
#define MAT_SKY 10
#define MAT_TEAL 11
#define MAT_CORAL 12
#define MAT_CLOUD 13
#define MAT_ROSE 14
#define MAT_ORANGE 15
#define MAT_OAK_BARK 16
#define MAT_PINE_BARK 17
#define MAT_BIRCH_BARK 18
#define MAT_JUNGLE_BARK 19
#define MAT_OAK_LEAF 20
#define MAT_PINE_LEAF 21
#define MAT_BIRCH_LEAF 22
#define MAT_JUNGLE_LEAF 23
#define MAT_CHERRY_LEAF 24
#define MAT_AUTUMN_LEAF 25
#define MAT_DEAD_WOOD 26
#define MAT_FLOWER_RED 27
#define MAT_FLOWER_BLUE 28
#define MAT_FLOWER_YELLOW 29
#define MAT_MUSHROOM 30
#define MAT_SAND 31
#define MAT_SNOW 32
#define MAT_MOSS 33
#define MAT_GRAVEL 34
#define MAT_GLOWING_MUSHROOM 35
#define MAT_WATER 36
#define MAT_DRY_GRASS 37
#define MAT_JUNGLE_GRASS 38
#define MAT_MUD 39
#define MAT_PODZOL 40
#define MAT_SANDSTONE 41
#define MAT_ICE 42
#define MAT_CACTUS 43
#define MAT_SWAMP_WATER 44

/* The pastel colours of props and pillars: the consecutive IDs MAT_PINK .. MAT_ROSE */
#define MAT_PASTEL_FIRST MAT_PINK
#define MAT_PASTEL_COUNT (MAT_ROSE - MAT_PINK + 1)

#ifdef __cplusplus
}
#endif

#endif
