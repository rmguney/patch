cmake_minimum_required(VERSION 3.21)
project(patch VERSION 1.0.0 LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

add_compile_definitions(_CRT_SECURE_NO_WARNINGS)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(PATCH_ENABLE_WARNINGS "Enable compiler warnings" ON)
option(PATCH_WARNINGS_AS_ERRORS "Treat compiler warnings in the project's own targets as errors (CI)" OFF)
option(PATCH_BUILD_APP "Vulkan renderer, app and GPU tests" ${WIN32})
option(PATCH_RUN_TESTS_ON_BUILD "Run the unit tests after every app build (test_scenes alone takes ~100 s)" OFF)
option(PATCH_USE_PREBUILT_SHADERS "Use the checked-in SPIR-V header instead of compiling shaders (no glslc needed)" OFF)
option(PATCH_ENABLE_PROFILING "Enable CPU/GPU profiling (F2 overlay in app)" ON)
option(PATCH_ENABLE_SANITIZERS "Enable sanitizers (ASan + UBSan)" OFF)
option(PATCH_ENABLE_CLANG_TIDY "Enable clang-tidy static analysis target" OFF)

# Clang-cl: CMake lacks C23 flag mapping for clang-cl MSVC frontend
if(CMAKE_C_COMPILER_ID STREQUAL "Clang" AND "x${CMAKE_C_COMPILER_FRONTEND_VARIANT}" STREQUAL "xMSVC")
    set(CMAKE_C23_STANDARD_COMPILE_OPTION "/clang:-std=c23")
    set(CMAKE_C23_EXTENSION_COMPILE_OPTION "/clang:-std=gnu23")
    set(CMAKE_C_STANDARD_LATEST 23)
elseif(MSVC AND NOT CMAKE_C23_STANDARD_COMPILE_OPTION)
    # MSVC cl: older CMake has no C23 mapping either
    set(CMAKE_C23_STANDARD_COMPILE_OPTION "/std:clatest")
    set(CMAKE_C23_EXTENSION_COMPILE_OPTION "/std:clatest")
    set(CMAKE_C_STANDARD_LATEST 23)
endif()

# C++ exceptions are opted into per target (the engine builds without them), so drop
# CMake's default /EHsc: cl warns (D9025) whenever a target overrides it
if(MSVC)
    string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
endif()

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(PATCH_BUILD_APP AND NOT WIN32)
    message(FATAL_ERROR
        "The app needs Win32 for now (window, entry point).\n"
        "Configure with -DPATCH_BUILD_APP=OFF to build the core libraries and tests.")
endif()

# Sanitizers (ASan + UBSan) instrument every target. Set up before the first target is
# created: targets take CMAKE_MSVC_RUNTIME_LIBRARY at creation.
if(PATCH_ENABLE_SANITIZERS)
    if(NOT MSVC)
        add_compile_definitions(PATCH_SANITIZERS_ENABLED)
        add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    elseif(CMAKE_C_COMPILER_ID STREQUAL "Clang")
        # clang-cl. ASan needs the release DLL runtime in every configuration.
        set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)

        # CMake links with the MSVC-style linker, which does not add the runtimes the clang
        # driver would. Ask the driver which ones it passes the linker (libraries it embeds
        # in the objects need nothing): the names change between LLVM releases, and with
        # ASan the UBSan runtime is part of the ASan one.
        file(WRITE "${CMAKE_BINARY_DIR}/sanitizer_probe.c" "int main(void) { return 0; }\n")
        execute_process(
            COMMAND "${CMAKE_C_COMPILER}" "-###" -fsanitize=address,undefined /MD "${CMAKE_BINARY_DIR}/sanitizer_probe.c"
            OUTPUT_VARIABLE SANITIZER_PROBE_OUT
            ERROR_VARIABLE SANITIZER_PROBE_ERR)
        string(REGEX MATCHALL "\"(-wholearchive:)?[A-Za-z]:[^\"]*clang_rt[^\"]*\\.lib\""
               SANITIZER_RUNTIMES "${SANITIZER_PROBE_OUT}${SANITIZER_PROBE_ERR}")
        set(SANITIZER_LINK_OPTIONS "")
        set(SANITIZER_DLL "")
        foreach(runtime IN LISTS SANITIZER_RUNTIMES)
            string(REPLACE "\"" "" runtime "${runtime}")
            string(REPLACE "\\\\" "/" runtime "${runtime}")
            list(APPEND SANITIZER_LINK_OPTIONS "${runtime}")
            if(runtime MATCHES "clang_rt\\.asan_dynamic-[^/]*\\.lib$")
                string(REGEX REPLACE "\\.lib$" ".dll" SANITIZER_DLL "${runtime}")
            endif()
        endforeach()
        if(NOT SANITIZER_DLL OR NOT EXISTS "${SANITIZER_DLL}")
            message(FATAL_ERROR "clang-cl named no ASan runtime DLL for -fsanitize=address,undefined "
                                "(runtimes: ${SANITIZER_LINK_OPTIONS})")
        endif()

        add_compile_definitions(PATCH_SANITIZERS_ENABLED)
        add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all)
        add_link_options(${SANITIZER_LINK_OPTIONS} /INCREMENTAL:NO)
        # The runtime DLL next to the executables
        file(COPY "${SANITIZER_DLL}" DESTINATION "${CMAKE_BINARY_DIR}")
        message(STATUS "Sanitizers enabled: ASan + UBSan (clang-cl), runtimes: ${SANITIZER_LINK_OPTIONS}")
    else()
        message(WARNING "PATCH_ENABLE_SANITIZERS needs gcc, clang or clang-cl; ignored for MSVC cl")
    endif()
endif()

# No RTTI or exceptions in C++ code (MSVC is also true for clang-cl)
function(patch_disable_rtti_exceptions tgt)
    if(MSVC)
        target_compile_options(${tgt} PRIVATE /GR- /EHs-c-)
    else()
        target_compile_options(${tgt} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-fno-rtti;-fno-exceptions>")
    endif()
    target_compile_definitions(${tgt} PRIVATE _HAS_EXCEPTIONS=0)
endfunction()

function(patch_enable_warnings tgt)
    if(NOT PATCH_ENABLE_WARNINGS)
        return()
    endif()
    if(MSVC)
        target_compile_options(${tgt} PRIVATE /W4 /permissive-)
        if(PATCH_WARNINGS_AS_ERRORS)
            target_compile_options(${tgt} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${tgt} PRIVATE -Wall -Wextra)
        if(PATCH_WARNINGS_AS_ERRORS)
            target_compile_options(${tgt} PRIVATE -Werror)
        endif()
    endif()
endfunction()

if(PATCH_BUILD_APP)
    find_package(Vulkan REQUIRED)
endif()

# -----------------------------------------------------------------------------
# Static library: engine_core (C only)
# -----------------------------------------------------------------------------
set(ENGINE_CORE_SOURCES
    engine/core/types.h
    engine/core/math.h
    engine/core/rng.h
    engine/core/noise.h
    engine/core/noise.c
    engine/core/spatial_hash.h
    engine/core/spatial_hash.c
    engine/core/profile.h
    engine/core/profile.c
    engine/core/jobs.h
    engine/core/jobs.c
)

add_library(engine_core STATIC ${ENGINE_CORE_SOURCES})
target_include_directories(engine_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(engine_core PUBLIC engine_platform_time)
# glibc keeps sinf, powf, floorf... in libm, which the C compiler driver does not link by itself
if(UNIX)
    target_link_libraries(engine_core PUBLIC m)
endif()
set_property(TARGET engine_core PROPERTY C_STANDARD 23)
set_property(TARGET engine_core PROPERTY C_STANDARD_REQUIRED ON)
set_property(TARGET engine_core PROPERTY C_EXTENSIONS OFF)
# The job system's C11 atomics: MSVC cl still gates <stdatomic.h> behind this switch
if(CMAKE_C_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(engine_core PRIVATE /experimental:c11atomics)
endif()

# -----------------------------------------------------------------------------
# Static library: engine_voxel (C only)
# -----------------------------------------------------------------------------
set(ENGINE_VOXEL_SOURCES
    engine/voxel/chunk.h
    engine/voxel/chunk.c
    engine/voxel/volume.h
    engine/voxel/volume.c
    engine/voxel/volume_raycast.c
    engine/voxel/volume_shadow.c
    engine/voxel/connectivity.h
    engine/voxel/connectivity.c
    engine/voxel/voxel_object.h
    engine/voxel/voxel_object.c
    engine/voxel/bvh.h
    engine/voxel/bvh.c
)

add_library(engine_voxel STATIC ${ENGINE_VOXEL_SOURCES})
target_link_libraries(engine_voxel PUBLIC engine_core content)
set_property(TARGET engine_voxel PROPERTY C_STANDARD 23)
set_property(TARGET engine_voxel PROPERTY C_STANDARD_REQUIRED ON)
set_property(TARGET engine_voxel PROPERTY C_EXTENSIONS OFF)

if(PATCH_ENABLE_PROFILING)
    target_compile_definitions(engine_voxel PRIVATE PATCH_PROFILE)
endif()

# -----------------------------------------------------------------------------
# Static library: engine_physics (C only)
# -----------------------------------------------------------------------------
set(ENGINE_PHYSICS_SOURCES
    engine/physics/particles.h
    engine/physics/particles.c
    engine/physics/rigidbody.h
    engine/physics/rigidbody.c
    engine/physics/broadphase.h
    engine/physics/broadphase.c
    engine/physics/convex_hull.h
    engine/physics/convex_hull.c
    engine/physics/gjk.h
    engine/physics/gjk.c
    engine/physics/collision_object.h
    engine/physics/collision_object.c
)

add_library(engine_physics STATIC ${ENGINE_PHYSICS_SOURCES})
target_link_libraries(engine_physics PUBLIC engine_core engine_voxel content)
set_property(TARGET engine_physics PROPERTY C_STANDARD 23)
set_property(TARGET engine_physics PROPERTY C_STANDARD_REQUIRED ON)
set_property(TARGET engine_physics PROPERTY C_EXTENSIONS OFF)

if(PATCH_ENABLE_PROFILING)
    target_compile_definitions(engine_physics PRIVATE PATCH_PROFILE)
endif()

# -----------------------------------------------------------------------------
# Static library: engine_sim (C only)
# -----------------------------------------------------------------------------
set(ENGINE_SIM_SOURCES
    engine/sim/scene.h
    engine/sim/ui.h
    engine/sim/ui_layout.h
    engine/sim/ui.c
    engine/sim/detach.h
    engine/sim/detach.c
    engine/sim/loading.h
    engine/sim/loading.c
    engine/sim/env_particles.h
    engine/sim/env_particles.c
)

add_library(engine_sim STATIC ${ENGINE_SIM_SOURCES})
target_link_libraries(engine_sim PUBLIC engine_core engine_voxel engine_physics)
set_property(TARGET engine_sim PROPERTY C_STANDARD 23)
set_property(TARGET engine_sim PROPERTY C_STANDARD_REQUIRED ON)
set_property(TARGET engine_sim PROPERTY C_EXTENSIONS OFF)

if(PATCH_ENABLE_PROFILING)
    target_compile_definitions(engine_sim PRIVATE PATCH_PROFILE)
endif()

# -----------------------------------------------------------------------------
# Static library: content (C only - data descriptors)
# One file per descriptor in content/shapes/ and content/materials/
# -----------------------------------------------------------------------------
file(GLOB CONTENT_SHAPE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/content/shapes/*.c"
)

file(GLOB CONTENT_MATERIAL_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/content/materials/*.c"
)

set(CONTENT_SOURCES
    content/materials.h
    content/materials.c
    content/scenes.h
    content/scenes.c
    content/voxel_shapes.h
    content/voxel_shapes.c
    ${CONTENT_SHAPE_SOURCES}
    ${CONTENT_MATERIAL_SOURCES}
)

add_library(content STATIC ${CONTENT_SOURCES})
target_link_libraries(content PUBLIC engine_core)
set_property(TARGET content PROPERTY C_STANDARD 23)
set_property(TARGET content PROPERTY C_STANDARD_REQUIRED ON)
set_property(TARGET content PROPERTY C_EXTENSIONS OFF)

# -----------------------------------------------------------------------------
# Static library: engine_platform_time (C only - portable high-resolution timer and threads)
# -----------------------------------------------------------------------------
if(WIN32)
    set(ENGINE_PLATFORM_TIME_SOURCES engine/platform/platform.h engine/platform/platform_win32.c
        engine/platform/thread.h engine/platform/thread_win32.c)
else()
    set(ENGINE_PLATFORM_TIME_SOURCES engine/platform/platform.h engine/platform/platform_posix.c
        engine/platform/thread.h engine/platform/thread_posix.c)
endif()

add_library(engine_platform_time STATIC ${ENGINE_PLATFORM_TIME_SOURCES})
target_include_directories(engine_platform_time PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(WIN32)
    # timeBeginPeriod (platform_precise_waits_begin)
    target_link_libraries(engine_platform_time PUBLIC winmm)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(engine_platform_time PUBLIC Threads::Threads)
endif()
set_property(TARGET engine_platform_time PROPERTY C_STANDARD 23)
set_property(TARGET engine_platform_time PROPERTY C_STANDARD_REQUIRED ON)
set_property(TARGET engine_platform_time PROPERTY C_EXTENSIONS OFF)

# -----------------------------------------------------------------------------
# Static library: game (C only)
# -----------------------------------------------------------------------------
set(GAME_C_SOURCES
    game/biome.h
    game/biome.c
    game/terrain_gen.h
    game/terrain_gen.c
    game/tree_gen.h
    game/tree_gen.c
    game/scatter_gen.h
    game/scatter_gen.c
    game/shrub_gen.h
    game/shrub_gen.c
    game/day_night.h
    game/day_night.c
    game/weather.h
    game/weather.c
    game/world_scene.h
    game/world_scene_internal.h
    game/world_scene.c
    game/destruction_script.h
    game/destruction_script.c
)

add_library(game STATIC ${GAME_C_SOURCES})
target_link_libraries(game PUBLIC
    engine_core
    engine_physics
    engine_voxel
    engine_sim
    content
)
set_property(TARGET game PROPERTY C_STANDARD 23)
set_property(TARGET game PROPERTY C_STANDARD_REQUIRED ON)
set_property(TARGET game PROPERTY C_EXTENSIONS OFF)

if(PATCH_ENABLE_PROFILING)
    target_compile_definitions(game PRIVATE PATCH_PROFILE)
endif()

# -----------------------------------------------------------------------------
# Vulkan app: window, renderer, executable (PATCH_BUILD_APP, Windows only)
# -----------------------------------------------------------------------------
if(PATCH_BUILD_APP)
    # engine_platform: Win32 window and entry point
    set(ENGINE_PLATFORM_SOURCES
        engine/platform/window.h
        engine/platform/window.cpp
        engine/platform/win32_entry.cpp
    )

    add_library(engine_platform STATIC ${ENGINE_PLATFORM_SOURCES})
    target_link_libraries(engine_platform PUBLIC engine_core engine_platform_time ${Vulkan_LIBRARIES})
    target_include_directories(engine_platform SYSTEM PUBLIC ${Vulkan_INCLUDE_DIRS})
    set_property(TARGET engine_platform PROPERTY CXX_STANDARD 23)
    set_property(TARGET engine_platform PROPERTY CXX_STANDARD_REQUIRED ON)
    set_property(TARGET engine_platform PROPERTY CXX_EXTENSIONS OFF)

    patch_disable_rtti_exceptions(engine_platform)

    # engine_render (C++ only)
    set(ENGINE_RENDER_SOURCES
        engine/render/renderer.h
        engine/render/renderer.cpp
        engine/render/vk_barriers.h
        engine/render/vk_barriers.cpp
        engine/render/vk_context.h
        engine/render/vk_context.cpp
        engine/render/vk_pipeline.h
        engine/render/vk_pipeline.cpp
        engine/render/vk_swapchain.h
        engine/render/vk_swapchain.cpp
        engine/render/frame_resources.h
        engine/render/frame_resources.cpp
        engine/render/gpu_timer.h
        engine/render/gpu_timer.cpp
        engine/render/render_targets.h
        engine/render/render_targets.cpp
        engine/render/terrain_volume.h
        engine/render/terrain_volume.cpp
        engine/render/gbuffer_pass.h
        engine/render/gbuffer_pass.cpp
        engine/render/shadow_ao_pass.h
        engine/render/shadow_ao_pass.cpp
        engine/render/lighting_pass.h
        engine/render/lighting_pass.cpp
        engine/render/post_pass.h
        engine/render/post_pass.cpp
        engine/render/voxel_objects.h
        engine/render/voxel_objects.cpp
        engine/render/voxel_bricks.h
        engine/render/voxel_bricks.cpp
        engine/render/particle_pass.h
        engine/render/particle_pass.cpp
        engine/render/ui_pass.h
        engine/render/ui_pass.cpp
        engine/render/ui_font.h
        engine/render/ui_font.cpp
        engine/render/ui_renderer.h
        engine/render/ui_renderer.cpp
        engine/render/gpu_memory.h
        engine/render/gpu_memory.cpp
        engine/render/gpu_bvh.h
        engine/render/gpu_volume.h
        engine/render/voxel_push_constants.h
        engine/render/raster_gpu_types.h
    )

    add_library(engine_render STATIC ${ENGINE_RENDER_SOURCES})
    target_link_libraries(engine_render PUBLIC
        engine_core
        engine_physics
        engine_voxel
        engine_sim
        engine_platform
        ${Vulkan_LIBRARIES}
    )
    # Third-party headers: their warnings are not the project's (nor errors under PATCH_WARNINGS_AS_ERRORS)
    target_include_directories(engine_render SYSTEM PUBLIC ${Vulkan_INCLUDE_DIRS})
    target_include_directories(engine_render SYSTEM PUBLIC ${CMAKE_SOURCE_DIR}/third_party/vma/include)
    set_property(TARGET engine_render PROPERTY CXX_STANDARD 23)
    set_property(TARGET engine_render PROPERTY CXX_STANDARD_REQUIRED ON)
    set_property(TARGET engine_render PROPERTY CXX_EXTENSIONS OFF)

    patch_disable_rtti_exceptions(engine_render)

    if(PATCH_ENABLE_PROFILING)
        target_compile_definitions(engine_render PRIVATE PATCH_PROFILE)
    endif()

    # Executable: patch_samples (C++ only)
    set(APP_SOURCES
        app/main.cpp
        app/app_ui.h
        app/app_ui.cpp
        app/app_debug.h
        app/app_debug.cpp
    )

    add_executable(patch_samples ${APP_SOURCES})
    target_link_libraries(patch_samples PRIVATE
        game
        engine_render
        engine_platform
        engine_sim
        engine_voxel
        engine_physics
        engine_core
    )
    set_property(TARGET patch_samples PROPERTY CXX_STANDARD 23)
    set_property(TARGET patch_samples PROPERTY CXX_STANDARD_REQUIRED ON)
    set_property(TARGET patch_samples PROPERTY CXX_EXTENSIONS OFF)

    patch_disable_rtti_exceptions(patch_samples)

    if(PATCH_ENABLE_PROFILING)
        target_compile_definitions(patch_samples PRIVATE PATCH_PROFILE)
    endif()

    # Windows-specific
    # Release: GUI subsystem (no console), Debug: console subsystem (for printf/stdout).
    # A generator expression, so multi-config generators get it per configuration.
    set_target_properties(patch_samples PROPERTIES
        WIN32_EXECUTABLE "$<CONFIG:Release,RelWithDebInfo,MinSizeRel>")

    set(VULKAN_LOADER_DLL "$ENV{VULKAN_SDK}/Bin/vulkan-1.dll")
    if(EXISTS "${VULKAN_LOADER_DLL}")
        add_custom_command(TARGET patch_samples POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${VULKAN_LOADER_DLL}" $<TARGET_FILE_DIR:patch_samples>
        )
    endif()
endif()

# -----------------------------------------------------------------------------
# Shaders (compile to SPIR-V and embed as C arrays)
# Single source of truth: all shaders in shaders/ are compiled AND embedded.
# Option: PATCH_USE_PREBUILT_SHADERS skips compilation and uses checked-in header.
# -----------------------------------------------------------------------------

if(PATCH_USE_PREBUILT_SHADERS OR NOT PATCH_BUILD_APP)
    # Use prebuilt shader header - no Vulkan SDK glslc required (test_gpu_layout checks it)
    message(STATUS "Using prebuilt shader header")
    set(PREBUILT_SHADER_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/engine/render/shaders_embedded_prebuilt.h")
    if(NOT EXISTS "${PREBUILT_SHADER_HEADER}")
        message(FATAL_ERROR
            "Prebuilt shader header not found: ${PREBUILT_SHADER_HEADER}\n"
            "Build with PATCH_USE_PREBUILT_SHADERS=OFF first to generate it, then copy:\n"
            "  cp build/generated/shaders_embedded.h engine/render/shaders_embedded_prebuilt.h")
    endif()

    # Copy prebuilt header to build directory with expected name
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/generated/shaders_embedded.h
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/generated
        COMMAND ${CMAKE_COMMAND} -E copy ${PREBUILT_SHADER_HEADER} ${CMAKE_BINARY_DIR}/generated/shaders_embedded.h
        DEPENDS ${PREBUILT_SHADER_HEADER}
        COMMENT "Using prebuilt shader header"
        VERBATIM
    )
    add_custom_target(Shaders DEPENDS ${CMAKE_BINARY_DIR}/generated/shaders_embedded.h)
else()
    # Verify glslc is available (fail early with clear message)
    if(NOT TARGET Vulkan::glslc)
        message(FATAL_ERROR
            "Vulkan::glslc not found. Install the Vulkan SDK and ensure VULKAN_SDK is set.\n"
            "Download: https://vulkan.lunarg.com/sdk/home\n"
            "Alternatively, use -DPATCH_USE_PREBUILT_SHADERS=ON to skip shader compilation.")
    endif()

    file(GLOB SHADER_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.vert"
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.frag"
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.comp"
    )

    # Shader include directory for shared GLSL modules
    set(SHADER_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders/include")
    file(GLOB SHADER_INCLUDES CONFIGURE_DEPENDS
        "${SHADER_INCLUDE_DIR}/*.glsl"
    )

    # Compile all shaders to SPIR-V and collect for embedding (single source of truth).
    # Only the listed outputs are embedded, so .spv files of removed shaders do no harm.
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/shaders)
    set(SPIRV_BINARY_FILES "")
    foreach(SHADER ${SHADER_SOURCES})
        get_filename_component(SHADER_NAME ${SHADER} NAME)
        set(SPV_OUTPUT "${CMAKE_BINARY_DIR}/shaders/${SHADER_NAME}.spv")
        add_custom_command(
            OUTPUT ${SPV_OUTPUT}
            COMMAND Vulkan::glslc --target-env=vulkan1.3 -I${SHADER_INCLUDE_DIR} ${SHADER} -o ${SPV_OUTPUT}
            DEPENDS ${SHADER} ${SHADER_INCLUDES}
            COMMENT "Compiling ${SHADER_NAME}"
            VERBATIM
        )
        list(APPEND SPIRV_BINARY_FILES ${SPV_OUTPUT})
    endforeach()

    # Embed ALL compiled shaders (no manual list - what compiles gets embedded)
    set(EMBEDDED_SHADER_HEADER "${CMAKE_BINARY_DIR}/generated/shaders_embedded.h")
    add_custom_command(
        OUTPUT ${EMBEDDED_SHADER_HEADER}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/generated
        COMMAND ${CMAKE_COMMAND}
            -DOUTPUT=${EMBEDDED_SHADER_HEADER}
            "-DSHADERS=${SPIRV_BINARY_FILES}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_spirv.cmake
        DEPENDS ${SPIRV_BINARY_FILES} ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_spirv.cmake
        COMMENT "Embedding SPIR-V into shaders_embedded.h"
        VERBATIM
    )

    add_custom_target(Shaders DEPENDS ${SPIRV_BINARY_FILES} ${EMBEDDED_SHADER_HEADER})

    # Auto-update prebuilt shader header after successful compilation
    set(PREBUILT_SHADER_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/engine/render/shaders_embedded_prebuilt.h")
    add_custom_command(TARGET Shaders POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${EMBEDDED_SHADER_HEADER}
            ${PREBUILT_SHADER_HEADER}
        COMMENT "Updating prebuilt shader header"
        VERBATIM
    )
endif()

if(PATCH_BUILD_APP)
    add_dependencies(engine_render Shaders)
    target_include_directories(engine_render PRIVATE ${CMAKE_BINARY_DIR}/generated)

    # Track generated shader header as a source so ninja rebuilds properly in one pass
    set_source_files_properties(
        ${CMAKE_BINARY_DIR}/generated/shaders_embedded.h
        PROPERTIES GENERATED TRUE
    )
    target_sources(engine_render PRIVATE ${CMAKE_BINARY_DIR}/generated/shaders_embedded.h)
endif()

# -----------------------------------------------------------------------------
# Warnings
# -----------------------------------------------------------------------------
set(PATCH_ALL_TARGETS engine_core engine_physics engine_voxel engine_sim content engine_platform_time game)
if(PATCH_BUILD_APP)
    list(APPEND PATCH_ALL_TARGETS engine_platform engine_render patch_samples)
endif()
foreach(tgt ${PATCH_ALL_TARGETS})
    patch_enable_warnings(${tgt})
endforeach()

# -----------------------------------------------------------------------------
# IDE helpers
# -----------------------------------------------------------------------------
if(PATCH_BUILD_APP)
    set_target_properties(patch_samples PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    )
endif()

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
    ${ENGINE_CORE_SOURCES}
    ${ENGINE_PHYSICS_SOURCES}
    ${ENGINE_VOXEL_SOURCES}
    ${ENGINE_SIM_SOURCES}
    ${ENGINE_PLATFORM_TIME_SOURCES}
    ${ENGINE_PLATFORM_SOURCES}
    ${ENGINE_RENDER_SOURCES}
    ${GAME_C_SOURCES}
    ${APP_SOURCES}
)

# -----------------------------------------------------------------------------
# Tests (C only, integrated with CTest)
# -----------------------------------------------------------------------------
enable_testing()

add_executable(test_core tests/test_core.c)
target_link_libraries(test_core PRIVATE engine_core)
set_property(TARGET test_core PROPERTY C_STANDARD 23)
add_test(NAME core COMMAND test_core)

add_executable(test_jobs tests/test_jobs.c)
target_link_libraries(test_jobs PRIVATE engine_core engine_sim engine_platform_time)
set_property(TARGET test_jobs PROPERTY C_STANDARD 23)
add_test(NAME jobs COMMAND test_jobs)

add_executable(test_voxel tests/test_voxel.c)
target_link_libraries(test_voxel PRIVATE engine_voxel content engine_platform_time)
set_property(TARGET test_voxel PROPERTY C_STANDARD 23)
add_test(NAME voxel COMMAND test_voxel)

add_executable(test_content tests/test_content.c)
target_link_libraries(test_content PRIVATE content engine_core)
set_property(TARGET test_content PROPERTY C_STANDARD 23)
add_test(NAME content COMMAND test_content)

add_executable(test_connectivity tests/test_connectivity.c)
target_link_libraries(test_connectivity PRIVATE engine_voxel content engine_platform_time)
set_property(TARGET test_connectivity PROPERTY C_STANDARD 23)
add_test(NAME connectivity COMMAND test_connectivity)

add_executable(test_terrain_detach tests/test_terrain_detach.c)
target_link_libraries(test_terrain_detach PRIVATE engine_sim engine_physics engine_voxel content engine_platform_time)
set_property(TARGET test_terrain_detach PROPERTY C_STANDARD 23)
add_test(NAME terrain_detach COMMAND test_terrain_detach)

add_executable(test_physics tests/test_physics.c)
target_link_libraries(test_physics PRIVATE engine_physics engine_sim engine_voxel content engine_platform_time)
target_compile_definitions(test_physics PRIVATE PATCH_PROFILE)
set_property(TARGET test_physics PROPERTY C_STANDARD 23)
add_test(NAME physics COMMAND test_physics)

add_executable(test_scenes tests/test_scenes.c)
target_link_libraries(test_scenes PRIVATE game engine_sim engine_physics engine_voxel engine_platform_time content)
set_property(TARGET test_scenes PROPERTY C_STANDARD 23)
add_test(NAME scenes COMMAND test_scenes)

add_executable(test_worldgen tests/test_worldgen.c)
target_link_libraries(test_worldgen PRIVATE game engine_sim engine_physics engine_voxel engine_platform_time content)
set_property(TARGET test_worldgen PROPERTY C_STANDARD 23)
add_test(NAME worldgen COMMAND test_worldgen)

add_executable(test_gpu_layout tests/test_gpu_layout.cpp)
target_link_libraries(test_gpu_layout PRIVATE engine_voxel engine_core)
target_include_directories(test_gpu_layout PRIVATE ${CMAKE_BINARY_DIR}/generated)
# Reads GLSL struct declarations to check them against the C structs
target_compile_definitions(test_gpu_layout PRIVATE PATCH_SHADER_DIR="${CMAKE_SOURCE_DIR}/shaders")
add_dependencies(test_gpu_layout Shaders)
set_property(TARGET test_gpu_layout PROPERTY CXX_STANDARD 23)
set_property(TARGET test_gpu_layout PROPERTY CXX_STANDARD_REQUIRED ON)
add_test(NAME gpu_layout COMMAND test_gpu_layout)

# Voxel object brick atlas bookkeeping (Vulkan-free renderer code, no GPU needed)
add_executable(test_voxel_bricks tests/test_voxel_bricks.cpp engine/render/voxel_bricks.h engine/render/voxel_bricks.cpp)
target_link_libraries(test_voxel_bricks PRIVATE engine_sim engine_physics engine_voxel content engine_platform_time)
set_property(TARGET test_voxel_bricks PROPERTY CXX_STANDARD 23)
set_property(TARGET test_voxel_bricks PROPERTY CXX_STANDARD_REQUIRED ON)
add_test(NAME voxel_bricks COMMAND test_voxel_bricks)

# The profiler tests read samples the engine records, so they need a profiling build
set(PATCH_PROFILE_TESTS)
if(PATCH_ENABLE_PROFILING)
    foreach(name profile profile_validation profile_stress)
        add_executable(test_${name} tests/test_${name}.c)
        target_link_libraries(test_${name} PRIVATE engine_voxel engine_platform_time content)
        target_compile_definitions(test_${name} PRIVATE PATCH_PROFILE)
        set_property(TARGET test_${name} PROPERTY C_STANDARD 23)
        add_test(NAME ${name} COMMAND test_${name})
        list(APPEND PATCH_PROFILE_TESTS ${name})
    endforeach()
endif()

add_executable(test_stress tests/test_stress.c)
target_link_libraries(test_stress PRIVATE engine_sim engine_physics engine_voxel engine_platform_time content)
set_property(TARGET test_stress PROPERTY C_STANDARD 23)
add_test(NAME stress COMMAND test_stress)

# Mark all unit tests as setting up the "unit_tests" fixture
set(UNIT_TESTS core jobs voxel content connectivity terrain_detach physics scenes worldgen gpu_layout voxel_bricks stress ${PATCH_PROFILE_TESTS})
set_tests_properties(${UNIT_TESTS} PROPERTIES FIXTURES_SETUP unit_tests)

# -----------------------------------------------------------------------------
# App tests, sanitizer setup for tests, optional post-build test run
# -----------------------------------------------------------------------------
if(PATCH_BUILD_APP)
    # Barrier derivation of the synchronization2 helper (no GPU needed)
    add_executable(test_vk_barriers tests/test_vk_barriers.cpp engine/render/vk_barriers.h engine/render/vk_barriers.cpp)
    target_include_directories(test_vk_barriers PRIVATE ${CMAKE_SOURCE_DIR} ${Vulkan_INCLUDE_DIRS})
    target_link_libraries(test_vk_barriers PRIVATE ${Vulkan_LIBRARIES})
    set_property(TARGET test_vk_barriers PROPERTY CXX_STANDARD 23)
    set_property(TARGET test_vk_barriers PROPERTY CXX_STANDARD_REQUIRED ON)
    add_test(NAME vk_barriers COMMAND test_vk_barriers)
    set_tests_properties(vk_barriers PROPERTIES FIXTURES_SETUP unit_tests)

    add_executable(test_launch tests/test_launch.cpp)
    set_property(TARGET test_launch PROPERTY CXX_STANDARD 23)
    set_property(TARGET test_launch PROPERTY CXX_STANDARD_REQUIRED ON)

    add_test(NAME launch COMMAND test_launch $<TARGET_FILE:patch_samples>)
    set_tests_properties(launch PROPERTIES FIXTURES_REQUIRED unit_tests)

    # Render performance test (runs app and validates profiling data)
    add_executable(test_render_perf tests/test_render_perf.cpp)
    set_property(TARGET test_render_perf PROPERTY CXX_STANDARD 23)
    set_property(TARGET test_render_perf PROPERTY CXX_STANDARD_REQUIRED ON)

    add_test(NAME render_perf COMMAND test_render_perf $<TARGET_FILE:patch_samples>)
    set_tests_properties(render_perf PROPERTIES FIXTURES_REQUIRED unit_tests)
endif()

set(PATCH_TEST_EXECUTABLES
    test_core test_jobs test_voxel test_content test_connectivity test_terrain_detach
    test_physics test_scenes test_worldgen test_gpu_layout test_voxel_bricks test_stress)
foreach(name ${PATCH_PROFILE_TESTS})
    list(APPEND PATCH_TEST_EXECUTABLES test_${name})
endforeach()
if(PATCH_BUILD_APP)
    list(APPEND PATCH_TEST_EXECUTABLES test_vk_barriers test_launch test_render_perf)
endif()
foreach(tgt ${PATCH_TEST_EXECUTABLES})
    patch_enable_warnings(${tgt})
endforeach()

# Opt-in: run all unit tests after every app build and write build_report.txt
if(PATCH_BUILD_APP AND PATCH_RUN_TESTS_ON_BUILD)
    # Ensure all test executables are built before running tests
    add_dependencies(patch_samples ${PATCH_TEST_EXECUTABLES})

    add_custom_command(TARGET patch_samples POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DBUILD_DIR=${CMAKE_BINARY_DIR}
            -DEXE_PATH=$<TARGET_FILE:patch_samples>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/run_tests.cmake
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running tests and generating build_report.txt..."
    )
endif()

# -----------------------------------------------------------------------------
# Tool: voxelize (OBJ mesh to C voxel shape converter)
# -----------------------------------------------------------------------------
add_executable(voxelize tools/voxelize.cpp)
# Header-only use of engine and content constants (VOBJ_GRID_SIZE, MAT_STONE)
target_include_directories(voxelize PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_property(TARGET voxelize PROPERTY CXX_STANDARD 23)
set_property(TARGET voxelize PROPERTY CXX_STANDARD_REQUIRED ON)
set_property(TARGET voxelize PROPERTY CXX_EXTENSIONS OFF)

patch_enable_warnings(voxelize)
if(MSVC)
    target_compile_options(voxelize PRIVATE /EHsc)
endif()

# -----------------------------------------------------------------------------
# Clang-Tidy static analysis
# -----------------------------------------------------------------------------
if(PATCH_ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy clang-tidy-18 clang-tidy-17
        HINTS "C:/Program Files/LLVM/bin" "$ENV{LLVM_PATH}/bin")
    if(CLANG_TIDY_EXE)
        file(GLOB_RECURSE TIDY_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_SOURCE_DIR}/engine/*.c"
            "${CMAKE_SOURCE_DIR}/engine/*.cpp"
            "${CMAKE_SOURCE_DIR}/app/*.cpp"
            "${CMAKE_SOURCE_DIR}/game/*.c"
            "${CMAKE_SOURCE_DIR}/content/*.c"
            "${CMAKE_SOURCE_DIR}/tools/*.cpp"
        )

        # Only sources in this configuration are in compile_commands.json
        if(WIN32)
            list(FILTER TIDY_SOURCES EXCLUDE REGEX "/engine/platform/[a-z]+_posix\\.c$")
        else()
            list(FILTER TIDY_SOURCES EXCLUDE REGEX "/engine/platform/[a-z]+_win32\\.c$")
        endif()
        if(NOT PATCH_BUILD_APP)
            list(FILTER TIDY_SOURCES EXCLUDE REGEX "/app/|/engine/render/|window\\.cpp$|win32_entry\\.cpp$")
        endif()

        add_custom_target(clang-tidy
            COMMAND ${CLANG_TIDY_EXE}
                -p ${CMAKE_BINARY_DIR}
                --config-file=${CMAKE_SOURCE_DIR}/.clang-tidy
                ${TIDY_SOURCES}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Running clang-tidy static analysis..."
        )

        add_custom_target(clang-tidy-fix
            COMMAND ${CLANG_TIDY_EXE}
                -p ${CMAKE_BINARY_DIR}
                --config-file=${CMAKE_SOURCE_DIR}/.clang-tidy
                --fix
                ${TIDY_SOURCES}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Running clang-tidy with automatic fixes..."
        )
    else()
        message(WARNING "clang-tidy not found")
    endif()
endif()
