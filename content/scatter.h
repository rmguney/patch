#ifndef PATCH_CONTENT_SCATTER_H
#define PATCH_CONTENT_SCATTER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        uint8_t material;
        uint8_t surface_material;
        float density;
        float noise_wavelength;
        float noise_threshold;
        float temp_center;
        float temp_tolerance;
        float humidity_center;
        float humidity_tolerance;
    } ScatterConfig;

    extern const ScatterConfig g_scatter_configs[];
    extern const int32_t g_scatter_config_count;

#ifdef __cplusplus
}
#endif

#endif
