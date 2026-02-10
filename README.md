![Output 1 - CPU Effects, Weather, Animation](outputs/output-1.gif)

> A high-performance **CPU-only real-time rasterizer** with animation, post-processing, and runtime scene editing.  
> No GPU rasterization. No hardware shaders. Everything runs on the CPU  
> (except for using the CPU-rasterized image with DX11 for presentation).

---

## Build Instructions

> **Generator:** Visual Studio 17 2022  
> **Platform:** x64  
> **CMake:** Multi-config (Release / RelWithDebInfo)

---

### Assignment Build Instructions  
*(run from the repository root)*

#### RelWithDebInfo (benchmark testing)
```powershell
cmake -S assignment -B assignment-relwithdebinfo -G "Visual Studio 17 2022" -A x64 -DCMAKE_CONFIGURATION_TYPES="Release;RelWithDebInfo" -DCMAKE_BUILD_TYPE=""

(next)

cmake --build assignment-relwithdebinfo --config RelWithDebInfo
```

executables should be in `assignment-relwithdebinfo/RelWithDebInfo`

#### Release (no debug info)
```powershell
cmake -S assignment -B assignment-release -G "Visual Studio 17 2022" -A x64 -DCMAKE_CONFIGURATION_TYPES="Release;RelWithDebInfo" -DCMAKE_BUILD_TYPE=""

(next)

cmake --build assignment-release --config Release
```
executables should be in `assignment-release/Release`

---

### Custom Scene Build Instructions  
*(run from the repository root)*

#### RelWithDebInfo
```powershell
cmake -S . -B build-relwithdebinfo -G "Visual Studio 17 2022" -A x64 -DCMAKE_CONFIGURATION_TYPES="Release;RelWithDebInfo" -DCMAKE_BUILD_TYPE=""

(next)

cmake --build build-relwithdebinfo --config RelWithDebInfo
```
executables should be in `assignment-relwithdebinfo/RelWithDebInfo`

#### Release
```powershell
cmake -S . -B build-release -G "Visual Studio 17 2022" -A x64 -DCMAKE_CONFIGURATION_TYPES="Release;RelWithDebInfo" -DCMAKE_BUILD_TYPE=""

(next)

cmake --build build-release --config Release
```
executables should be in `assignment-release/Release`

---


# Fox CPU Rasterizer

Fox CPU Rasterizer is a **C++ based rasterizer and runtime scene editor** built to explore how far a carefully optimized, multithreaded CPU renderer can go.

This project intentionally avoids GPU raster pipelines. All stages geometry processing, rasterization, shading, animation, and post-processing—are executed entirely on the **CPU**  
(most of the development time around 70% was spent on optimization).

It serves both as:
- a **graphics rendering intuition and learning renderer built the hard way**, and
- a **runtime scene editor** with real time interaction and serialization  
  (PS: add audio before anything ELSE!)

---

## What is shown above (Output 1)

- Screen-space ambient occlusion (SSAO) — only a test version, since doing an exhaustive one with static reflections on the CPU is expensive!
- Rain effects
- Skeletal animation evaluated fully on the CPU
- CPU post-processing pipeline
- Stable, deterministic rendering

The animation, lighting (limited to directional light only, since I already invested significant time in lighting in my previous project and avoided it here due to time constraints), and post effects you see are not previews or offline renders! - they are produced in real time on the CPU!!.

---

## Runtime Editing and Heavy CPU Rendering

![Output 2 – Runtime Editing](outputs/output-2.gif)

This output demonstrates:

- Selecting scene objects at runtime by holding the mouse
- Moving and rotating objects using the mouse
- Immediate visual feedback in the renderer
- Heavy rendering workloads handled entirely by the CPU  
  (the map contains millions of triangles rendered on the CPU)

The same runtime that renders the scene also supports **interactive editing** without switching modes or using a separate editor.

---

## Core Features

### CPU-Only Rendering Pipeline
- Triangle rasterization on the CPU
- Depth testing, lighting, and blending on the CPU
- No GPU raster pipeline usage
- Deterministic frame output

### Multithreaded Renderer
- Persistent worker threads
- Reduced synchronization in hot paths
- Designed for cache efficiency and scalability

### ECS-Driven Design
- Custom entity-component system (`fecs`)
- Scene objects stored as ECS entities
- Clear separation between:
  - scene storage and mutation (`render_queue`)
  - rendering systems
  - editor UI logic

### Runtime Scene Editing
- Create static and dynamic meshes at runtime
- Select and edit objects during execution
- Mouse-based translation and rotation
- Live editing of transforms and animation state

### CPU Animation System
- Skeletal animation evaluated per entity
- Independent animation state per object
- No shared animation playback bugs
- Runtime animation control from the editor

### Post-Processing on the CPU
- Screen-space reflections (SSR) — beta version only
- Weather effects (rain)
- Many smaller effects such as saturation, contrast, etc.
- Entire post-processing pipeline executed on the CPU

### Scene Serialization
- Single `scene.json` file
- Saves:
  - all scene objects
  - transforms
  - animation state
  - post-processing settings
- Scene loads at startup

## License

MIT License
