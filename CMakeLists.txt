cmake_minimum_required(VERSION 3.20)

project(gfx
    VERSION 0.1
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenGL REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW REQUIRED glfw3)

add_executable(gfx
    src/main.cpp
    external/glad-generated/src/gl.c
)

target_include_directories(gfx
    PRIVATE
        ${GLFW_INCLUDE_DIRS}
        ${CMAKE_CURRENT_SOURCE_DIR}/external/glad-generated/include
)

target_link_libraries(gfx
    PRIVATE
        ${GLFW_LIBRARIES}
        OpenGL::GL
)
