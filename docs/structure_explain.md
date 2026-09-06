# Project Structure Explanation

Current architectural direction:

```text
Application
├── Window
├── Renderer
└── Game
    ├── Player
    └── NetworkClient
```

Graphics internals:

```text
Renderer
    ↓
Uses graphics resources
├── Shader
├── VertexArray
└── VertexBuffer
```

The important architectural idea is that the Renderer should **use renderable resources**, rather than becoming responsible for manually creating every VBO, VAO, shader, texture, etc.

---

# Application

`Application` is the main high-level owner/orchestrator of the program.

Responsibilities:

```text
Application
├── Own Window
├── Own Renderer
├── Own Game
└── Run main application loop
```

The purpose is to keep `main.cpp` extremely small.

Target `main.cpp`:

```cpp
int main(){
    try{
        Application app;
        app.RunApplication();
    }
    catch(const std::exception& e){
        std::cerr << e.what() << "\n";
    }
}
```

The application loop conceptually becomes:

```text
while application is running
    ↓
process events
    ↓
update game
    ↓
render frame
    ↓
present frame
```

`Application` coordinates systems.

It should not contain low-level GLFW/OpenGL setup details.

---

# Window

`Window` owns the GLFW window and handles window/context-specific responsibilities.

Responsibilities currently include:

```text
Window
├── Initialize GLFW
├── Create GLFW window
├── Make OpenGL context current
├── Initialize GLAD
├── Set initial OpenGL viewport
├── Register framebuffer resize callback
├── Check close state
├── Poll events
├── Swap buffers
└── Destroy window / terminate GLFW
```

Important API:

```cpp
Window(...);
~Window();

bool ShouldClose();

void PollEvents();

void SwapBuffers();

GLFWwindow* GetNativeWindow();
```

Window does **not** own the main application loop.

The loop belongs to `Application`.

Window also should not directly contain game-specific behavior.

For example:

```text
Window minimized
      ↓
Event system / Application
      ↓
Game decides whether to pause
```

Instead of:

```text
Window
    ↓
Game::Pause()
```

This keeps dependencies flowing in the correct direction.

---

# VertexBuffer

`VertexBuffer` owns one OpenGL buffer object.

Current internal state:

```cpp
GLuint vbo;
GLenum target;
```

The target is remembered so that callers do not repeatedly pass:

```cpp
GL_ARRAY_BUFFER
```

when binding/unbinding.

Responsibilities:

```text
VertexBuffer
├── Generate VBO
├── Bind VBO
├── Upload vertex data
├── Unbind VBO
└── Delete VBO
```

Constructor encapsulates:

```cpp
glGenBuffers();
glBindBuffer();
glBufferData();
```

Bind:

```cpp
glBindBuffer(target, vbo);
```

Unbind:

```cpp
glBindBuffer(target, 0);
```

Destructor:

```cpp
glDeleteBuffers();
```

Conceptually:

```text
CPU vertex data
      ↓
VertexBuffer
      ↓
OpenGL buffer
      ↓
GPU-accessible vertex data
```

---

# VertexArray

`VertexArray` owns one OpenGL Vertex Array Object.

Internal state:

```cpp
GLuint vao;
```

A VAO stores the configuration/recipe describing how vertex attributes should be interpreted.

Responsibilities:

```text
VertexArray
├── Generate VAO
├── Bind VAO
├── Unbind VAO
├── Configure attributes
├── Enable attributes
└── Delete VAO
```

Constructor:

```cpp
glGenVertexArrays();
```

Bind:

```cpp
glBindVertexArray(vao);
```

Unbind:

```cpp
glBindVertexArray(0);
```

Attribute setup:

```cpp
glVertexAttribPointer();
glEnableVertexAttribArray();
```

Current abstraction:

```cpp
vao.AddAttribute(
    index,
    size,
    type,
    normalized,
    stride,
    pointer
);
```

Destructor:

```cpp
glDeleteVertexArrays();
```

Conceptually:

```text
VertexBuffer
    ↓
contains raw data

VertexArray
    ↓
describes how that raw data
should be interpreted
```

Example:

```text
VBO

[x][y][x][y][x][y]

VAO

Attribute 0
├── 2 components
├── GL_FLOAT
├── stride = 2 * sizeof(float)
└── offset = 0
```

---

# Shader

`Shader` owns the final linked OpenGL shader program.

Internal state:

```cpp
GLuint shaderProgram;
```

Responsibilities:

```text
Shader
├── Read GLSL files
├── Create vertex shader
├── Create fragment shader
├── Supply GLSL source
├── Compile shaders
├── Check compilation
├── Create shader program
├── Attach shaders
├── Link shader program
├── Check linking
├── Delete temporary shader objects
├── Bind shader program
├── Unbind shader program
└── Delete final shader program
```

Current high-level flow:

```text
vertex.glsl
    ↓
readShader()
    ↓
glCreateShader(GL_VERTEX_SHADER)
    ↓
CompileShader()
        ↓
    glShaderSource()
        ↓
    glCompileShader()
        ↓
    compilation check


fragment.glsl
    ↓
readShader()
    ↓
glCreateShader(GL_FRAGMENT_SHADER)
    ↓
CompileShader()


Compiled Vertex Shader ─────┐
                            ├── LinkProgram()
Compiled Fragment Shader ───┘
                                  ↓
                          glCreateProgram()
                                  ↓
                          glAttachShader()
                                  ↓
                           glLinkProgram()
                                  ↓
                           Shader Program
```

After linking:

```cpp
glDeleteShader(vertexShader);
glDeleteShader(fragmentShader);
```

The final program remains alive.

Bind:

```cpp
glUseProgram(shaderProgram);
```

Unbind:

```cpp
glUseProgram(0);
```

Destructor:

```cpp
glDeleteProgram(shaderProgram);
```

---

# Renderer

Renderer is the next major system.

Its job should be:

> Take already-created renderable resources and issue the commands required to render them.

It should **not** become responsible for manually constructing every shader, vertex buffer, vertex array, texture, player, sprite, etc.

Bad direction:

```text
Renderer
├── Create every VBO
├── Create every VAO
├── Compile every shader
├── Create every texture
├── Know every game object
└── Draw everything
```

That would turn Renderer into a giant tightly-coupled class.

Preferred direction:

```text
Graphics / Game resources
        ↓
already exist
        ↓
Renderer
        ↓
uses them to issue draw calls
```

Current low-level rendering sequence:

```cpp
shader.Bind();
vao.Bind();

glDrawArrays(
    GL_TRIANGLES,
    0,
    3
);
```

Renderer will eventually abstract this operation.

Before defining its final API, we still need to decide what information represents a complete draw call.

Possible information includes:

```text
Shader / Material
VertexArray / Mesh
Primitive type
Starting vertex
Vertex count
```

This architectural decision should be made before implementing the Renderer.

---

# Game

Future system.

`Game` owns game-specific state and logic.

Examples:

```text
Game
├── Players
├── World state
├── gameplay update
├── collision/game rules
└── networking interaction
```

Game should not directly contain raw GLFW/OpenGL management.

Long-term direction:

```text
Game
    ↓
Renderable objects
    ↓
Renderer
    ↓
Graphics resources
    ↓
OpenGL
```

---

# Player

Future game object.

Possible responsibilities:

```text
Player
├── Position
├── Movement
├── State
└── Network identity/state
```

Rendering and networking responsibilities should remain separated where practical.

---

# NetworkClient

Future networking system.

Responsible for client/server communication.

Possible responsibilities:

```text
NetworkClient
├── Connect to server
├── Send local player state/input
├── Receive remote state
└── Expose network events/state to Game
```

Desired separation:

```text
NetworkClient
      ↓
    Game
      ↓
  Renderer
```

Renderer should never care whether player coordinates originated locally or over the network.

It should simply receive/render the current game state.

---

# Current Dependency Direction

```text
main
 ↓
Application
 ├──────────────→ Window
 │
 ├──────────────→ Renderer
 │                   ↓
 │             Graphics Resources
 │             ├── Shader
 │             ├── VertexArray
 │             └── VertexBuffer
 │
 └──────────────→ Game
                     ↓
                  Player
                     ↓
                NetworkClient
```

The goal is to keep lower-level systems independent from higher-level systems.

For example:

```text
VertexBuffer
```

should not know about:

```text
Renderer
Game
Player
Application
Window
NetworkClient
```

It only knows:

> I own and manage an OpenGL buffer.

Similarly:

```text
Shader
```

only needs to understand shader/program management.

This keeps the engine modular and scalable.

---

# Current Resource Setup

Current graphics-resource initialization pattern:

```cpp
float vertices[] = {
     0.0f,  0.5f,
    -0.5f, -0.5f,
     0.5f, -0.5f
};

VertexBuffer vbo(
    GL_ARRAY_BUFFER,
    sizeof(vertices),
    vertices,
    GL_STATIC_DRAW
);

VertexArray vao;

vao.Bind();
vbo.Bind();

vao.AddAttribute(
    0,
    2,
    GL_FLOAT,
    GL_FALSE,
    2 * sizeof(float),
    0
);

vao.Unbind();

Shader shader(
    "src/vertex.glsl",
    "src/fragment.glsl"
);
```

Current drawing sequence:

```cpp
shader.Bind();
vao.Bind();

glDrawArrays(
    GL_TRIANGLES,
    0,
    3
);
```

This is temporary low-level usage.

The next architecture step is to determine what information the future Renderer receives for each draw call.
