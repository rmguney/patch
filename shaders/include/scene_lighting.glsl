layout(std140, SET_BINDING(LIGHTING_SET, 0)) uniform SceneLightingBlock {
    vec4 sun_dir;           /* .w = wrap_bias */
    vec4 sun_color;         /* .w = strength */
    vec4 fill_dir;          /* .w = strength */
    vec4 fill_color;        /* .w = back_strength */
    vec4 back_dir;          /* .w = rim_strength */
    vec4 back_color;        /* .w = ambient_strength */
    vec4 sky_ambient;       /* .w = exposure */
    vec4 ground_ambient;    /* .w = emissive_mult */
    vec4 sky_color_params;  /* .w = shadow_max_dist */
} lighting;
