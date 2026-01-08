@echo off
setlocal

if "%1"=="clean" (
    echo Cleaning build directory...
    if exist build rmdir /s /q build
    echo Done.
    exit /b 0
)

if "%1"=="configure" (
    echo Configuring...
    cmake -B build -G Ninja
    exit /b %errorlevel%
)

if "%1"=="run" (
    echo Running patch_samples...
    if exist build\patch_samples.exe (
        build\patch_samples.exe
    ) else (
        echo Error: build\patch_samples.exe not found. Run build.bat first.
        exit /b 1
    )
    exit /b %errorlevel%
)

if not exist build (
    echo Configuring...
    cmake -B build -G Ninja
    if %errorlevel% neq 0 exit /b %errorlevel%
)

echo Building...
cmake --build build
if %errorlevel% neq 0 exit /b %errorlevel%

echo.
echo Running tests...
ctest --test-dir build --output-on-failure
exit /b %errorlevel%
