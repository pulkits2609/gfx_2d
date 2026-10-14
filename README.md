# gfx_2d

This repo showcases my journey of learning OpenGL by building a simple 2D game from scratch.

The goal is to understand how the different parts of a 2D game/graphics application work together, while gradually building a clean and reusable foundation.

## Current Status

The menu offers Create Lobby, Join Lobby and Play Offline. A host chooses 2-4
teams and a match duration of 1-30 minutes. Players choose an available team;
only the host can start, with at least one opposing player. Joining is locked
during the match. Two teams allow four players each, three allow three each,
and four allow two each. The host leaving closes the lobby.

The dedicated server owns movement, pushing, travelling bullets, health,
reload, boost, respawns, scores and the match timer. TCP handles sessions and
lobbies; authenticated UDP endpoints carry inputs and snapshots. The server
runs at 30 Hz and sends snapshots at 20 Hz. Clients predict their own movement
and interpolate other players. Offline practice keeps four switchable players.

Run commands from the executable directory so shader assets can be found:

```powershell
cd build
.\gfx_server.exe
# In two other terminals, launch one client in each:
.\gfx.exe --name Pulkit
.\gfx.exe --name Player2
```

Create a lobby in the first client, join its code in the second, choose teams,
then start as host. The default address is `127.0.0.1`, TCP port 7777 and UDP port
7778. For LAN play, run `gfx_server --bind 0.0.0.0` and enter that computer's LAN
address in each client. The server must be reachable on both ports; codes belong
to that server. Custom server ports use `--port` and `--udp-port`.

Use WASD to move, mouse to aim, LMB to fire, R to reload and Shift to boost.
Escape leaves an online match or returns offline practice to the menu. Keys
1-4 switch players only in offline practice. Build without graphics using
`-DGFX_BUILD_CLIENT=OFF`; run checks with `ctest --test-dir build --output-on-failure`.


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
