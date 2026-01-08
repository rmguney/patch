cmake_minimum_required(VERSION 3.21)
project(patch VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(PATCH_ENABLE_WARNINGS "Enable compiler warnings" ON)
option(PATCH_ENABLE_ASAN "Enable address sanitizer" OFF)

find_package(Vulkan REQUIRED)

set(PATCH_CORE
    src/core/types.h
    src/core/math.h
    src/core/scene.h
    src/core/physics.h
    src/core/physics.c
    src/core/particles.h
    src/core/particles.c
    src/core/voxel_physics.h
    src/core/voxel_physics.c
    src/core/voxel_object.h
    src/core/voxel_object.c
    src/core/ui.h
    src/core/ui.c
)

set(PATCH_GAME
    src/game/combat.h
    src/game/combat.c
    src/game/player.h
    src/game/player.c
    src/game/enemy.h
    src/game/enemy.c
    src/game/humanoid.h
    src/game/humanoid.c
)

set(PATCH_ENGINE
    src/engine/window.h
    src/engine/window.cpp
    src/engine/renderer.h
    src/engine/renderer.cpp
    src/engine/ui_font.h
    src/engine/ui_font.cpp
    src/engine/ui_renderer.h
    src/engine/ui_renderer.cpp
)

set(PATCH_SCENES
    src/scenes/ball_pit.h
    src/scenes/ball_pit.c
    src/scenes/ball_pit_renderer.h
    src/scenes/ball_pit_renderer.cpp
    src/scenes/melee.h
    src/scenes/melee.c
    src/scenes/melee_renderer.h
    src/scenes/melee_renderer.cpp
    src/scenes/shooter.h
    src/scenes/shooter.c
    src/scenes/shooter_renderer.h
    src/scenes/shooter_renderer.cpp
)

set(PATCH_APP
    src/app/main.cpp
)

add_executable(${PROJECT_NAME}
    ${PATCH_CORE}
    ${PATCH_GAME}
    ${PATCH_ENGINE}
    ${PATCH_SCENES}
    ${PATCH_APP}
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${Vulkan_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    ${Vulkan_LIBRARIES}
)

if(PATCH_ENABLE_WARNINGS)
    if(MSVC)
        target_compile_options(${PROJECT_NAME} PRIVATE /W4 /permissive- /EHsc)
    else()
        target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

if(PATCH_ENABLE_ASAN AND NOT MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(${PROJECT_NAME} PRIVATE -fsanitize=address)
endif()

if(WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE TRUE)
    target_sources(${PROJECT_NAME} PRIVATE src/app/win32_entry.cpp)

    target_compile_definitions(${PROJECT_NAME} PRIVATE
        VK_USE_PLATFORM_WIN32_KHR
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )

    set(VULKAN_LOADER_DLL "$ENV{VULKAN_SDK}/Bin/vulkan-1.dll")
    if(EXISTS "${VULKAN_LOADER_DLL}")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${VULKAN_LOADER_DLL}" $<TARGET_FILE_DIR:${PROJECT_NAME}>
        )
    endif()
endif()

file(GLOB CONFIGURE_DEPENDS SHADER_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.vert"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.frag"
)

list(APPEND SHADER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadowmap.vert")
list(REMOVE_DUPLICATES SHADER_SOURCES)

set(SPIRV_BINARY_FILES)
foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/shaders/${SHADER_NAME}.spv
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/shaders
        COMMAND Vulkan::glslc -DPATCH_VULKAN=1 --target-env=vulkan1.2 ${SHADER} -o ${CMAKE_BINARY_DIR}/shaders/${SHADER_NAME}.spv
        DEPENDS ${SHADER}
        VERBATIM
    )
    list(APPEND SPIRV_BINARY_FILES ${CMAKE_BINARY_DIR}/shaders/${SHADER_NAME}.spv)
endforeach()

add_custom_target(Shaders DEPENDS ${SPIRV_BINARY_FILES})
add_dependencies(${PROJECT_NAME} Shaders)

set_target_properties(${PROJECT_NAME} PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src FILES ${PATCH_CORE} ${PATCH_ENGINE} ${PATCH_APP})
