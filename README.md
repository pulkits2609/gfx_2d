# gfx_2d

This repo showcases my journey of learning OpenGL by building a simple 2D game from scratch.

The goal is to understand how the different parts of a 2D game/graphics application work together, while gradually building a clean and reusable foundation.

## Current Status

The menu offers Create Lobby, Browse Public Lobbies, Join Private Lobby and Play Offline. A host chooses 2-4
teams and a match duration of 1-30 minutes. Players choose an available team;
only the host can start, with everyone ready and at least one opposing player. Joining is locked
during the match. Two teams allow four players each, three allow three each,
and four allow two each. The host leaving closes the lobby.

The dedicated server owns movement, pushing, travelling bullets, health,
reload, boost, respawns, scores and the match timer. TCP handles sessions and
lobbies; authenticated UDP endpoints carry inputs and snapshots. The server
runs at 30 Hz and sends snapshots at 30 Hz. Clients predict their own movement
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

Mapped GLFW controllers are detected automatically: left stick moves, right stick aims,
LT boosts, RT fires, and X/Square or Back/Select reloads. Start opens the leave menu.
The Settings gear in menus, lobbies and results lets you choose a controller and remap
boost/fire; the two sticks stay fixed. Back/Select still reloads when X is remapped.
Settings are hidden during active gameplay. UI scale is limited to 85–120% and does
not change the arena or hitboxes; small windows scroll when needed. Preferences are
saved per user (`%LOCALAPPDATA%/GFX_2D/settings.json` on Windows).
Escape leaves an online match or returns offline practice to the menu. Keys
1-4 switch players only in offline practice. Build without graphics using
`-DGFX_BUILD_CLIENT=OFF`; run checks with `ctest --test-dir build --output-on-failure`.

The performance panel shows FPS, average frame time, TCP round-trip ping,
the age of the newest gameplay snapshot, and the server tick rate. Ping refreshes
about once per second; snapshot age measures freshness, not one-way latency.
Unavailable or stale measurements show a placeholder rather than zero.
Spawning and respawning give three seconds of protection from damage. Players
can fire immediately without losing that protection; the health HUD shows the
remaining shield time.
Protected players also have a visible ring and a SHIELD label. Bullets use their
team colour, and a brief red arena border marks a confirmed drop in local health.
The menu and performance panel show the client version so players can compare builds.

## Debug network logs

Set `--tick-rate 180` when starting `gfx_server` to choose its simulation rate
(1-180 Hz; default 30). The server tells clients the selected rate so input
sending and movement prediction use the same step. Snapshots remain capped at
30 Hz, with client interpolation between updates. Higher tick rates require
more CPU and input bandwidth; all players should use the updated client.

Debug builds enable `GFX_DEBUG` through CMake, following realtime-voice's
`VOICE_DEBUG` pattern. `include/Core/debugLog.hpp` provides `GFX_DEBUG_LOG`:
it prints timestamped messages to stderr in Debug, and compiles the messages
and their formatting out of Release. Timestamps are UTC (`Z`).

For Ninja builds, configure a separate server directory so the running game
and the client package are not overwritten:

```powershell
cmake -S . -B build/debug-server -G Ninja -DCMAKE_BUILD_TYPE=Debug -DGFX_BUILD_CLIENT=OFF
cmake --build build/debug-server --target gfx_server --parallel 3
```

After stopping the previous GFX server, start this build with your chosen ports:

```powershell
.\build\debug-server\gfx_server.exe --bind 0.0.0.0 --port 9999 --udp-port 9999 --voice-port 9998
```

To also save its console output for diagnosis:

```powershell
.\build\debug-server\gfx_server.exe --bind 0.0.0.0 --port 9999 --udp-port 9999 --voice-port 9998 2>&1 | Tee-Object -FilePath .\build\debug-server\server-debug.log
```

Use `-DCMAKE_BUILD_TYPE=Release` with a separate `build/release-server` directory
for the normal build without debug diagnostics. Visual Studio generators select
the configuration at build time using `cmake --build <directory> --config Debug`
or `--config Release`, rather than `CMAKE_BUILD_TYPE`.

The logs identify connections, explicit lobby leaves, host disbands, match starts,
registered UDP endpoints, first snapshots, first inputs and exact disconnect
reasons. During matches, a summary every two seconds shows input counts, snapshot
send attempts/errors, and the age of the last UDP packet and input. A successful
UDP send only means the OS accepted it; it does not prove that a remote player
received it. Invalid UDP warnings are rate-limited and session/rejoin tokens are
not printed. Debug clients also print their own connection failure reason.

The client keeps its gameplay UDP route alive with a registration packet every
two seconds, including while waiting in a lobby. When a match starts or snapshots
stall, it sends `refresh_gameplay` over its existing TCP session. The server
replies with `gameplay_refresh` and allows a UDP registration from a changed
port for three seconds; the session token and TCP peer address must still match.
The previous endpoint loses access after the change. Keepalives never refresh
the gameplay input timeout, so missing player input still stops movement and
eventually disconnects the player. Both the server and clients need this update
to recover from router port changes.

## Voice chat

Voice uses the separate realtime-voice applications (protocol 2, integration
checked against commit `d2dd174`). One `voice_server` serves the GFX server's
voice groups; each running GFX client starts one background `voice_client --ipc`.
No microphone samples pass through the game server or the process pipes.

Use an extracted protocol-2 voice release as the runtime. Configure its directory
when building GFX; CMake copies the executables, libraries and license notices into
the game's `voice/` folder. The directory must contain `BUILD.txt` and `LICENSE`.

```powershell
cmake -S . -B build -DGFX_VOICE_RUNTIME_DIR="C:/path/to/extracted/realtime-voice-release"
cmake --build build --config Release
cd build
# Start these in separate terminals, from the executable directory:
.\voice\voice_server.exe 8080
.\gfx_server.exe
.\gfx.exe --name Pulkit
```

For a multi-configuration build, the executable directory is `build/Release`.
On Ubuntu use `./voice/voice_server 8080`, `./gfx_server` and `./gfx`; keep the
voice release's launchers, `bin`, `lib` and `share` folders together. When making
a portable GFX package, include its complete `voice/` folder as well as the
normal GFX assets and runtime libraries. Voice's license notices travel with it.

You can also use `gfx --voice-client PATH` without copying the voice runtime.
Use the actual client executable on Windows and the top-level release launcher
on Ubuntu. The default path is resolved beside `gfx`, not from the working directory.
Missing voice binaries or an unavailable voice service leave gameplay available.

`gfx_server` connects to `127.0.0.1:8080` by default. Its options are:

| Option | Purpose |
| --- | --- |
| `--voice-host HOST` | Address used by the server's management connection |
| `--voice-port PORT` | Voice server port, used for both TCP and UDP |
| `--voice-public-host HOST` | Voice address advertised to players; defaults to their selected GFX server hostname |
| `--no-voice` | Run gameplay without connecting to a voice server |

For LAN play, start voice on the server computer, run GFX with `--bind 0.0.0.0`,
and allow the voice port for TCP and UDP as well as the normal game ports.
Use `--voice-public-host` when the voice service has a different reachable address.
Run voice before creating the gameplay lobby. The GFX server prints
`Voice manager connected` after its voice connection is ready.

Microphone, incoming All and incoming Team start **muted**. Open the microphone
with **Mic: muted**; use the separate **Hear All / Hear Team** controls to listen.
The All/Team buttons choose where your microphone transmits. You always remain
eligible to receive All and your own Team, independently of that selection.
Choices stay in memory until GFX closes, including if its voice child is replaced.

Waiting lobbies transmit to All and disable Team selection. Starting the match
selects Team; returning to the lobby selects All. Results retain the same group
and channels, and deaths/respawns do not change voice. Guest departure removes
that voice membership; an approved gameplay rejoin gets a new voice session on
the restored team. Host departure destroys the entire group. Manual deletion or
a voice restart disables voice for the affected lobby without ending the game.
An existing game never silently gets replacement voice channels after a restart.

At most five GFX-created groups may exist, including pending creations, waiting
lobbies, running matches and results. Manually created voice groups do not count.
Brief management TCP interruptions can resume the same owner/groups for ten
seconds. Reconciliation applies team/phase changes and departures made during
the gap. This version uses the voice project's unauthenticated testing API;
ownership IDs identify a connection's groups and are not login credentials.

To test with actual voice processes without opening audio devices:

```powershell
cmake -S . -B build -DGFX_VOICE_SERVER_EXECUTABLE="C:/voice/voice_server.exe" -DGFX_VOICE_CLIENT_EXECUTABLE="C:/voice/voice_client.exe"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The optional `voice_integration` suite checks GFX-to-voice ownership, capacity,
binding, management recovery, manual deletion, restart and two real GFX client
processes with background voice children. `voice_process` also tests broken
children and bounded pipe handling without requiring the voice runtime.
For a full one-minute match through results and lobby return, run:

```powershell
python tests/voice_integration.py build/gfx_server.exe build/voice/voice_server.exe --client-driver build/voice_test_client.exe --voice-client build/voice/voice_client.exe --timed
```

Use the corresponding Ubuntu paths without `.exe`, or `build/Release` paths for
multi-configuration Windows builds. Real two-way speech, simultaneous speakers
on headphones, and cross-machine network behavior still need a hardware/LAN check.


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

## Optional result ledger

After a timed online match, the host can expand **Upload results to ledger** on the
results screen, enter the separate ledger's host name/IP address and port (default
`127.0.0.1:8000`), and submit the final server result. Start blockchainLedger first.
The request is HTTP `POST /api/blocks`; its body is `{ "match_id": "server-issued resultId", "data": { ...final result... } }`.
Retries use the same result ID, so the ledger returns the existing block instead of
recording the match twice. Uploads do not pause gameplay and fail after eight seconds.
Returning to the lobby cancels a pending upload; a server may already have saved it.
The displayed hash is the ledger server's receipt, not independent proof of validity.

This trusted-demo API has no authentication. The host submits the received server
result, but a modified client could submit different data. No client-supplied record
should be treated as cryptographic proof that a match occurred.

To remove the uploader and its UI from the build, configure with
`-DGFX_ENABLE_LEDGER_UPLOAD=OFF`. The game server never links the HTTP helper.
