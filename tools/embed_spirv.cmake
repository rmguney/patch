# Convert SPIR-V binary files to C++ header with embedded uint32_t arrays
# Usage: cmake -DOUTPUT=out.h -DSHADERS="a.spv;b.spv" -P embed_spirv.cmake

if(NOT DEFINED OUTPUT OR NOT DEFINED SHADERS)
    message(FATAL_ERROR "Usage: cmake -DOUTPUT=<output.h> -DSHADERS=<shader1.spv;shader2.spv> -P embed_spirv.cmake")
endif()

set(HEADER_CONTENT "/* Auto-generated embedded SPIR-V shaders - do not edit */
#ifndef PATCH_SHADERS_EMBEDDED_H
#define PATCH_SHADERS_EMBEDDED_H

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

    set(POS 0)
    set(WORD_COUNT 0)
    while(POS LESS HEX_LEN)
        string(SUBSTRING "${SPV_HEX}" ${POS} 8 WORD_HEX)
        string(SUBSTRING "${WORD_HEX}" 0 2 B0)
        string(SUBSTRING "${WORD_HEX}" 2 2 B1)
        string(SUBSTRING "${WORD_HEX}" 4 2 B2)
        string(SUBSTRING "${WORD_HEX}" 6 2 B3)
        set(WORD_LE "0x${B3}${B2}${B1}${B0}")

        math(EXPR COL "${WORD_COUNT} % 8")
        if(COL EQUAL 0)
            string(APPEND HEADER_CONTENT "    ")
        endif()
        string(APPEND HEADER_CONTENT "${WORD_LE},")
        math(EXPR COL_NEXT "(${WORD_COUNT} + 1) % 8")
        if(COL_NEXT EQUAL 0)
            string(APPEND HEADER_CONTENT "\n")
        else()
            string(APPEND HEADER_CONTENT " ")
        endif()

        math(EXPR POS "${POS} + 8")
        math(EXPR WORD_COUNT "${WORD_COUNT} + 1")
    endwhile()

    math(EXPR COL "${WORD_COUNT} % 8")
    if(NOT COL EQUAL 0)
        string(APPEND HEADER_CONTENT "\n")
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
