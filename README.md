# Patch

Raymarched voxel engine with real-time destruction simulation

## Sample Scenes (legacy scenes, under .github/legacy)

![ball pit gif](.github/readme/ballpit.gif)
![melee gif](.github/readme/melee.gif)
![shooter gif](.github/readme/shooter.gif)
![building gif](.github/readme/building.gif)

## Quick Start

### Requirements

- Windows
- CMake 3.21+
- C/C++ compiler
- Ninja build system (available via CMake)
- Vulkan SDK (for building shaders, alternatively prebuilt shaders included)

### Build & Run

```shell
cmake -B build -G Ninja
cmake --build build
./build/patch_samples.exe
```

### Without Vulkan SDK (uses prebuilt shaders)

```shell
cmake -B build -G Ninja -DPATCH_USE_PREBUILT_SHADERS=ON
cmake --build build
```

## Project Structure

```dir
engine/     Core engine (voxels, physics, rendering)
content/    Materials, shapes, scene definitions
game/       Sample game scenes
shaders/    GPU shaders (auto-compiled at build)
app/        Application entry point
```

## Tools

**voxelize** - Convert OBJ to voxel:

```shell
./build/voxelize.exe model.obj output.c --resolution 16
```
