#ifndef HDDA_TYPES_GLSL
#define HDDA_TYPES_GLSL

struct HitInfo {
    bool hit;
    vec3 pos;           /* World space hit position */
    vec3 normal;        /* World space surface normal */
    float t;            /* Ray parameter at hit */
    uint material_id;   /* Material ID (0 = empty) */
    ivec3 voxel_coord;  /* Grid coordinate of hit voxel */
};

struct RayConfig {
    vec3 origin;        /* Ray origin in world space */
    vec3 direction;     /* Normalized ray direction */
    float t_min;        /* Minimum ray parameter */
    float t_max;        /* Maximum ray parameter */
    int max_steps;      /* Maximum DDA steps */
};

struct DDAState {
    ivec3 map_pos;      /* Current voxel position */
    ivec3 step_dir;     /* Step direction per axis (-1 or +1) */
    vec3 side_dist;     /* Distance to next voxel boundary per axis */
    vec3 delta_dist;    /* Distance between boundaries per axis */
    bvec3 last_mask;    /* Which axis was stepped last (for normal) */
};

const int CHUNK_SIZE = 32;          /* Voxels per chunk dimension */
const int REGION_SIZE = 8;          /* Voxels per region dimension (8x8x8) */
const int REGIONS_PER_CHUNK = 4;    /* Regions per chunk dimension (4x4x4 = 64 regions) */
const int CHUNK_UINT_COUNT = 8192;  /* uint32s per chunk for voxel data (32768 voxels / 4) */

const uint MATERIAL_STRIDE = 2u;    /* 2 vec4s per material entry */

#endif /* HDDA_TYPES_GLSL */
