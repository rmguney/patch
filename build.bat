@echo off
setlocal enabledelayedexpansion

set "CLANG_PATH=C:\Program Files\LLVM\bin"
set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"

:: Initialize MSVC environment if not already done
if defined VSCMD_VER goto :vcvars_done
if not exist "%VCVARS%" (
    echo ERROR: Visual Studio not found. Install VS 2022 Build Tools.
    exit /b 1
)
call "%VCVARS%" x64 >nul 2>&1
:vcvars_done

:: Kill zombies - build tools, compilers, linkers, test executables
taskkill /f /im ninja.exe >nul 2>&1
taskkill /f /im cmake.exe >nul 2>&1
taskkill /f /im cl.exe >nul 2>&1
taskkill /f /im link.exe >nul 2>&1
taskkill /f /im patch_samples.exe >nul 2>&1
taskkill /f /im test_scenes.exe >nul 2>&1
taskkill /f /im test_launch.exe >nul 2>&1
taskkill /f /im test_render_perf.exe >nul 2>&1

:: Clean stale ninja state if present
if exist build\.ninja_log.restat del /f build\.ninja_log.restat

set "CMD=%~1"
if "%CMD%"=="" goto :build

if /i "%CMD%"=="help" goto :help
if /i "%CMD%"=="clean" goto :clean
if /i "%CMD%"=="configure" goto :configure
if /i "%CMD%"=="run" goto :run
if /i "%CMD%"=="sanitize" goto :sanitize
if /i "%CMD%"=="tidy" goto :tidy

echo Unknown command: %CMD%
goto :help

:help
echo Usage: build.bat [command]
echo.
echo Commands:
echo   [none]     Configure, build, and run tests
echo   clean      Remove build directory
echo   configure  Configure CMake only
echo   run        Run patch_samples.exe
echo   sanitize   Configure with sanitizers (ASan + UBSan)
echo   tidy       Run clang-tidy static analysis
echo   help       Show this help
echo.
echo Compiler: Clang-cl (LLVM + MSVC environment)
exit /b 0

:clean
echo Cleaning build directory...
if exist build rmdir /s /q build
echo Done.
exit /b 0

:configure
echo Configuring with Clang-cl...
cmake -B build -G Ninja -DCMAKE_C_COMPILER="%CLANG_PATH%\clang-cl.exe" -DCMAKE_CXX_COMPILER="%CLANG_PATH%\clang-cl.exe"
exit /b !errorlevel!

:run
if not exist build\patch_samples.exe (
    echo Error: build\patch_samples.exe not found. Run build.bat first.
    exit /b 1
)
echo Running patch_samples...
build\patch_samples.exe
exit /b !errorlevel!

:sanitize
echo Cleaning build directory for sanitizer build...
if exist build rmdir /s /q build
echo Configuring with sanitizers (ASan + UBSan)...
cmake -B build -G Ninja -DCMAKE_C_COMPILER="%CLANG_PATH%\clang-cl.exe" -DCMAKE_CXX_COMPILER="%CLANG_PATH%\clang-cl.exe" -DCMAKE_BUILD_TYPE=Debug -DPATCH_ENABLE_SANITIZERS=ON
if !errorlevel! neq 0 exit /b !errorlevel!
echo Building with sanitizers...
cmake --build build
exit /b !errorlevel!

:tidy
if not exist build (
    echo Configuring with clang-tidy...
    cmake -B build -G Ninja -DCMAKE_C_COMPILER="%CLANG_PATH%\clang-cl.exe" -DCMAKE_CXX_COMPILER="%CLANG_PATH%\clang-cl.exe" -DPATCH_ENABLE_CLANG_TIDY=ON
    if !errorlevel! neq 0 exit /b !errorlevel!
)
echo Running clang-tidy...
cmake --build build --target clang-tidy
exit /b !errorlevel!

:build
if not exist build (
    echo Configuring with Clang-cl...
    cmake -B build -G Ninja -DCMAKE_C_COMPILER="%CLANG_PATH%\clang-cl.exe" -DCMAKE_CXX_COMPILER="%CLANG_PATH%\clang-cl.exe"
    if !errorlevel! neq 0 exit /b !errorlevel!
)

echo Building (unit tests run automatically via POST_BUILD)...
cmake --build build
exit /b !errorlevel!
