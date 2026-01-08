# Run all tests and generate build_report.txt
# Usage: cmake -DBUILD_DIR=<build_dir> -DEXE_PATH=<patch_samples.exe> -P run_tests.cmake

cmake_minimum_required(VERSION 3.21)

if(NOT BUILD_DIR)
    message(FATAL_ERROR "BUILD_DIR not specified")
endif()

if(NOT EXE_PATH)
    message(FATAL_ERROR "EXE_PATH not specified")
endif()

set(REPORT_FILE "${BUILD_DIR}/build_report.txt")

string(TIMESTAMP TIMESTAMP "%Y-%m-%d %H:%M:%S")

file(WRITE ${REPORT_FILE} "################################################################################\n")
file(APPEND ${REPORT_FILE} "#                        PATCH ENGINE BUILD REPORT                            #\n")
file(APPEND ${REPORT_FILE} "################################################################################\n")
file(APPEND ${REPORT_FILE} "Generated: ${TIMESTAMP}\n")
file(APPEND ${REPORT_FILE} "Executable: ${EXE_PATH}\n\n")

file(APPEND ${REPORT_FILE} "================================================================================\n")
file(APPEND ${REPORT_FILE} "  UNIT TESTS\n")
file(APPEND ${REPORT_FILE} "================================================================================\n\n")

execute_process(
    COMMAND ${CMAKE_CTEST_COMMAND} -V -E "render_perf|launch"
    WORKING_DIRECTORY ${BUILD_DIR}
    OUTPUT_VARIABLE UNIT_OUTPUT
    ERROR_VARIABLE UNIT_ERROR
    RESULT_VARIABLE UNIT_RESULT
)
file(APPEND ${REPORT_FILE} "${UNIT_OUTPUT}\n")
if(UNIT_ERROR)
    file(APPEND ${REPORT_FILE} "${UNIT_ERROR}\n")
endif()

file(APPEND ${REPORT_FILE} "\n================================================================================\n")
file(APPEND ${REPORT_FILE} "  LAUNCH TESTS\n")
file(APPEND ${REPORT_FILE} "================================================================================\n\n")

execute_process(
    COMMAND "${BUILD_DIR}/test_launch.exe" "${EXE_PATH}"
    WORKING_DIRECTORY ${BUILD_DIR}
    OUTPUT_VARIABLE LAUNCH_OUTPUT
    ERROR_VARIABLE LAUNCH_ERROR
    RESULT_VARIABLE LAUNCH_RESULT
)
file(APPEND ${REPORT_FILE} "${LAUNCH_OUTPUT}\n")
if(LAUNCH_ERROR)
    file(APPEND ${REPORT_FILE} "${LAUNCH_ERROR}\n")
endif()

file(APPEND ${REPORT_FILE} "\n================================================================================\n")
file(APPEND ${REPORT_FILE} "  PERFORMANCE TESTS\n")
file(APPEND ${REPORT_FILE} "================================================================================\n\n")

execute_process(
    COMMAND "${BUILD_DIR}/test_render_perf.exe" "${EXE_PATH}"
    WORKING_DIRECTORY ${BUILD_DIR}
    OUTPUT_VARIABLE PERF_OUTPUT
    ERROR_VARIABLE PERF_ERROR
    RESULT_VARIABLE PERF_RESULT
)
file(APPEND ${REPORT_FILE} "${PERF_OUTPUT}\n")
if(PERF_ERROR)
    file(APPEND ${REPORT_FILE} "${PERF_ERROR}\n")
endif()

file(APPEND ${REPORT_FILE} "\n################################################################################\n")
file(APPEND ${REPORT_FILE} "#                              FINAL SUMMARY                                  #\n")
file(APPEND ${REPORT_FILE} "################################################################################\n")
file(APPEND ${REPORT_FILE} "Unit tests exit code: ${UNIT_RESULT}\n")
file(APPEND ${REPORT_FILE} "Launch tests exit code: ${LAUNCH_RESULT}\n")
file(APPEND ${REPORT_FILE} "Performance tests exit code: ${PERF_RESULT}\n")
file(APPEND ${REPORT_FILE} "Report saved: build_report.txt\n")

message(STATUS "Build report saved to: ${REPORT_FILE}")

if(NOT UNIT_RESULT EQUAL 0 OR NOT LAUNCH_RESULT EQUAL 0 OR NOT PERF_RESULT EQUAL 0)
    message(FATAL_ERROR "Some tests failed")
endif()
