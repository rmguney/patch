#!/bin/bash
# Build wrapper - handles zombie ninja cleanup and MSVC env setup
# Usage: ./build.sh [cmake build args...]
# Examples:
#   ./build.sh                    # normal build (unit tests only)
#   PATCH_RUN_ALL_TESTS=1 ./build.sh  # build + all tests

# Kill zombies - build tools, compilers, linkers, test executables
taskkill //f //im ninja.exe 2>/dev/null
taskkill //f //im cmake.exe 2>/dev/null
taskkill //f //im cl.exe 2>/dev/null
taskkill //f //im link.exe 2>/dev/null
taskkill //f //im patch_samples.exe 2>/dev/null
taskkill //f //im test_scenes.exe 2>/dev/null
taskkill //f //im test_launch.exe 2>/dev/null
taskkill //f //im test_render_perf.exe 2>/dev/null

# Clean stale ninja state if present
[ -f build/.ninja_log.restat ] && rm -f build/.ninja_log.restat

# MSVC environment
export INCLUDE='C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.43.34808\include;C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\ucrt;C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\um;C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\shared'
export LIB='C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.43.34808\lib\x64;C:\Program Files (x86)\Windows Kits\10\Lib\10.0.22621.0\ucrt\x64;C:\Program Files (x86)\Windows Kits\10\Lib\10.0.22621.0\um\x64'
export PATH="/c/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.43.34808/bin/Hostx64/x64:/c/Program Files (x86)/Windows Kits/10/bin/10.0.22621.0/x64:/c/VulkanSDK/1.4.309.0/Bin:$PATH"

cmake --build build "$@" 2>&1
