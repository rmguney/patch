@echo off
setlocal enabledelayedexpansion

:: Run from the repository root, wherever the script was started from
cd /d "%~dp0"

set "CLANG_PATH=C:\Program Files\LLVM\bin"
set "BUILD_DIR=%CD%\build"

:: Initialize the MSVC environment if not already done. vswhere finds the newest Visual
:: Studio or Build Tools install that has the C++ x64 tools.
if defined VSCMD_VER goto :vcvars_done
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VCVARS="
if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        set "VCVARS=%%i\VC\Auxiliary\Build\vcvarsall.bat"
    )
)
if not defined VCVARS (
    echo ERROR: Visual Studio with the C++ x64 tools not found. Install Visual Studio or the Build Tools with the "Desktop development with C++" workload.
    exit /b 1
)
call "%VCVARS%" x64 >nul 2>&1
:vcvars_done

:: clang-cl when LLVM is installed (needed by sanitize and tidy), MSVC cl otherwise
if exist "%CLANG_PATH%\clang-cl.exe" (
    set "HAVE_LLVM=1"
    set "COMPILER_NAME=Clang-cl"
    set COMPILER_ARGS=-DCMAKE_C_COMPILER="%CLANG_PATH%\clang-cl.exe" -DCMAKE_CXX_COMPILER="%CLANG_PATH%\clang-cl.exe"
) else (
    set "HAVE_LLVM="
    set "COMPILER_NAME=MSVC cl"
    set COMPILER_ARGS=-DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl
)
set CONFIGURE=cmake -B build -G Ninja !COMPILER_ARGS! -DPATCH_RUN_TESTS_ON_BUILD=ON

:: Stop this project's own executables left running from build\ (they lock the outputs on
:: relink). Other processes, including other builds, are left alone.
powershell -NoProfile -Command "Get-Process patch_samples,test_scenes,test_launch,test_render_perf -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path.StartsWith('%BUILD_DIR%\', [StringComparison]::OrdinalIgnoreCase) } | Stop-Process -Force" >nul 2>&1

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
echo   sanitize   Configure with sanitizers (ASan + UBSan, needs LLVM)
echo   tidy       Run clang-tidy static analysis (needs LLVM)
echo   help       Show this help
echo.
echo Compiler: clang-cl from "%CLANG_PATH%" when installed, otherwise MSVC cl
exit /b 0

:clean
echo Cleaning build directory...
if exist build rmdir /s /q build
echo Done.
exit /b 0

:configure
echo Configuring with %COMPILER_NAME%...
%CONFIGURE%
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
if not defined HAVE_LLVM (
    echo ERROR: The sanitizer build needs LLVM ^(clang-cl and its ASan/UBSan runtimes^), not found in "%CLANG_PATH%".
    exit /b 1
)
echo Cleaning build directory for sanitizer build...
if exist build rmdir /s /q build
echo Configuring with sanitizers (ASan + UBSan)...
%CONFIGURE% -DCMAKE_BUILD_TYPE=Debug -DPATCH_ENABLE_SANITIZERS=ON
if !errorlevel! neq 0 exit /b !errorlevel!
echo Building with sanitizers...
cmake --build build
exit /b !errorlevel!

:tidy
if not defined HAVE_LLVM (
    echo ERROR: tidy needs LLVM ^(clang-tidy^), not found in "%CLANG_PATH%".
    exit /b 1
)
:: (Re)configure so an existing build directory also gets the clang-tidy target
echo Configuring with clang-tidy...
%CONFIGURE% -DPATCH_ENABLE_CLANG_TIDY=ON
if !errorlevel! neq 0 exit /b !errorlevel!
echo Running clang-tidy...
cmake --build build --target clang-tidy
exit /b !errorlevel!

:build
if not exist build (
    echo Configuring with %COMPILER_NAME%...
    %CONFIGURE%
    if !errorlevel! neq 0 exit /b !errorlevel!
) else (
    rem Existing build dirs: keep the post-build test run on (the CMake default is off)
    cmake -B build -DPATCH_RUN_TESTS_ON_BUILD=ON >nul
    if !errorlevel! neq 0 exit /b !errorlevel!
)

echo Building (unit tests run automatically via POST_BUILD)...
cmake --build build
exit /b !errorlevel!
