# GLFW

## What is GLFW responsible for?

GLFW is a cross-platform library responsible for:

* Creating windows
* Creating/managing OpenGL contexts
* Handling input and window events
* Swapping front/back buffers
* Providing OpenGL function addresses to GLAD

GLFW is **not the graphics API itself**.

OpenGL is the graphics API.

---

## `GLFWwindow*` vs OpenGL Context

`GLFWwindow*` is an opaque handle/pointer to a GLFW-managed window object.

Conceptually:

```text
GLFWwindow
├── OS window information
├── GLFW state
├── input/event state
└── associated OpenGL context
```

The OpenGL context is the OpenGL state/environment in which OpenGL commands execute.

---

## `glfwMakeContextCurrent()`

```cpp
glfwMakeContextCurrent(window);
```

Makes the OpenGL context associated with `window` current on the calling CPU thread.

This must happen before loading OpenGL functions through GLAD.

Initialization sequence:

```text
glfwInit()
    ↓
glfwCreateWindow()
    ↓
glfwMakeContextCurrent(window)
    ↓
gladLoadGL(glfwGetProcAddress)
    ↓
OpenGL functions can now be used
```

---

## Why must `gladLoadGL()` happen after creating the window/context?

GLAD needs to retrieve the addresses of OpenGL functions.

```cpp
gladLoadGL(glfwGetProcAddress);
```

Internally:

```text
OpenGL context becomes current
        ↓
gladLoadGL(glfwGetProcAddress)
        ↓
GLAD asks GLFW for OpenGL function addresses
        ↓
GLAD stores the function pointers
```

Without a valid current OpenGL context, those function addresses cannot be reliably loaded.

---

## Framebuffer Resize Callback

Register:

```cpp
glfwSetFramebufferSizeCallback(window, callback);
```

This tells GLFW:

> Whenever this window's framebuffer changes size, call this function.

Callback format:

```cpp
void callback(GLFWwindow* window, int width, int height);
```

Example:

```cpp
static void frameBuffer_size_callback(
    GLFWwindow* window,
    int width,
    int height
){
    glViewport(0, 0, width, height);
}
```

`glViewport()` defines the rectangular region of the framebuffer OpenGL renders into.

Initial viewport:

```cpp
glViewport(0, 0, width, height);
```

It should normally be set once initially and then updated when the framebuffer size changes.

---

## C++ Member Functions and GLFW Callbacks

GLFW expects a normal C-style function pointer:

```cpp
void (*)(GLFWwindow*, int, int)
```

A normal C++ non-static member function has an implicit `this` pointer:

```cpp
void (Window::*)(GLFWwindow*, int, int)
```

Therefore a normal member function cannot directly be passed to GLFW.

A `static` member function has no `this` pointer, so it can match GLFW's required callback type.

Example:

```cpp
static void frameBuffer_size_callback(
    GLFWwindow* window,
    int width,
    int height
);
```

---

## `glfwPollEvents()` vs `glfwSwapBuffers()`

### `glfwPollEvents()`

```cpp
glfwPollEvents();
```

Processes pending operating-system/window events such as:

* Keyboard input
* Mouse input
* Resize events
* Window close
* Window focus
* Other GLFW events

---

### `glfwSwapBuffers()`

```cpp
glfwSwapBuffers(window);
```

Swaps the front and back buffers associated with the window.

Typical double-buffered rendering:

```text
Back Buffer
    ↓
GPU renders frame
    ↓
glfwSwapBuffers()
    ↓
Front Buffer
    ↓
Display
```

This prevents partially rendered frames from being displayed.

---

## `glfwWindowShouldClose()`

```cpp
glfwWindowShouldClose(window);
```

Checks GLFW's close flag for the window.

Typical loop:

```cpp
while(!glfwWindowShouldClose(window)){
    glfwPollEvents();

    // rendering

    glfwSwapBuffers(window);
}
```

---

## GLFW Initialization and Termination

Initialization:

```cpp
glfwInit();
```

Should happen before creating any GLFW windows.

Termination:

```cpp
glfwTerminate();
```

Should happen after GLFW-managed resources/windows have been destroyed.

Typical lifetime:

```text
glfwInit()
    ↓
Create Window
    ↓
Use Window
    ↓
glfwDestroyWindow()
    ↓
glfwTerminate()
```

---

# OpenGL Buffer Objects

## Vertex Buffer Object — VBO

A VBO stores vertex data in GPU-accessible memory.

Example CPU data:

```cpp
float vertices[] = {
     0.0f,  0.5f,
    -0.5f, -0.5f,
     0.5f, -0.5f
};
```

Typical raw OpenGL VBO sequence:

```cpp
GLuint vbo;

glGenBuffers(1, &vbo);

glBindBuffer(GL_ARRAY_BUFFER, vbo);

glBufferData(
    GL_ARRAY_BUFFER,
    sizeof(vertices),
    vertices,
    GL_STATIC_DRAW
);
```

---

## `glGenBuffers()`

Format:

```cpp
glGenBuffers(
    GLsizei n,
    GLuint* buffers
);
```

Example:

```cpp
glGenBuffers(1, &vbo);
```

Creates one buffer object name/ID.

---

## `glBindBuffer()`

Format:

```cpp
glBindBuffer(
    GLenum target,
    GLuint buffer
);
```

Example:

```cpp
glBindBuffer(GL_ARRAY_BUFFER, vbo);
```

This makes `vbo` the currently bound buffer for `GL_ARRAY_BUFFER`.

Mental model:

```text
GL_ARRAY_BUFFER
       ↓
      vbo
```

---

## Unbinding a Buffer

OpenGL does not have a function called `glUnbindBuffer()`.

Instead:

```cpp
glBindBuffer(target, 0);
```

Example:

```cpp
glBindBuffer(GL_ARRAY_BUFFER, 0);
```

`0` means no user-created buffer object is currently bound to that target.

---

## `glBufferData()`

Format:

```cpp
glBufferData(
    GLenum target,
    GLsizeiptr size,
    const void* data,
    GLenum usage
);
```

Example:

```cpp
glBufferData(
    GL_ARRAY_BUFFER,
    sizeof(vertices),
    vertices,
    GL_STATIC_DRAW
);
```

Parameters:

```text
target
    Which buffer target receives the data.

size
    Number of bytes being uploaded.

data
    Pointer to the source data.

usage
    Hint describing expected usage.
```

Example usage:

```cpp
GL_STATIC_DRAW
```

means the data is expected to be specified relatively infrequently and used many times for drawing.

---

## `glDeleteBuffers()`

Format:

```cpp
glDeleteBuffers(
    GLsizei n,
    const GLuint* buffers
);
```

Example:

```cpp
glDeleteBuffers(1, &vbo);
```

Used by the `VertexBuffer` destructor to release the OpenGL buffer.

---

# Vertex Array Object — VAO

## VBO vs VAO

```text
VBO
    Stores vertex data.

VAO
    Stores the configuration/recipe describing
    how vertex attributes are interpreted.
```

Example VBO:

```text
[x][y][x][y][x][y]
```

Corresponding VAO configuration:

```text
attribute 0
    ├── 2 components
    ├── GL_FLOAT
    ├── GL_FALSE
    ├── stride = 2 * sizeof(float)
    └── offset = 0
```

---

## `glGenVertexArrays()`

Format:

```cpp
glGenVertexArrays(
    GLsizei n,
    GLuint* arrays
);
```

Example:

```cpp
glGenVertexArrays(1, &vao);
```

---

## `glBindVertexArray()`

Format:

```cpp
glBindVertexArray(GLuint array);
```

Bind:

```cpp
glBindVertexArray(vao);
```

Unbind:

```cpp
glBindVertexArray(0);
```

Binding `0` means the user-created VAO is no longer bound.

It does **not** mean that every attribute globally becomes disabled.

---

## `glVertexAttribPointer()`

Format:

```cpp
glVertexAttribPointer(
    GLuint index,
    GLint size,
    GLenum type,
    GLboolean normalized,
    GLsizei stride,
    const void* pointer
);
```

Example:

```cpp
glVertexAttribPointer(
    0,
    2,
    GL_FLOAT,
    GL_FALSE,
    2 * sizeof(float),
    0
);
```

### Parameters

#### `index`

Which vertex shader attribute is being configured.

Example shader:

```glsl
layout(location = 0) in vec2 aPos;
```

Therefore:

```cpp
index = 0;
```

---

#### `size`

Number of components in the attribute.

```text
vec2 → 2
vec3 → 3
vec4 → 4
```

---

#### `type`

Data type of each component.

Example:

```cpp
GL_FLOAT
```

---

#### `normalized`

Mostly relevant when integer vertex data is being converted to floating-point values.

For normal floating-point position data:

```cpp
GL_FALSE
```

---

#### `stride`

Number of bytes between the beginning of one vertex's attribute and the next vertex's same attribute.

For:

```text
[x][y][x][y][x][y]
```

stride is:

```cpp
2 * sizeof(float)
```

---

#### `pointer`

When a VBO is bound, this parameter represents the byte offset of the attribute inside each vertex.

Position beginning at the first byte:

```cpp
0
```

If another attribute started after two floats, its byte offset would conceptually be:

```cpp
2 * sizeof(float)
```

passed as an offset/pointer value.

---

## `glEnableVertexAttribArray()`

```cpp
glEnableVertexAttribArray(index);
```

`glVertexAttribPointer()` configures the attribute.

`glEnableVertexAttribArray()` enables it.

Our abstraction combines both inside:

```cpp
VertexArray::AddAttribute(...)
```

---

## VertexArray API

```cpp
VertexArray vao;

vao.Bind();

vao.AddAttribute(
    index,
    size,
    type,
    normalized,
    stride,
    pointer
);

vao.Unbind();
```

Internally:

```cpp
glVertexAttribPointer(...);
glEnableVertexAttribArray(index);
```

---

## VAO + VBO Configuration Order

Important setup sequence:

```cpp
vao.Bind();
vbo.Bind();

vao.AddAttribute(...);
```

Conceptually:

```text
Bind VAO
   ↓
Bind VBO
   ↓
Describe vertex attribute
   ↓
Configuration stored as VAO state
```

Later during rendering, the VAO can be rebound without manually rebuilding the attribute layout.

---

# Shader System

## Shader Pipeline

Current shader abstraction:

```text
Read Vertex Shader
        ↓
Create Vertex Shader Object
        ↓
Compile
        ↓

Read Fragment Shader
        ↓
Create Fragment Shader Object
        ↓
Compile
        ↓

Create Shader Program
        ↓
Attach Shaders
        ↓
Link Program
        ↓
Delete Temporary Shader Objects
        ↓
Keep Shader Program
```

---

## `glCreateShader()`

Format:

```cpp
GLuint glCreateShader(GLenum shaderType);
```

Examples:

```cpp
GLuint vertexShader =
    glCreateShader(GL_VERTEX_SHADER);

GLuint fragmentShader =
    glCreateShader(GL_FRAGMENT_SHADER);
```

---

## `glShaderSource()`

Format:

```cpp
glShaderSource(
    GLuint shader,
    GLsizei count,
    const GLchar* const* string,
    const GLint* length
);
```

Typical usage:

```cpp
const GLchar* shaderString = shaderData.c_str();
GLint shaderLength =
    static_cast<GLint>(shaderData.size());

glShaderSource(
    shader,
    1,
    &shaderString,
    &shaderLength
);
```

---

## `glCompileShader()`

```cpp
glCompileShader(shader);
```

Compiles the GLSL source stored inside a shader object.

---

# Shader Compilation Checking

Check compilation:

```cpp
GLint success = 0;

glGetShaderiv(
    shader,
    GL_COMPILE_STATUS,
    &success
);
```

If compilation failed, first retrieve the required log length:

```cpp
GLint logLength;

glGetShaderiv(
    shader,
    GL_INFO_LOG_LENGTH,
    &logLength
);
```

Then retrieve the log:

```cpp
glGetShaderInfoLog(
    shader,
    logLength,
    nullptr,
    infoLog
);
```

Important distinction:

```text
Shader object
    ↓
glGetShaderiv()
glGetShaderInfoLog()
```

---

# Shader Program

## `glCreateProgram()`

```cpp
GLuint shaderProgram = glCreateProgram();
```

Creates an initially empty program object.

---

## `glAttachShader()`

```cpp
glAttachShader(shaderProgram, vertexShader);
glAttachShader(shaderProgram, fragmentShader);
```

Attaches compiled shader stages to the program.

---

## `glLinkProgram()`

```cpp
glLinkProgram(shaderProgram);
```

Links the attached shader stages into an executable OpenGL shader program.

---

# Program Link Checking

Check link status:

```cpp
GLint success = 0;

glGetProgramiv(
    shaderProgram,
    GL_LINK_STATUS,
    &success
);
```

Retrieve log length:

```cpp
GLint logLength;

glGetProgramiv(
    shaderProgram,
    GL_INFO_LOG_LENGTH,
    &logLength
);
```

Retrieve program link log:

```cpp
glGetProgramInfoLog(
    shaderProgram,
    logLength,
    nullptr,
    infoLog
);
```

Important distinction:

```text
Shader object
    ↓
glGetShaderiv()
glGetShaderInfoLog()

Program object
    ↓
glGetProgramiv()
glGetProgramInfoLog()
```

---

## Temporary Shader Objects

After the shader program has linked successfully:

```cpp
glDeleteShader(vertexShader);
glDeleteShader(fragmentShader);
```

The linked program remains valid.

The individual shader objects are no longer required.

---

## Activating a Shader Program

Bind/use:

```cpp
glUseProgram(shaderProgram);
```

Unbind:

```cpp
glUseProgram(0);
```

Our abstraction:

```cpp
shader.Bind();
shader.Unbind();
```

---

## Deleting a Shader Program

```cpp
glDeleteProgram(shaderProgram);
```

Called by the `Shader` destructor.

---

# RAII

RAII = **Resource Acquisition Is Initialization**.

The resource lifetime is tied to the C++ object's lifetime.

Example:

```text
VertexBuffer constructor
        ↓
glGenBuffers()

VertexBuffer destructor
        ↓
glDeleteBuffers()
```

Similarly:

```text
VertexArray
    Constructor → glGenVertexArrays()
    Destructor  → glDeleteVertexArrays()

Shader
    Constructor → glCreateProgram()
    Destructor  → glDeleteProgram()

Window
    Constructor → create/init window resources
    Destructor  → destroy window resources
```

This avoids having separate manual functions such as:

```text
Initialize()
Destroy()
Deinitialize()
Cleanup()
```

for every resource.

---

## `std::ifstream` and RAII

```cpp
std::ifstream file(path);
```

When `file` goes out of scope, its destructor automatically closes the file.

Therefore:

```cpp
file.close();
```

is usually unnecessary when normal scope-based destruction is sufficient.

---

# Current Graphics Resource Usage

Current setup boilerplate:

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

Current raw draw sequence:

```cpp
shader.Bind();
vao.Bind();

glDrawArrays(
    GL_TRIANGLES,
    0,
    3
);
```

Conceptually:

```text
Shader
    ↓
Which GPU program should execute?

VAO
    ↓
How should the vertex data be interpreted?

VBO
    ↓
Where is the vertex data?

Draw Call
    ↓
Render the geometry
```

---

# Important C++ Resource Ownership Note

Classes such as:

```text
VertexBuffer
VertexArray
Shader
```

own OpenGL resources.

Default C++ copying can therefore eventually become dangerous.

Example:

```cpp
VertexBuffer a(...);
VertexBuffer b = a;
```

Both objects could end up storing the same underlying OpenGL ID.

That creates an ownership problem because both destructors may attempt to release the same GPU resource.

This will later be handled using proper copy/move semantics.

Do not implement it yet unless the architecture reaches the point where copying these objects is required.
