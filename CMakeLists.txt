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

FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
)

FetchContent_MakeAvailable(glm)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.9b
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC glfw OpenGL::GL ${CMAKE_DL_LIBS})

add_library(gfx_game STATIC src/Game/game.cpp)
target_include_directories(gfx_game PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(gfx_game PUBLIC glm::glm)

add_executable(gfx

    src/main.cpp

    external/glad-generated/src/gl.c

    src/Core/window.cpp
    src/Core/application.cpp

    src/Graphics/vertexBuffer.cpp
    src/Graphics/vertexArray.cpp
    src/Graphics/shader.cpp
    src/Graphics/renderer.cpp
    src/Graphics/mesh.cpp
    src/Graphics/transform.cpp
    src/Input/keyboardInput.cpp

    src/UI/gameUI.cpp
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
        glm::glm
        gfx_game
        imgui
)

# Runtime Assets

add_custom_command(TARGET gfx POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:gfx>/assets
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:gfx>/assets/licenses
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${imgui_SOURCE_DIR}/LICENSE.txt $<TARGET_FILE_DIR:gfx>/assets/licenses/imgui.txt
)

include(CTest)
if(BUILD_TESTING)
    add_executable(game_tests tests/game_tests.cpp)
    target_link_libraries(game_tests PRIVATE gfx_game)
    add_test(NAME gameplay COMMAND game_tests)
endif()
