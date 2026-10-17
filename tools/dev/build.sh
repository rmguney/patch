#!/bin/bash
# Git Bash shortcut: `cmake --build build` inside the MSVC environment (found with vswhere),
# for a build dir that build.bat already configured. Stops this project's own executables
# running from build/ first, since they lock the outputs on relink.
# Usage: tools/dev/build.sh [cmake --build args...]   e.g. tools/dev/build.sh --target test_physics
set -e
cd "$(dirname "$0")/../.."

BUILD_DIR_WIN="$(cygpath -w "$PWD/build")"
powershell -NoProfile -Command "Get-Process patch_samples,test_scenes,test_launch,test_render_perf -ErrorAction SilentlyContinue | Where-Object { \$_.Path -and \$_.Path.StartsWith('$BUILD_DIR_WIN\', [StringComparison]::OrdinalIgnoreCase) } | Stop-Process -Force" >/dev/null 2>&1 || true

VSWHERE="/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
VS_PATH="$("$VSWHERE" -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | tr -d '\r')"
if [ -z "$VS_PATH" ]; then
    echo "Visual Studio with C++ tools not found" >&2
    exit 1
fi

# A throwaway .cmd sidesteps Git Bash rewriting the quotes cmd needs
STEP="$(mktemp --suffix=.cmd)"
trap 'rm -f "$STEP"' EXIT
printf '@call "%s\\VC\\Auxiliary\\Build\\vcvarsall.bat" x64 >nul 2>&1 && cmake --build build %s\r\n' "$VS_PATH" "$*" > "$STEP"
cmd //c "$(cygpath -w "$STEP")"
