# Convert SPIR-V binary files to C++ header with embedded uint32_t arrays
# Usage: cmake -DOUTPUT=out.h -DSHADERS="a.spv;b.spv" -P embed_spirv.cmake

if(NOT DEFINED OUTPUT OR NOT DEFINED SHADERS)
    message(FATAL_ERROR "Usage: cmake -DOUTPUT=<output.h> -DSHADERS=<shader1.spv;shader2.spv> -P embed_spirv.cmake")
endif()

set(HEADER_CONTENT "/* Auto-generated embedded SPIR-V shaders - do not edit */
#ifndef PATCH_SHADERS_EMBEDDED_H
#define PATCH_SHADERS_EMBEDDED_H

#include <cstddef>
#include <cstdint>

namespace patch {
namespace shaders {

")

foreach(SPV_FILE ${SHADERS})
    get_filename_component(SPV_NAME "${SPV_FILE}" NAME)
    string(REPLACE "." "_" C_NAME "${SPV_NAME}")
    set(C_NAME "k_shader_${C_NAME}")

    file(READ "${SPV_FILE}" SPV_HEX HEX)
    string(LENGTH "${SPV_HEX}" HEX_LEN)
    math(EXPR BYTE_COUNT "${HEX_LEN} / 2")

    string(APPEND HEADER_CONTENT "/* ${SPV_NAME} (${BYTE_COUNT} bytes) */\n")
    string(APPEND HEADER_CONTENT "static constexpr uint32_t ${C_NAME}[] = {\n")

    # Whole-string regexes keep this linear in the shader size: little-endian byte
    # quadruples become words, then every 8 words end a line
    string(REGEX REPLACE "(..)(..)(..)(..)" "0x\\4\\3\\2\\1, " WORDS "${SPV_HEX}")
    string(REGEX REPLACE "((0x[0-9a-f]+, )(0x[0-9a-f]+, )(0x[0-9a-f]+, )(0x[0-9a-f]+, )(0x[0-9a-f]+, )(0x[0-9a-f]+, )(0x[0-9a-f]+, )0x[0-9a-f]+), "
           "\\1,\n" WORDS "${WORDS}")
    string(REGEX REPLACE "\n(0x)" "\n    \\1" WORDS "${WORDS}")
    if(NOT WORDS STREQUAL "")
        string(APPEND HEADER_CONTENT "    ${WORDS}")
        if(NOT WORDS MATCHES "\n$")
            string(APPEND HEADER_CONTENT "\n")
        endif()
    endif()

    string(APPEND HEADER_CONTENT "};\n")
    string(APPEND HEADER_CONTENT "static constexpr size_t ${C_NAME}_size = sizeof(${C_NAME});\n\n")
endforeach()

string(APPEND HEADER_CONTENT "} // namespace shaders
} // namespace patch

#endif
")

file(WRITE "${OUTPUT}" "${HEADER_CONTENT}")
list(LENGTH SHADERS SHADER_COUNT)
message(STATUS "Generated ${OUTPUT} with ${SHADER_COUNT} shaders")
