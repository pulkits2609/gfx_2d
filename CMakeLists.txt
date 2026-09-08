set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
cmake_minimum_required(VERSION 3.20)

project(gfx
    VERSION 0.1
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenGL REQUIRED)

include(FetchContent)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
)

FetchContent_MakeAvailable(glfw)

FetchContent_MakeAvailable(glfw)

add_executable(gfx

    src/main.cpp

    external/glad-generated/src/gl.c

    src/Core/window.cpp
    src/Core/application.cpp

    src/Graphics/vertexBuffer.cpp
    src/Graphics/vertexArray.cpp
    src/Graphics/shader.cpp
    src/Graphics/renderer.cpp
)

target_include_directories(gfx
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/external/glad-generated/include
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(gfx
    PRIVATE
        glfw
        OpenGL::GL
)

# Runtime Assets

add_custom_command(TARGET gfx POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:gfx>/assets
)