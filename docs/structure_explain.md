# GFX_2D — Project Structure & Architecture

## 1. Current Project Structure

```text
gfx_2d/
│
├── CMakeLists.txt
├── README.md
│
├── docs/
│   ├── cheatsheet.md
│   └── structure_explain.md
│
├── external/
│   ├── glad/
│   └── glad-generated/
│
├── include/
│   ├── Core/
│   │   ├── application.hpp
│   │   └── window.hpp
│   │
│   └── Graphics/
│       ├── renderer.hpp
│       ├── vertexBuffer.hpp
│       ├── vertexArray.hpp
│       └── shader.hpp
│
└── src/
    ├── main.cpp
    │
    ├── Core/
    │   ├── application.cpp
    │   └── window.cpp
    │
    ├── Graphics/
    │   ├── renderer.cpp
    │   ├── vertexBuffer.cpp
    │   ├── vertexArray.cpp
    │   └── shader.cpp
    │
    ├── vertex.glsl
    └── fragment.glsl
```

---

# 2. High-Level Architecture

The current architecture is:

```text
Application
│
├── Window
│
├── VertexBuffer
│
├── VertexArray
│
├── Shader
│
└── Renderer
```

The important distinction is that these objects have different responsibilities.

```text
Application
    ↓
Coordinates everything

Window
    ↓
Owns the window and OpenGL context

VertexBuffer
    ↓
Owns vertex data on the GPU

VertexArray
    ↓
Defines how vertex data is interpreted

Shader
    ↓
Owns the linked shader program

Renderer
    ↓
Issues rendering commands
```

---

# 3. `main.cpp`

The goal of `main.cpp` is to remain extremely small.

Current version:

```cpp
#include <iostream>
#include <cmath>
#include "../include/Core/application.hpp"

int main()
{
    try
    {
        Application app;
        app.RunApplication();
    }
    catch(const std::exception& e)
    {
        std::cerr << e.what() << "\n";
    }

    return 0;
}
```

The old monolithic OpenGL implementation has been removed from the active application flow.

`main.cpp` should not know how:

* GLFW is initialized
* OpenGL is initialized
* buffers are created
* shaders are compiled
* vertices are configured
* rendering happens

It simply starts the application.

---

# 4. Application

`Application` is currently the central coordinator.

Conceptually:

```text
Application
│
├── Window
├── VertexBuffer
├── VertexArray
├── Shader
└── Renderer
```

It is responsible for coordinating these systems rather than implementing their low-level behavior itself.

### Current responsibilities

* Create the window
* Create graphics resources
* Configure the initial renderable
* Run the game loop
* Coordinate rendering

---

# 5. Window

`Window` encapsulates GLFW and the OpenGL context setup.

```text
Application
     ↓
   Window
     ↓
    GLFW
     ↓
OpenGL Context
```

### Window responsibilities

* Initialize GLFW
* Create the GLFW window
* Create/make current the OpenGL context
* Initialize GLAD
* Configure framebuffer resizing
* Poll events
* Swap buffers
* Determine whether the window should close
* Destroy the window
* Terminate GLFW

Application therefore does not need to directly manage GLFW lifecycle details.

---

# 6. VertexBuffer

`VertexBuffer` is an RAII wrapper around a VBO.

```text
VertexBuffer
     ↓
OpenGL VBO
```

### Responsibilities

* Generate VBO
* Remember its buffer target
* Upload vertex data
* Bind VBO
* Unbind VBO
* Delete VBO

Current interface:

```cpp
VertexBuffer(
    GLenum target,
    GLsizeiptr size,
    const GLvoid* data,
    GLenum usage
);

void Bind();
void Unbind();
```

The class remembers the target internally so callers do not repeatedly provide it.

---

# 7. VertexArray

`VertexArray` is an RAII wrapper around a VAO.

```text
VertexArray
     ↓
OpenGL VAO
```

### Responsibilities

* Generate VAO
* Bind VAO
* Unbind VAO
* Configure vertex attributes
* Delete VAO

Current abstraction:

```cpp
void AddAttribute(
    GLuint index,
    GLint size,
    GLenum type,
    GLboolean normalized,
    GLsizei stride,
    const void* pointer
);
```

Internally this configures:

```cpp
glVertexAttribPointer(...);
glEnableVertexAttribArray(...);
```

---

# 8. Shader

`Shader` represents the linked OpenGL shader program.

Internally it performs:

```text
Read GLSL files
     ↓
Create vertex shader
     ↓
Create fragment shader
     ↓
Compile
     ↓
Check compilation
     ↓
Create program
     ↓
Attach shaders
     ↓
Link
     ↓
Check linking
     ↓
Delete temporary shader objects
```

The final linked program remains owned by `Shader`.

### Responsibilities

* Read shader source files
* Compile vertex shader
* Compile fragment shader
* Check compilation errors
* Create shader program
* Link program
* Check linking errors
* Bind program
* Unbind program
* Delete program

---

# 9. Renderer

The Renderer is the newest layer.

Its current job is intentionally small.

```text
Renderer
│
├── Clear()
└── Draw()
```

### `Clear()`

Responsible for:

```cpp
glClearColor(...);
glClear(GL_COLOR_BUFFER_BIT);
```

### `Draw()`

Responsible for issuing:

```cpp
glDrawArrays(
    mode,
    first,
    count
);
```

Current API:

```cpp
void Draw(
    GLenum mode,
    GLint startIndex,
    GLsizei count
);
```

---

# 10. Renderer Does Not Own Resources

An important current architectural decision:

```text
Renderer
    ✗ does not own VertexBuffer
    ✗ does not own VertexArray
    ✗ does not own Shader
```

Instead:

```text
Application
    │
    ├── owns resources
    │
    └── prepares OpenGL state
             ↓
          Renderer
             ↓
       issues draw command
```

This prevents Renderer from becoming a giant class that creates and manages every graphics resource.

---

# 11. Current Initialization Pipeline

When `Application` is constructed:

```text
Application
    │
    ├── Window
    │     ├── GLFW initialization
    │     ├── Window creation
    │     ├── OpenGL context
    │     └── GLAD
    │
    ├── VertexBuffer
    │     └── Vertex data uploaded
    │
    ├── VertexArray
    │     └── Vertex attributes configured
    │
    ├── Shader
    │     ├── Vertex shader
    │     ├── Fragment shader
    │     └── Linked program
    │
    └── Renderer
```

---

# 12. Triangle Setup

The current test geometry:

```cpp
float vertices[] =
{
     0.0f,  0.5f,
    -0.5f, -0.5f,
     0.5f, -0.5f
};
```

represents three 2D vertices:

```text
       (0, 0.5)
          /\
         /  \
        /    \
       /______\
(-0.5,-0.5)  (0.5,-0.5)
```

Each vertex contains:

```text
X
Y
```

Therefore:

```cpp
stride = 2 * sizeof(float);
```

and the attribute starts at:

```cpp
offset = 0;
```

---

# 13. VAO Setup Pipeline

The current setup is:

```text
Create VBO
    ↓
Upload vertex data
    ↓
Create VAO
    ↓
Bind VAO
    ↓
Bind VBO
    ↓
Configure attribute 0
    ↓
Enable attribute 0
    ↓
Unbind VAO
    ↓
Unbind VBO
```

The VAO now remembers how the vertex data should be interpreted.

---

# 14. Rendering Pipeline

Each frame currently follows:

```text
GameLoop
    │
    ├── PollEvents
    │
    ├── Renderer.Clear()
    │
    ├── Shader.Bind()
    │
    ├── VertexArray.Bind()
    │
    ├── Renderer.Draw(
    │       GL_TRIANGLES,
    │       0,
    │       3
    │   )
    │
    ├── VertexArray.Unbind()
    │
    ├── Shader.Unbind()
    │
    └── SwapBuffers()
```

The important sequence is:

```text
Prepare OpenGL state
        ↓
Issue draw command
        ↓
Present framebuffer
```

---

# 15. Why Renderer Exists

Without Renderer, Application would directly contain:

```cpp
glClearColor(...);
glClear(...);

shader.Bind();
vao.Bind();

glDrawArrays(...);

vao.Unbind();
shader.Unbind();
```

That causes Application to know too much about OpenGL.

With Renderer:

```cpp
renderer.Clear(...);

shader.Bind();
vao.Bind();

renderer.Draw(...);
```

The actual draw command is now centralized behind Renderer.

This is the beginning of separating:

```text
WHAT should happen?
        ↓
Application / Game

HOW does OpenGL perform it?
        ↓
Graphics layer
```

---

# 16. Current Architecture vs Future Architecture

### Current

```text
Application
│
├── Window
├── VertexBuffer
├── VertexArray
├── Shader
└── Renderer
```

This is intentionally low-level because we are building the engine from the bottom up.

### Future direction

Eventually we want something closer to:

```text
Application
│
├── Window
├── Renderer
└── Game
     │
     └── Renderable Objects
          │
          ├── Mesh
          │    ├── VertexBuffer
          │    └── VertexArray
          │
          ├── Material
          │    └── Shader
          │
          └── Transform
```

Potentially:

```text
Game
 ↓
Sprite / Renderable
 ↓
Renderer
 ↓
OpenGL
```

---

# 17. Why We Are Not Building Everything Yet

It would be tempting to immediately create:

```text
Mesh
Material
Texture
Sprite
Transform
RenderCommand
ResourceManager
Renderer
```

But that would hide the important concepts before they are understood.

The current progression is deliberate:

```text
OpenGL
   ↓
Window
   ↓
VBO
   ↓
VAO
   ↓
Shader
   ↓
Renderer
   ↓
Mesh
   ↓
Material
   ↓
Renderable / Sprite
   ↓
Game
```

Each layer should solve a specific problem before another abstraction is added.

---

# 18. Current Dependency Direction

The intended dependency direction is:

```text
Application
    ↓
Core / Graphics
    ↓
OpenGL / GLFW
```

Application coordinates systems.

Graphics classes hide OpenGL details.

Window hides GLFW details.

The lower-level systems should not depend on the Game layer.

---

# 19. Current Milestone

The first complete rendering abstraction milestone is now finished.

We have successfully moved from:

```text
main.cpp
    ↓
Everything directly implemented with OpenGL
```

to:

```text
main.cpp
    ↓
Application
    ↓
Window + Graphics abstractions
    ↓
Renderer
    ↓
OpenGL
```

The application now successfully:

* Creates a window
* Initializes OpenGL
* Initializes GLAD
* Loads vertex data
* Configures a VAO
* Compiles GLSL shaders
* Links a shader program
* Clears the framebuffer
* Draws a triangle
* Handles framebuffer resizing
* Presents frames
* Cleans up resources through RAII

---

# 20. Next Architectural Question

The current pipeline still contains:

```cpp
shader.Bind();
vao.Bind();

renderer.Draw(...);
```

Therefore `Application` still knows that rendering requires:

```text
Shader
+
VertexArray
+
Draw parameters
```

The next architectural problem is:

> How do we represent a drawable object so that Application/Game does not need to understand individual OpenGL resources?

This leads toward concepts such as:

```text
Mesh
Material
Renderable
Sprite
```

Before implementing those classes, the Renderer API and ownership model should be reconsidered.

The goal is eventually to reach:

```text
Game
 ↓
"Draw this object"
 ↓
Renderer
 ↓
OpenGL
```

without turning Renderer into a god class.
