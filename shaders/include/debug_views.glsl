#ifndef DEBUG_VIEWS_GLSL
#define DEBUG_VIEWS_GLSL

/*
 * debug_views.glsl - values of the push constant debug_mode (F4 / F5 in the app).
 * Keep in step with TERRAIN_DEBUG_MODE_NAMES in app/app_debug.cpp.
 */

#define DEBUG_VIEW_NORMALS      1
#define DEBUG_VIEW_ALBEDO       2
#define DEBUG_VIEW_DEPTH        3
#define DEBUG_VIEW_ROUGHNESS    4
#define DEBUG_VIEW_METALLIC     5
#define DEBUG_VIEW_EMISSIVE     6
#define DEBUG_VIEW_WATER_DEPTH  7
#define DEBUG_VIEW_OBJECTS      8
#define DEBUG_VIEW_WORLD_POS    9
#define DEBUG_VIEW_SHADOW_UVW   10
#define DEBUG_VIEW_SHADOW       11
#define DEBUG_VIEW_AO           12
#define DEBUG_VIEW_DENOISE_RAW  13
#define DEBUG_VIEW_DENOISE_DIFF 14
#define DEBUG_VIEW_SPECULAR_ENV 15

#endif
