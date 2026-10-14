# TCP control protocol version 1

This checkpoint establishes sessions only. Movement and combat still run in the
local practice game. There is no UDP gameplay synchronization, lobby or voice
transport yet. TCP control uses port 7777. Ports 7778 (gameplay UDP) and 7779
(voice UDP) remain reserved for later steps and are not opened by this server.

## Frame format

Each frame is **four length bytes followed by one UTF-8 JSON object**. The length
is an unsigned 32-bit integer in big-endian/network order. It counts body bytes,
not characters or the four header bytes. Valid body sizes are 1 through 16384.
For example, a body of 256 bytes has header `00 00 01 00`.

TCP does not preserve application messages. ReadHeader obtains exactly four
bytes with Asio async_read, checks the size before allocating, then ReadBody
obtains exactly that many bytes. Split headers/bodies and several frames in one
TCP write therefore work without special cases. A truncated frame closes only
its own connection. The same shared Protocol code is used on both ends.

Every object contains integer `version: 1` and string `type` (at most 32 bytes).
Optional `requestId` is an unsigned integer from 0 through 4294967295. Nesting
beyond 12 levels, malformed JSON and incorrect field types are rejected.

## Session flow

1. Connect to the dedicated server.
2. Send `{"version":1,"type":"hello","name":"Player 1"}`.
3. Receive `{"version":1,"type":"welcome","playerId":1,"name":"Player 1","sessionToken":"..."}`.
4. Exchange control requests. Close the socket or send `goodbye` to leave.

Names are 1-24 printable ASCII characters, with no leading/trailing space. IDs
are assigned by the server, unique during its lifetime. Each accepted hello gets
a 256-bit random token, encoded as 64 hexadecimal characters. Windows uses the
system RNG through BCryptGenRandom; Linux reads /dev/urandom. Tokens belong to
the current TCP session and are discarded when it closes. Neither server logs
nor the control probe print tokens. They provide a basis for binding UDP to a
session later; no UDP registration is implemented in this checkpoint.

| Request type | Reply | Meaning |
| --- | --- | --- |
| hello | welcome | Required first message; establishes the session. |
| ping | pong | Heartbeat; echoes optional requestId. |
| players | players | Array of connected playerId/name entries; echoes requestId. |
| goodbye | goodbye, then close | Cleanly finishes the session. |
| unknown type | error | Unknown operation; established session remains usable. |

Invalid envelopes, invalid initial hello and rate-limit violations receive an
error and are closed. Invalid lengths close immediately without allocating the
advertised body. Player-list order is unspecified. A repeated hello after
welcome is an unknown operation and never creates a second identity.

## Bounds and timing

- At most 64 TCP connections, including clients still sending hello.
- Each session keeps at most one 16 KiB input body and one bounded reply.
- The server reads another request only after its reply finishes writing.
- At most 30 messages per connection per one-second rate window.
- Initial header: 5 seconds. Each body/write: 5 seconds.
- Established idle header: 30 seconds. Partial headers share that deadline.
- The client sends a ping every 10 seconds, bounds its output queue to 8 frames,
  and processes at most 32 ready callbacks per render frame.
- DNS/connect and the initial welcome have five-second deadlines. Client
  errors leave local practice running and display the TCP error in the HUD.

These sessions identify connections, not verified human accounts. The transport
is plaintext and intended for controlled LAN/trusted-server testing. A token
does not turn this into authenticated public matchmaking or encrypted transport.

## Beginner workflow through the code

`server/main.cpp` creates io_context and GameServer, prints the listening port,
then runs the event loop. GameServer accepts sockets. Each ControlSession owns
one socket, deadline and buffers. Its functions follow:

`ReadHeader -> ReadBody -> HandleMessage -> Send -> ReadHeader`.

Callbacks keep a session alive through shared_ptr while reads/writes finish.
The server owns the active-session map; Close erases the session and cancels its
socket/timer. Stop closes all sessions, so Ctrl+C can exit cleanly.

GameClient follows the same frame format. Application calls Poll every frame;
it never performs a blocking socket read on the rendering thread. A GameClient
supports one connection attempt; relaunch to reconnect for this checkpoint.

## Run it

From the executable directory, start:

```powershell
.\gfx_server.exe
```

In a second terminal:

```powershell
.\gfx_control_client.exe 127.0.0.1 7777 "Control Tester"
.\gfx.exe --server 127.0.0.1 --port 7777 --name "Pulkit" --teams 4
```

The graphical HUD says `TCP #... CONNECTED / LOCAL PRACTICE`. Starting two
graphical clients creates two sessions; their gameplay is still separate until
the next networking checkpoint. For LAN access use
`gfx_server --bind 0.0.0.0 --port 7777`, with clients using that machine's address.
Default binding is loopback. `--port 0` chooses an available port for tests.

## Headless build and tests

```sh
cmake -S . -B build-headless -DGFX_BUILD_CLIENT=OFF
cmake --build build-headless --config Release
ctest --test-dir build-headless -C Release --output-on-failure
```

No OpenGL, GLFW, ImGui, window or shader assets are required in this build.
GLM, Boost headers and nlohmann/json are fetched at pinned versions. Server
dependencies are header-only apart from platform sockets/threading libraries.
CTest runs gameplay and protocol checks. With Python 3 available, tcp_sessions
also launches real server/C++ client processes and exercises fragmentation,
coalescing, disconnects, malformed messages, size limits, timeouts, rate limits
and the connection cap. Dependency notices are copied into `licenses/`.
