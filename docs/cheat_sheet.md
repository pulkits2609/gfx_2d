# GFX_2D — OpenGL & Architecture Cheatsheet

## 1. GLFW

### What GLFW does

GLFW is responsible for:

* Creating the application window
* Creating the OpenGL context
* Handling keyboard/mouse/window events
* Processing events
* Swapping front/back buffers

GLFW is **not** the graphics API.

OpenGL performs the actual rendering.

### Basic Window Flow

```cpp
glfwInit();

GLFWwindow* window =
    glfwCreateWindow(width, height, title, nullptr, nullptr);

glfwMakeContextCurrent(window);

gladLoadGL(glfwGetProcAddress);

while(!glfwWindowShouldClose(window))
{
    glfwPollEvents();

    // Rendering

    glfwSwapBuffers(window);
}

glfwDestroyWindow(window);
glfwTerminate();
```

---

# 2. OpenGL Context

`GLFWwindow*` is an opaque GLFW window handle.

The OpenGL context contains the OpenGL state associated with the window.

```cpp
glfwMakeContextCurrent(window);
```

makes that context current on the calling thread.

GLAD must be initialized **after** a valid OpenGL context exists because GLAD needs to obtain OpenGL function addresses from the active context.

```cpp
gladLoadGL(glfwGetProcAddress);
```

---

# 3. Framebuffer Resize

The framebuffer callback is responsible for updating the OpenGL viewport when the drawable framebuffer changes size.

```cpp
void FramebufferSizeCallback(
    GLFWwindow* window,
    int width,
    int height
)
{
    glViewport(0, 0, width, height);
}
```

Register it with:

```cpp
glfwSetFramebufferSizeCallback(
    window,
    FramebufferSizeCallback
);
```

### Why static member functions are useful

A normal C++ member function has an implicit `this` parameter:

```cpp
void Window::Callback(...)
```

Its type is incompatible with the C-style callback expected by GLFW.

A `static` member function has no implicit `this`, so it can match the required callback signature.

---

# 4. VBO — Vertex Buffer Object

A VBO stores vertex data in GPU-accessible memory.

Typical lifecycle:

```cpp
glGenBuffers(1, &vbo);

glBindBuffer(GL_ARRAY_BUFFER, vbo);

glBufferData(
    GL_ARRAY_BUFFER,
    size,
    data,
    GL_STATIC_DRAW
);
```

### Important functions

| Function            | Purpose                                       |
| ------------------- | --------------------------------------------- |
| `glGenBuffers()`    | Creates buffer object IDs                     |
| `glBindBuffer()`    | Makes a buffer the active buffer for a target |
| `glBufferData()`    | Uploads data                                  |
| `glDeleteBuffers()` | Deletes the buffer                            |

### Unbinding

There is no `glUnbindBuffer()`.

Use:

```cpp
glBindBuffer(GL_ARRAY_BUFFER, 0);
```

`0` means no user-created buffer is currently bound to that target.

---

# 5. VAO — Vertex Array Object

A VAO stores the configuration required to interpret vertex data.

It remembers things such as:

* Vertex attribute configuration
* Which vertex attributes are enabled
* Vertex buffer association used by vertex attribute setup

Create:

```cpp
glGenVertexArrays(1, &vao);
```

Bind:

```cpp
glBindVertexArray(vao);
```

Unbind:

```cpp
glBindVertexArray(0);
```

`glBindVertexArray(0)` simply unbinds the currently bound VAO.

---

# 6. Vertex Attributes

For a vertex containing two floats:

```cpp
x, y
```

we can configure:

```cpp
vao.AddAttribute(
    0,
    2,
    GL_FLOAT,
    GL_FALSE,
    2 * sizeof(float),
    0
);
```

Internally:

```cpp
glVertexAttribPointer(
    index,
    size,
    type,
    normalized,
    stride,
    pointer
);
```

### Parameters

| Parameter    | Meaning                                    |
| ------------ | ------------------------------------------ |
| `index`      | Attribute location in the shader           |
| `size`       | Number of components                       |
| `type`       | Data type                                  |
| `normalized` | Whether integer data should be normalized  |
| `stride`     | Byte distance between consecutive vertices |
| `pointer`    | Byte offset of the attribute               |

For:

```text
x y
```

the stride is:

```cpp
2 * sizeof(float)
```

and the first attribute begins at offset:

```cpp
0
```

---

# 7. VAO + VBO Setup Order

A typical setup is:

```cpp
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
vbo.Unbind();
```

The important idea:

```text
VBO
 ↓
contains actual vertex data

VAO
 ↓
contains information about how to interpret that data
```

---

# 8. Shader Pipeline

There are two important concepts:

```text
Shader Objects
    ↓
Vertex Shader
Fragment Shader
    ↓
Compile
    ↓
Shader Program
    ↓
Link
```

### Shader object

Created using:

```cpp
glCreateShader(GL_VERTEX_SHADER);
glCreateShader(GL_FRAGMENT_SHADER);
```

Source:

```cpp
glShaderSource(...);
```

Compile:

```cpp
glCompileShader(...);
```

Check:

```cpp
glGetShaderiv(
    shader,
    GL_COMPILE_STATUS,
    &success
);
```

Error information:

```cpp
glGetShaderiv(
    shader,
    GL_INFO_LOG_LENGTH,
    &logLength
);

glGetShaderInfoLog(...);
```

---

# 9. Shader Program

Create:

```cpp
GLuint program = glCreateProgram();
```

Attach shaders:

```cpp
glAttachShader(program, vertexShader);
glAttachShader(program, fragmentShader);
```

Link:

```cpp
glLinkProgram(program);
```

Check:

```cpp
glGetProgramiv(
    program,
    GL_LINK_STATUS,
    &success
);
```

Program errors:

```cpp
glGetProgramInfoLog(...);
```

After successful linking, the temporary shader objects can be deleted:

```cpp
glDeleteShader(vertexShader);
glDeleteShader(fragmentShader);
```

The linked program remains usable.

---

# 10. Using a Shader Program

Bind:

```cpp
glUseProgram(program);
```

Through our abstraction:

```cpp
shader.Bind();
```

Unbind:

```cpp
glUseProgram(0);
```

Through our abstraction:

```cpp
shader.Unbind();
```

---

# 11. RAII

Our OpenGL resource classes use C++ RAII.

The object owns the OpenGL resource.

Example:

```text
VertexBuffer object
        ↓
owns VBO
        ↓
destructor
        ↓
glDeleteBuffers()
```

Similarly:

```text
VertexArray → glDeleteVertexArrays()
Shader      → glDeleteProgram()
Window      → glfwDestroyWindow()
```

This means resource cleanup happens automatically when the owning C++ object is destroyed.

---

# 12. Renderer

The Renderer is currently responsible for issuing high-level rendering commands.

Current interface:

```cpp
class Renderer
{
public:
    void Clear(float r, float g, float b, float a);

    void Draw(
        GLenum mode,
        GLint startIndex,
        GLsizei count
    );
};
```

### Clear

```cpp
renderer.Clear(
    0.1f,
    0.1f,
    0.1f,
    1.0f
);
```

Internally:

```cpp
glClearColor(r, g, b, a);
glClear(GL_COLOR_BUFFER_BIT);
```

### Draw

```cpp
renderer.Draw(
    GL_TRIANGLES,
    0,
    3
);
```

Internally:

```cpp
glDrawArrays(
    mode,
    startIndex,
    count
);
```

---

# 13. `glDrawArrays`

Basic form:

```cpp
glDrawArrays(
    mode,
    first,
    count
);
```

### Parameters

| Parameter | Meaning            |
| --------- | ------------------ |
| `mode`    | Primitive type     |
| `first`   | Starting vertex    |
| `count`   | Number of vertices |

Example:

```cpp
glDrawArrays(
    GL_TRIANGLES,
    0,
    3
);
```

means:

```text
Primitive → triangles
Start     → vertex 0
Count     → 3 vertices
```

Common primitive modes:

```cpp
GL_TRIANGLES
GL_LINES
GL_POINTS
GL_TRIANGLE_STRIP
```

---

# 14. Current Rendering Pipeline

Our current triangle pipeline is:

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
             │
             ↓
        OpenGL commands
```

Setup:

```text
Create VBO
    ↓
Upload vertex data
    ↓
Create VAO
    ↓
Configure vertex attributes
    ↓
Create Shader
    ↓
Compile shaders
    ↓
Link program
```

Per frame:

```text
Poll Events
     ↓
Clear
     ↓
Bind Shader
     ↓
Bind VAO
     ↓
Renderer.Draw()
     ↓
Unbind VAO
     ↓
Unbind Shader
     ↓
Swap Buffers
```

---

# 15. Current Application Loop

The main entry point is intentionally minimal:

```cpp
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

`main.cpp` is responsible only for:

* Creating the application
* Starting it
* Handling top-level exceptions

The actual application lifecycle is handled by `Application`.

---

# 16. Important Architecture Principle

Current responsibility separation:

```text
Window
→ GLFW / window / context / events

VertexBuffer
→ VBO resource

VertexArray
→ VAO + vertex attribute configuration

Shader
→ shader loading / compilation / linking / program

Renderer
→ rendering commands

Application
→ owns the current systems and coordinates the pipeline
```

The Renderer currently **does not own** the VBO, VAO, or Shader.

It simply issues rendering commands using the OpenGL state prepared by the caller.

---

# 17. Current Limitation

The current Application still knows too much about low-level rendering:

```cpp
shader.Bind();
vao.Bind();

renderer.Draw(
    GL_TRIANGLES,
    0,
    3
);
```

This is acceptable at the current stage.

As the engine grows, we want to move toward:

```text
Game
 ↓
Renderable object
 ↓
Renderer
 ↓
OpenGL
```

Potential future abstractions:

```text
Mesh
├── VertexBuffer
└── VertexArray

Material
└── Shader

Sprite / Renderable
├── Mesh
├── Material
└── Transform
```

The exact architecture will be decided before implementing these abstractions.

---

# 18. C++ Resource Ownership Warning

OpenGL handles such as:

```cpp
GLuint vbo;
GLuint vao;
GLuint shaderProgram;
```

represent resources owned by the corresponding C++ objects.

Default copying can therefore become dangerous:

```cpp
VertexBuffer a;
VertexBuffer b = a;
```

Both objects could end up believing they own the same OpenGL resource and both destructors could attempt to delete it.

This will need to be addressed as the resource architecture becomes more sophisticated.

---

# 19. Current Golden Rule

Think of the graphics system as:

```text
DATA
 ↓
VertexBuffer

INTERPRETATION
 ↓
VertexArray

PROGRAM
 ↓
Shader

COMMAND
 ↓
Renderer

ORCHESTRATION
 ↓
Application
```

This is the foundation we are building on.
