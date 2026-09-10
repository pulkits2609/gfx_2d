# gfx_2d

This repo showcases my journey of learning OpenGL by building a simple 2D game from scratch.

The goal is to understand how the different parts of a 2D game/graphics application work together, while gradually building a clean and reusable foundation.

## Current Status

The project currently has:

- Cross-platform window creation using GLFW
- OpenGL context and GLAD initialization
- Basic shader system
- Vertex Buffer and Vertex Array abstractions
- Triangle mesh abstraction
- Renderer abstraction
- 2D Transform system
- Keyboard input handling
- WASD-based object movement
- GLM integration for matrix calculations
- CMake-based build system
- Cross-platform builds for Windows and Linux
- GitHub Actions build automation
- GitHub Releases for Windows and Linux builds

## Current Architecture

```text
Application
├── Window
├── KeyboardInput
├── Renderer
├── Shader
├── Mesh
└── Transform
```

## Rendering Flow

```text
Keyboard Input
      ↓
   Transform
      ↓
  Model Matrix
      ↓
    Shader
      ↓
   Renderer
      ↓
    OpenGL
```

## Technologies

- C++17
- OpenGL
- GLFW
- GLAD
- GLM
- CMake
- GitHub Actions

## Project Structure

```text
gfx_2d/
├── include/
│   ├── Core/
│   ├── Graphics/
│   └── Input/
├── src/
│   ├── Core/
│   ├── Graphics/
│   ├── Input/
│   └── main.cpp
├── assets/
│   └── shaders/
├── external/
│   └── glad-generated/
├── docs/
├── CMakeLists.txt
└── README.md
```

## Building

The project uses CMake and FetchContent to manage external dependencies.

### Linux

```bash
cmake -S . -B build
cmake --build build
./build/gfx
```

### Windows

```powershell
cmake -S . -B build
cmake --build build --config Release
```

The executable and required assets are packaged automatically for the project's releases.

## Releases

Pre-built Windows and Linux versions are available through the GitHub Releases page.

Each release contains the executable together with the required runtime assets.

## Goal

The project will gradually evolve from a basic OpenGL learning project into a small 2D game/engine foundation, with systems for rendering, input, transformations, cameras, assets, and eventually game-specific functionality.