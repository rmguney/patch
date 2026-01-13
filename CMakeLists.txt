cmake_minimum_required(VERSION 3.21)
project(patch VERSION 1.0.0 LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(PATCH_ENABLE_WARNINGS "Enable compiler warnings" ON)
option(PATCH_ENABLE_ASAN "Enable address sanitizer" OFF)
option(PATCH_USE_PREBUILT_SHADERS "Use prebuilt shader header (no Vulkan SDK required for build)" OFF)
option(PATCH_ENABLE_PROFILING "Enable CPU/GPU profiling (F3 toggle in app)" ON)

# Optimization flags for Release builds
if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    if(MSVC)
        add_compile_options(/O2 /Ob2 /DNDEBUG)
    else()
        add_compile_options(-O3 -DNDEBUG)
    endif()
endif()

# Kill patch_samples.exe if running (allows rebuild while app accidentally left open)
if(WIN32)
    execute_process(
        COMMAND taskkill /F /IM patch_samples.exe
        OUTPUT_QUIET ERROR_QUIET
    )
endif()

find_package(Vulkan REQUIRED)

# -----------------------------------------------------------------------------
# Static library: engine_core (C only)
# -----------------------------------------------------------------------------
set(ENGINE_CORE_SOURCES
    engine/core/types.h
    engine/core/math.h
    engine/core/rng.h
    engine/core/spatial_hash.h
    engine/core/spatial_hash.c
    engine/core/arena.h
    engine/core/profile.h
)

add_library(engine_core STATIC ${ENGINE_CORE_SOURCES})
target_include_directories(engine_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_property(TARGET engine_core PROPERTY C_STANDARD 11)
set_property(TARGET engine_core PROPERTY C_STANDARD_REQUIRED ON)
set_property(TARGET engine_core PROPERTY C_EXTENSIONS OFF)

# -----------------------------------------------------------------------------
# Static library: engine_voxel (C only)
# -----------------------------------------------------------------------------
set(ENGINE_VOXEL_SOURCES
    engine/voxel/chunk.h
    engine/voxel/chunk.c
    engine/voxel/volume.h
    engine/voxel/volume.c
    engine/voxel/connectivity.h
    engine/voxel/connectivity.c
    engine/voxel/voxel_object.h
    engine/voxel/voxel_object.c
)

add_library(engine_voxel STATIC ${ENGINE_VOXEL_SOURCES})
target_link_libraries(engine_voxel PUBLIC engine_core)
set_property(TARGET engine_voxel PROPERTY C_STANDARD 11)
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
)

add_library(engine_physics STATIC ${ENGINE_PHYSICS_SOURCES})
target_link_libraries(engine_physics PUBLIC engine_core engine_voxel engine_sim)
set_property(TARGET engine_physics PROPERTY C_STANDARD 11)
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
    engine/sim/entity.h
    engine/sim/lighting.h
    engine/sim/ui.h
    engine/sim/ui.c
    engine/sim/detach.h
    engine/sim/detach.c
)

add_library(engine_sim STATIC ${ENGINE_SIM_SOURCES})
target_link_libraries(engine_sim PUBLIC engine_core engine_voxel)
set_property(TARGET engine_sim PROPERTY C_STANDARD 11)
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
set_property(TARGET content PROPERTY C_STANDARD 11)
set_property(TARGET content PROPERTY C_STANDARD_REQUIRED ON)
set_property(TARGET content PROPERTY C_EXTENSIONS OFF)

# -----------------------------------------------------------------------------
# Static library: engine_platform (C++ with C time API)
# -----------------------------------------------------------------------------
set(ENGINE_PLATFORM_SOURCES
    engine/platform/platform.h
    engine/platform/platform.c
    engine/platform/window.h
    engine/platform/window.cpp
    engine/platform/win32_entry.cpp
)

add_library(engine_platform STATIC ${ENGINE_PLATFORM_SOURCES})
target_link_libraries(engine_platform PUBLIC engine_core ${Vulkan_LIBRARIES})
target_include_directories(engine_platform PUBLIC ${Vulkan_INCLUDE_DIRS})
set_property(TARGET engine_platform PROPERTY C_STANDARD 11)
set_property(TARGET engine_platform PROPERTY C_STANDARD_REQUIRED ON)
set_property(TARGET engine_platform PROPERTY C_EXTENSIONS OFF)
set_property(TARGET engine_platform PROPERTY CXX_STANDARD 20)
set_property(TARGET engine_platform PROPERTY CXX_STANDARD_REQUIRED ON)
set_property(TARGET engine_platform PROPERTY CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(engine_platform PRIVATE /GR- /EHs-c-)
    target_compile_definitions(engine_platform PRIVATE _HAS_EXCEPTIONS=0)
else()
    target_compile_options(engine_platform PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>)
endif()

if(PATCH_ENABLE_PROFILING)
    target_compile_definitions(engine_platform PRIVATE PATCH_PROFILE)
endif()

# -----------------------------------------------------------------------------
# Static library: engine_render (C++ only)
# -----------------------------------------------------------------------------
set(ENGINE_RENDER_SOURCES
    engine/render/renderer.h
    engine/render/renderer_internal.h
    engine/render/renderer.cpp
    engine/render/renderer_resources.cpp
    engine/render/renderer_draw.cpp
    engine/render/renderer_vulkan.cpp
    engine/render/renderer_voxel.cpp
    engine/render/renderer_gbuffer.cpp
    engine/render/renderer_raymarch.cpp
    engine/render/renderer_voxel_objects.cpp
    engine/render/renderer_particles.cpp
    engine/render/ui_font.h
    engine/render/ui_font.cpp
    engine/render/ui_renderer.h
    engine/render/ui_renderer.cpp
    engine/render/gpu_buffer.h
    engine/render/gpu_chunk.h
    engine/render/gpu_volume.h
    engine/render/voxel_push_constants.h
    engine/render/draw_list.h
)

add_library(engine_render STATIC ${ENGINE_RENDER_SOURCES})
target_link_libraries(engine_render PUBLIC
    engine_core
    engine_physics
    engine_voxel
    engine_platform
    ${Vulkan_LIBRARIES}
)
target_include_directories(engine_render PUBLIC ${Vulkan_INCLUDE_DIRS})
set_property(TARGET engine_render PROPERTY CXX_STANDARD 20)
set_property(TARGET engine_render PROPERTY CXX_STANDARD_REQUIRED ON)
set_property(TARGET engine_render PROPERTY CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(engine_render PRIVATE /GR- /EHs-c-)
    target_compile_definitions(engine_render PRIVATE _HAS_EXCEPTIONS=0)
else()
    target_compile_options(engine_render PRIVATE -fno-exceptions -fno-rtti)
endif()

if(PATCH_ENABLE_PROFILING)
    target_compile_definitions(engine_render PRIVATE PATCH_PROFILE)
endif()

# -----------------------------------------------------------------------------
# Static library: game (C++ for renderers, C for rest)
# -----------------------------------------------------------------------------
set(GAME_C_SOURCES
    game/ball_pit.h
    game/ball_pit.c
)

set(GAME_CXX_SOURCES
    game/ball_pit_renderer.h
    game/ball_pit_renderer.cpp
)

add_library(game STATIC ${GAME_C_SOURCES} ${GAME_CXX_SOURCES})
target_link_libraries(game PUBLIC
    engine_core
    engine_physics
    engine_voxel
    engine_sim
    engine_render
    content
)
set_property(TARGET game PROPERTY C_STANDARD 11)
set_property(TARGET game PROPERTY C_STANDARD_REQUIRED ON)
set_property(TARGET game PROPERTY C_EXTENSIONS OFF)
set_property(TARGET game PROPERTY CXX_STANDARD 20)
set_property(TARGET game PROPERTY CXX_STANDARD_REQUIRED ON)
set_property(TARGET game PROPERTY CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(game PRIVATE /GR- /EHs-c-)
    target_compile_definitions(game PRIVATE _HAS_EXCEPTIONS=0)
else()
    target_compile_options(game PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>)
endif()

if(PATCH_ENABLE_PROFILING)
    target_compile_definitions(game PRIVATE PATCH_PROFILE)
endif()

# -----------------------------------------------------------------------------
# Executable: patch_app (C++ only)
# -----------------------------------------------------------------------------
set(APP_SOURCES
    app/main.cpp
    app/app_ui.h
    app/app_ui.cpp
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
set_property(TARGET patch_samples PROPERTY CXX_STANDARD 20)
set_property(TARGET patch_samples PROPERTY CXX_STANDARD_REQUIRED ON)
set_property(TARGET patch_samples PROPERTY CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(patch_samples PRIVATE /GR- /EHs-c-)
    target_compile_definitions(patch_samples PRIVATE _HAS_EXCEPTIONS=0)
else()
    target_compile_options(patch_samples PRIVATE -fno-exceptions -fno-rtti)
endif()

if(PATCH_ENABLE_PROFILING)
    target_compile_definitions(patch_samples PRIVATE PATCH_PROFILE)
endif()

# -----------------------------------------------------------------------------
# Warnings
# -----------------------------------------------------------------------------
set(PATCH_ALL_TARGETS engine_physics engine_voxel engine_sim content engine_platform engine_render game patch_samples)
if(PATCH_ENABLE_WARNINGS)
    foreach(tgt ${PATCH_ALL_TARGETS})
        if(MSVC)
            target_compile_options(${tgt} PRIVATE /W4 /permissive-)
        else()
            target_compile_options(${tgt} PRIVATE -Wall -Wextra -Wpedantic)
        endif()
    endforeach()
endif()

# -----------------------------------------------------------------------------
# Address sanitizer
# -----------------------------------------------------------------------------
if(PATCH_ENABLE_ASAN AND NOT MSVC)
    foreach(tgt ${PATCH_ALL_TARGETS})
        target_compile_options(${tgt} PRIVATE -fsanitize=address -fno-omit-frame-pointer)
        target_link_options(${tgt} PRIVATE -fsanitize=address)
    endforeach()
endif()

# -----------------------------------------------------------------------------
# Windows-specific
# -----------------------------------------------------------------------------
if(WIN32)
    # Release: GUI subsystem (no console), Debug: console subsystem (for printf/stdout)
    if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        set_target_properties(patch_samples PROPERTIES WIN32_EXECUTABLE TRUE)
    else()
        set_target_properties(patch_samples PROPERTIES WIN32_EXECUTABLE FALSE)
    endif()

    set(WIN32_TARGETS engine_platform engine_render game patch_samples)
    foreach(tgt ${WIN32_TARGETS})
        target_compile_definitions(${tgt} PRIVATE
            VK_USE_PLATFORM_WIN32_KHR
            WIN32_LEAN_AND_MEAN
            NOMINMAX
        )
    endforeach()

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

if(PATCH_USE_PREBUILT_SHADERS)
    # Use prebuilt shader header - no Vulkan SDK glslc required
    message(STATUS "Using prebuilt shader header (PATCH_USE_PREBUILT_SHADERS=ON)")
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
    add_dependencies(engine_render Shaders)
    target_include_directories(engine_render PRIVATE ${CMAKE_BINARY_DIR}/generated)
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

    # Clean stale .spv files before compiling (prevents confusion from removed shaders)
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/shaders/.cleaned
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${CMAKE_BINARY_DIR}/shaders
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/shaders
        COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_BINARY_DIR}/shaders/.cleaned
        COMMENT "Cleaning shader output directory"
        VERBATIM
    )

    # Compile all shaders to SPIR-V and collect for embedding (single source of truth)
    set(SPIRV_BINARY_FILES "")
    foreach(SHADER ${SHADER_SOURCES})
        get_filename_component(SHADER_NAME ${SHADER} NAME)
        set(SPV_OUTPUT "${CMAKE_BINARY_DIR}/shaders/${SHADER_NAME}.spv")
        add_custom_command(
            OUTPUT ${SPV_OUTPUT}
            COMMAND Vulkan::glslc -DPATCH_VULKAN=1 --target-env=vulkan1.2 -I${SHADER_INCLUDE_DIR} ${SHADER} -o ${SPV_OUTPUT}
            DEPENDS ${SHADER} ${CMAKE_BINARY_DIR}/shaders/.cleaned ${SHADER_INCLUDES}
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
        COMMENT "Embedding ${CMAKE_CURRENT_LIST_LENGTH} shaders"
        VERBATIM
    )

    add_custom_target(Shaders DEPENDS ${SPIRV_BINARY_FILES} ${EMBEDDED_SHADER_HEADER})
    add_dependencies(engine_render Shaders)
    target_include_directories(engine_render PRIVATE ${CMAKE_BINARY_DIR}/generated)

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

# Make engine_render rebuild when embedded shaders change
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/render/renderer_gbuffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/render/renderer_raymarch.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/render/renderer_particles.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/render/renderer_voxel_objects.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/render/renderer_vulkan.cpp
    PROPERTIES OBJECT_DEPENDS ${CMAKE_BINARY_DIR}/generated/shaders_embedded.h
)

# -----------------------------------------------------------------------------
# IDE helpers
# -----------------------------------------------------------------------------
set_target_properties(patch_samples PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
    ${ENGINE_CORE_SOURCES}
    ${ENGINE_PHYSICS_SOURCES}
    ${ENGINE_VOXEL_SOURCES}
    ${ENGINE_SIM_SOURCES}
    ${ENGINE_PLATFORM_SOURCES}
    ${ENGINE_RENDER_SOURCES}
    ${GAME_C_SOURCES}
    ${GAME_CXX_SOURCES}
    ${APP_SOURCES}
)

# -----------------------------------------------------------------------------
# Tests (C only, integrated with CTest)
# -----------------------------------------------------------------------------
enable_testing()

add_executable(test_core tests/test_core.c)
target_link_libraries(test_core PRIVATE engine_core)
set_property(TARGET test_core PROPERTY C_STANDARD 11)
add_test(NAME core COMMAND test_core)

add_executable(test_voxel tests/test_voxel.c)
target_link_libraries(test_voxel PRIVATE engine_voxel content engine_platform)
set_property(TARGET test_voxel PROPERTY C_STANDARD 11)
add_test(NAME voxel COMMAND test_voxel)

add_executable(test_content tests/test_content.c)
target_link_libraries(test_content PRIVATE content engine_core)
set_property(TARGET test_content PROPERTY C_STANDARD 11)
add_test(NAME content COMMAND test_content)

add_executable(test_connectivity tests/test_connectivity.c)
target_link_libraries(test_connectivity PRIVATE engine_voxel content engine_platform)
set_property(TARGET test_connectivity PROPERTY C_STANDARD 11)
add_test(NAME connectivity COMMAND test_connectivity)

add_executable(test_terrain_detach tests/test_terrain_detach.c)
target_link_libraries(test_terrain_detach PRIVATE engine_sim engine_voxel content engine_platform)
set_property(TARGET test_terrain_detach PROPERTY C_STANDARD 11)
add_test(NAME terrain_detach COMMAND test_terrain_detach)

add_executable(test_physics tests/test_physics.c)
target_link_libraries(test_physics PRIVATE engine_physics engine_sim engine_voxel content engine_platform)
target_compile_definitions(test_physics PRIVATE PATCH_PROFILE)
set_property(TARGET test_physics PROPERTY C_STANDARD 11)
add_test(NAME physics COMMAND test_physics)

add_executable(test_scenes tests/test_scenes.c)
target_link_libraries(test_scenes PRIVATE game engine_sim engine_physics engine_voxel engine_platform content)
set_property(TARGET test_scenes PROPERTY C_STANDARD 11)
add_test(NAME scenes COMMAND test_scenes)

add_executable(test_gpu_layout tests/test_gpu_layout.cpp)
target_link_libraries(test_gpu_layout PRIVATE engine_voxel engine_core)
target_include_directories(test_gpu_layout PRIVATE ${CMAKE_BINARY_DIR}/generated)
set_property(TARGET test_gpu_layout PROPERTY CXX_STANDARD 20)
set_property(TARGET test_gpu_layout PROPERTY CXX_STANDARD_REQUIRED ON)
add_test(NAME gpu_layout COMMAND test_gpu_layout)

add_executable(test_profile tests/test_profile.c)
target_link_libraries(test_profile PRIVATE engine_voxel engine_platform content)
target_compile_definitions(test_profile PRIVATE PATCH_PROFILE)
set_property(TARGET test_profile PROPERTY C_STANDARD 11)
add_test(NAME profile COMMAND test_profile)

add_executable(test_profile_validation tests/test_profile_validation.c)
target_link_libraries(test_profile_validation PRIVATE engine_voxel engine_platform content)
target_compile_definitions(test_profile_validation PRIVATE PATCH_PROFILE)
set_property(TARGET test_profile_validation PROPERTY C_STANDARD 11)
add_test(NAME profile_validation COMMAND test_profile_validation)

add_executable(test_profile_stress tests/test_profile_stress.c)
target_link_libraries(test_profile_stress PRIVATE engine_voxel engine_platform content)
target_compile_definitions(test_profile_stress PRIVATE PATCH_PROFILE)
set_property(TARGET test_profile_stress PROPERTY C_STANDARD 11)
add_test(NAME profile_stress COMMAND test_profile_stress)

add_executable(test_stress tests/test_stress.c)
target_link_libraries(test_stress PRIVATE engine_sim engine_physics engine_voxel engine_platform content)
set_property(TARGET test_stress PROPERTY C_STANDARD 11)
add_test(NAME stress COMMAND test_stress)

# Mark all unit tests as setting up the "unit_tests" fixture
set(UNIT_TESTS core voxel content connectivity terrain_detach physics scenes gpu_layout profile profile_validation profile_stress stress)
set_tests_properties(${UNIT_TESTS} PROPERTIES FIXTURES_SETUP unit_tests)

# -----------------------------------------------------------------------------
# Launch test (runs app executable, must run after all unit tests)
# -----------------------------------------------------------------------------
add_executable(test_launch tests/test_launch.cpp)
set_property(TARGET test_launch PROPERTY CXX_STANDARD 20)
set_property(TARGET test_launch PROPERTY CXX_STANDARD_REQUIRED ON)
if(MSVC)
    target_compile_options(test_launch PRIVATE /W4)
else()
    target_compile_options(test_launch PRIVATE -Wall -Wextra)
endif()

add_test(NAME launch COMMAND test_launch $<TARGET_FILE:patch_samples>)
set_tests_properties(launch PROPERTIES FIXTURES_REQUIRED unit_tests)

# Render performance test (runs app and validates profiling data)
add_executable(test_render_perf tests/test_render_perf.cpp)
set_property(TARGET test_render_perf PROPERTY CXX_STANDARD 20)
set_property(TARGET test_render_perf PROPERTY CXX_STANDARD_REQUIRED ON)
if(MSVC)
    target_compile_options(test_render_perf PRIVATE /W4)
else()
    target_compile_options(test_render_perf PRIVATE -Wall -Wextra)
endif()

add_test(NAME render_perf COMMAND test_render_perf $<TARGET_FILE:patch_samples>)
set_tests_properties(render_perf PROPERTIES FIXTURES_REQUIRED unit_tests)

# Run all tests and generate build report
add_custom_command(TARGET patch_samples POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        -DBUILD_DIR=${CMAKE_BINARY_DIR}
        -DEXE_PATH=$<TARGET_FILE:patch_samples>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/run_tests.cmake
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running tests and generating build_report.txt..."
)

# -----------------------------------------------------------------------------
# Tool: voxelize (OBJ mesh to C voxel shape converter)
# -----------------------------------------------------------------------------
add_executable(voxelize tools/voxelize.cpp)
set_property(TARGET voxelize PROPERTY CXX_STANDARD 20)
set_property(TARGET voxelize PROPERTY CXX_STANDARD_REQUIRED ON)
set_property(TARGET voxelize PROPERTY CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(voxelize PRIVATE /W4)
else()
    target_compile_options(voxelize PRIVATE -Wall -Wextra)
endif()
