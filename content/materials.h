#ifndef PATCH_CONTENT_MATERIALS_H
#define PATCH_CONTENT_MATERIALS_H

#include "engine/core/types.h"
#include "engine/core/math.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Material ID 0 is reserved for empty/air (alias for engine constant) */
#define MATERIAL_ID_EMPTY VOXEL_MATERIAL_EMPTY

/* Maximum number of materials (must match engine constant) */
#define MATERIAL_MAX_COUNT VOXEL_MATERIAL_MAX

    /* Physical properties flags */
    typedef enum
    {
        MAT_FLAG_NONE = 0,
        MAT_FLAG_SOLID = 1 << 0,       /* Blocks movement */
        MAT_FLAG_BREAKABLE = 1 << 1,   /* Can be destroyed */
        MAT_FLAG_FLAMMABLE = 1 << 2,   /* Can burn */
        MAT_FLAG_CONDUCTIVE = 1 << 3,  /* Conducts electricity */
        MAT_FLAG_TRANSPARENT = 1 << 4, /* Light passes through */
        MAT_FLAG_LIQUID = 1 << 5,      /* Liquid material (water, lava) */
    } MaterialFlags;

    /*
     * MaterialDescriptor: immutable definition of a material type.
     * RGB color is stored here, not per-voxel.
     * Minecraft-style properties for destruction/interaction.
     */
    typedef struct
    {
        const char *name;  /* Display name (for debug/tools only) */
        uint8_t r, g, b;   /* Base RGB color */
        uint8_t flags;     /* MaterialFlags */
        float density;     /* Reserved for physics: mass per voxel unit */
        float hardness;    /* Resistance to damage (0-1), affects break time */
        float friction;    /* Surface friction coefficient */
        float restitution; /* Bounciness (0-1) */
        float emissive;    /* Emissive intensity (0-1+, can exceed 1 for glow) */
        float roughness;   /* Surface roughness for specular (0=mirror, 1=matte) */

        /* Minecraft-style destruction/interaction properties */
        float blast_resistance; /* Resistance to explosions (0-1) */
        float burn_rate;        /* How fast it burns when on fire (0=won't burn) */
        uint8_t drop_id;        /* Material dropped when destroyed (0xFF = same as self) */
        uint8_t tool_tier;      /* Minimum tool tier required to break (0=hand) */

        /* Render properties (single source of truth) */
        float metallic; /* Metallic factor for PBR (0=dielectric, 1=metal) */

        /* Liquid/transparency properties (for water, glass, etc.) */
        float transparency;   /* 0=opaque, 1=fully transparent */
        float ior;            /* Index of refraction (1.0=air, 1.33=water, 1.5=glass) */
        float absorption[3];  /* RGB absorption coefficients for colored transparency */
    } MaterialDescriptor;

    /* Drop ID constant: material drops itself */
#define MAT_DROP_SELF 0xFF

    /* Tool tier constants */
#define TOOL_TIER_HAND 0
#define TOOL_TIER_WOOD 1
#define TOOL_TIER_STONE 2
#define TOOL_TIER_IRON 3
#define TOOL_TIER_DIAMOND 4

    /* Static assertion for descriptor size (includes liquid properties) */
#ifdef __cplusplus
    static_assert(sizeof(MaterialDescriptor) >= 72 && sizeof(MaterialDescriptor) <= 88,
                  "MaterialDescriptor size unexpected");
#else
_Static_assert(sizeof(MaterialDescriptor) >= 72 && sizeof(MaterialDescriptor) <= 88,
               "MaterialDescriptor size unexpected");
#endif

    /*
     * Global material table - explicit registration via pointer array.
     * Defined in materials.c, declared here for engine access.
     * Individual materials defined in content/materials/ (one file per material).
     */
    extern const MaterialDescriptor *const g_materials[MATERIAL_MAX_COUNT];
    extern const int32_t g_material_count;

    /*
     * Lookup material by ID.
     * Returns pointer to material descriptor.
     * Out-of-range IDs return NULL (undefined materials).
     */
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

    /* Check material flags */
    static inline bool material_is_solid(uint8_t id)
    {
        const MaterialDescriptor *mat = material_get(id);
        return mat && (mat->flags & MAT_FLAG_SOLID) != 0;
    }

    static inline bool material_is_breakable(uint8_t id)
    {
        const MaterialDescriptor *mat = material_get(id);
        return mat && (mat->flags & MAT_FLAG_BREAKABLE) != 0;
    }

    /* Get material emissive intensity */
    static inline float material_get_emissive(uint8_t id)
    {
        const MaterialDescriptor *mat = material_get(id);
        return mat ? mat->emissive : 0.0f;
    }

    /* Get material roughness */
    static inline float material_get_roughness(uint8_t id)
    {
        const MaterialDescriptor *mat = material_get(id);
        return mat ? mat->roughness : 1.0f;
    }

    /* Get blast resistance */
    static inline float material_get_blast_resistance(uint8_t id)
    {
        const MaterialDescriptor *mat = material_get(id);
        return mat ? mat->blast_resistance : 0.0f;
    }

    /* Get metallic factor */
    static inline float material_get_metallic(uint8_t id)
    {
        const MaterialDescriptor *mat = material_get(id);
        return mat ? mat->metallic : 0.0f;
    }

    /* Get drop material ID (returns self if MAT_DROP_SELF) */
    static inline uint8_t material_get_drop_id(uint8_t id)
    {
        const MaterialDescriptor *mat = material_get(id);
        if (!mat)
            return 0;
        uint8_t drop = mat->drop_id;
        return (drop == MAT_DROP_SELF) ? id : drop;
    }

    /* Check if material can be broken by given tool tier */
    static inline bool material_can_break_with_tier(uint8_t id, uint8_t tool_tier)
    {
        const MaterialDescriptor *mat = material_get(id);
        return mat && (mat->flags & MAT_FLAG_BREAKABLE) != 0 && tool_tier >= mat->tool_tier;
    }

    /* Calculate damage multiplier based on hardness (higher hardness = more hits) */
    static inline float material_get_damage_multiplier(uint8_t id)
    {
        const MaterialDescriptor *mat = material_get(id);
        if (!mat)
            return 1.0f;
        float hardness = mat->hardness;
        return (hardness > 0.0f) ? (1.0f / hardness) : 1.0f;
    }

    /* Check if material is liquid */
    static inline bool material_is_liquid(uint8_t id)
    {
        const MaterialDescriptor *mat = material_get(id);
        return mat && (mat->flags & MAT_FLAG_LIQUID) != 0;
    }

    /* Get material transparency (0=opaque, 1=fully transparent) */
    static inline float material_get_transparency(uint8_t id)
    {
        const MaterialDescriptor *mat = material_get(id);
        return mat ? mat->transparency : 0.0f;
    }

    /* Get material index of refraction */
    static inline float material_get_ior(uint8_t id)
    {
        const MaterialDescriptor *mat = material_get(id);
        return mat ? mat->ior : 1.0f;
    }

/*
 * Predefined material IDs for common types.
 * These must match the registration order in materials.c.
 */
#define MAT_AIR VOXEL_MATERIAL_EMPTY /* Canonical: 0 */
#define MAT_STONE 1
#define MAT_DIRT 2
#define MAT_GRASS 3
#define MAT_SAND 4
#define MAT_WOOD 5
#define MAT_BRICK 6
#define MAT_CONCRETE 7
#define MAT_METAL 8
#define MAT_GLASS 9
#define MAT_WATER 10
#define MAT_FLESH 11
#define MAT_BONE 12
#define MAT_PINK 13
#define MAT_CYAN 14
#define MAT_PEACH 15
#define MAT_MINT 16
#define MAT_LAVENDER 17
#define MAT_SKY 18
#define MAT_TEAL 19
#define MAT_CORAL 20
#define MAT_CLOUD 21
#define MAT_ROSE 22
#define MAT_ORANGE 23
#define MAT_WHITE 24
#define MAT_YELLOW 25

#ifdef __cplusplus
}
#endif

#endif
