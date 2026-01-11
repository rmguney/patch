# Patch

Raymarched voxel engine with real-time destruction simulation.

## Features

- **Destructible voxel terrain** - Everything can be broken into pieces
- **Soft shadows** - Smooth, natural-looking shadows that update in real-time
- **Particle debris** - Destroyed voxels turn into physics-driven particles
- **Voxel objects** - Independent objects that can move and rotate
- **60 FPS target** - Optimized for smooth performance

## Sample Scenes (legacy scenes, under .github/legacy)

![ball pit gif](.github/readme/ballpit.gif)
![melee gif](.github/readme/melee.gif)
![shooter gif](.github/readme/shooter.gif)
![building gif](.github/readme/building.gif)

## Quick Start

### Requirements

- Windows
- CMake 3.21+
- C/C++ compiler (MSVC recommended)

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

## Controls

- **Mouse** - Look around / interact
- **F3** - Toggle performance overlay

## Project Structure

```
engine/     Core engine (voxels, physics, rendering)
content/    Materials, shapes, scene definitions
game/       Sample game scenes
shaders/    GPU shaders (auto-compiled at build)
app/        Application entry point
```

## Tools

**voxelize** - Convert 3D models to voxel shapes:

```shell
./build/voxelize.exe model.obj output.c --resolution 16
```
