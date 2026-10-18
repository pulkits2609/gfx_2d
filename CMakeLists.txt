set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
cmake_minimum_required(VERSION 3.20)

project(gfx
    VERSION 0.1
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(GFX_BUILD_CLIENT "Build the graphical game client" ON)
set(GFX_VOICE_RUNTIME_DIR "" CACHE PATH "Extracted realtime-voice release to bundle beside gfx")

include(FetchContent)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
)

FetchContent_MakeAvailable(glm)

add_library(gfx_game STATIC src/Game/game.cpp)
target_include_directories(gfx_game PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(gfx_game PUBLIC glm::glm)

#Boost.Asio is header-only here; do not configure or build unrelated Boost libraries.
FetchContent_Declare(
    boost_headers
    URL https://archives.boost.io/release/1.86.0/source/boost_1_86_0.tar.gz
    URL_HASH SHA256=2575e74ffc3ef1cd0babac2c1ee8bdb5782a0ee672b1912da40e5b4b591ca01f
    DOWNLOAD_DIR ${CMAKE_BINARY_DIR}/_deps
    SOURCE_SUBDIR header-only
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
)
FetchContent_MakeAvailable(boost_headers json)
find_package(Threads REQUIRED)

add_library(gfx_protocol STATIC src/Network/protocol.cpp src/Network/gameplayPackets.cpp)
target_include_directories(gfx_protocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(gfx_protocol PUBLIC nlohmann_json::nlohmann_json gfx_game)

add_library(gfx_network STATIC src/Network/gameClient.cpp src/Network/gameServer.cpp src/Network/shotPreview.cpp
    src/Network/voiceManager.cpp src/Network/voiceProcess.cpp src/Network/voiceChat.cpp)
target_include_directories(gfx_network SYSTEM PUBLIC ${boost_headers_SOURCE_DIR})
target_link_libraries(gfx_network PUBLIC gfx_protocol Threads::Threads)
target_compile_definitions(gfx_network PUBLIC BOOST_ERROR_CODE_HEADER_ONLY BOOST_SYSTEM_NO_LIB)
target_compile_definitions(gfx_network PUBLIC $<$<CONFIG:Debug>:GFX_DEBUG>)
if(WIN32)
    target_compile_definitions(gfx_network PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0601)
    target_link_libraries(gfx_network PUBLIC ws2_32 mswsock bcrypt)
endif()

add_executable(gfx_server server/main.cpp)
target_link_libraries(gfx_server PRIVATE gfx_network)
add_executable(gfx_control_client tools/control_client.cpp)
target_link_libraries(gfx_control_client PRIVATE gfx_network)

if(GFX_BUILD_CLIENT)
find_package(OpenGL REQUIRED)

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
        gfx_network
)

#Voice remains a separate application, with its own codec/audio dependencies.
#No voice headers or audio libraries are linked into gfx or gfx_server.
if(GFX_VOICE_RUNTIME_DIR)
    if(NOT EXISTS "${GFX_VOICE_RUNTIME_DIR}/voice_client${CMAKE_EXECUTABLE_SUFFIX}")
        message(FATAL_ERROR "The voice runtime does not contain voice_client for this platform")
    endif()
    add_custom_target(gfx_voice_runtime ALL
        COMMAND ${CMAKE_COMMAND}
            "-DVOICE_RUNTIME=${GFX_VOICE_RUNTIME_DIR}"
            "-DVOICE_DESTINATION=$<TARGET_FILE_DIR:gfx>/voice"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/copyVoiceRuntime.cmake
        COMMENT "Copying the separate voice runtime"
    )
    add_dependencies(gfx gfx_voice_runtime)
endif()

# Runtime Assets

add_custom_command(TARGET gfx POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:gfx>/assets
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:gfx>/assets/licenses
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${imgui_SOURCE_DIR}/LICENSE.txt $<TARGET_FILE_DIR:gfx>/assets/licenses/imgui.txt
)

endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(game_tests tests/game_tests.cpp)
    target_link_libraries(game_tests PRIVATE gfx_game)
    add_test(NAME gameplay COMMAND game_tests)
    add_executable(combat_rule_tests tests/combat_rules.cpp)
    target_link_libraries(combat_rule_tests PRIVATE gfx_game)
    add_test(NAME combat_rules COMMAND combat_rule_tests)
    add_executable(protocol_tests tests/protocol_tests.cpp)
    target_link_libraries(protocol_tests PRIVATE gfx_protocol)
    add_test(NAME protocol COMMAND protocol_tests)
    add_executable(gameplay_packet_tests tests/gameplay_packets.cpp)
    target_link_libraries(gameplay_packet_tests PRIVATE gfx_protocol)
    add_test(NAME gameplay_packets COMMAND gameplay_packet_tests)
    add_executable(shot_preview_tests tests/shot_preview.cpp)
    target_link_libraries(shot_preview_tests PRIVATE gfx_network)
    add_test(NAME shot_preview COMMAND shot_preview_tests)
    add_executable(gameplay_test_client tests/gameplay_client.cpp)
    target_link_libraries(gameplay_test_client PRIVATE gfx_network)
    add_executable(rejoin_test_client tests/rejoin_client.cpp)
    target_link_libraries(rejoin_test_client PRIVATE gfx_network)
    add_executable(voice_test_client tests/voice_client.cpp)
    target_link_libraries(voice_test_client PRIVATE gfx_network)
    add_executable(voice_process_tests tests/voice_process.cpp)
    target_link_libraries(voice_process_tests PRIVATE gfx_network)
    add_test(NAME voice_process COMMAND voice_process_tests)
    set_tests_properties(voice_process PROPERTIES TIMEOUT 25)
    set(GFX_VOICE_SERVER_EXECUTABLE "" CACHE FILEPATH "Optional voice_server for real voice integration tests")
    set(GFX_VOICE_CLIENT_EXECUTABLE "" CACHE FILEPATH "Optional voice_client for device-free integration tests")
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME debug_logging COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/debug_logging.py $<TARGET_FILE:gfx_server>
            $<IF:$<CONFIG:Debug>,1,0>)
        set_tests_properties(debug_logging PROPERTIES TIMEOUT 25)
        if(GFX_VOICE_SERVER_EXECUTABLE AND GFX_VOICE_CLIENT_EXECUTABLE)
            add_test(NAME voice_integration COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/voice_integration.py $<TARGET_FILE:gfx_server>
                ${GFX_VOICE_SERVER_EXECUTABLE} --client-driver $<TARGET_FILE:voice_test_client>
                --voice-client ${GFX_VOICE_CLIENT_EXECUTABLE})
            set_tests_properties(voice_integration PROPERTIES TIMEOUT 100 RUN_SERIAL TRUE)
        endif()
        add_test(NAME tcp_sessions COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/tcp_sessions.py
            $<TARGET_FILE:gfx_server> $<TARGET_FILE:gfx_control_client>)
        set_tests_properties(tcp_sessions PROPERTIES TIMEOUT 60)
        add_test(NAME lobbies COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/lobbies.py $<TARGET_FILE:gfx_server>)
        set_tests_properties(lobbies PROPERTIES TIMEOUT 60)
        add_test(NAME public_lobbies COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/public_lobbies.py $<TARGET_FILE:gfx_server>)
        set_tests_properties(public_lobbies PROPERTIES TIMEOUT 60)
        add_test(NAME two_clients COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/two_clients.py
            $<TARGET_FILE:gfx_server> $<TARGET_FILE:gameplay_test_client>)
        set_tests_properties(two_clients PROPERTIES TIMEOUT 35)
        foreach(tick_rate 60 180)
            add_test(NAME two_clients_${tick_rate} COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/two_clients.py
                $<TARGET_FILE:gfx_server> $<TARGET_FILE:gameplay_test_client> --tick-rate ${tick_rate})
            set_tests_properties(two_clients_${tick_rate} PROPERTIES TIMEOUT 35)
        endforeach()
        add_test(NAME tick_rate_options COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/tick_rate_options.py $<TARGET_FILE:gfx_server>)
        set_tests_properties(tick_rate_options PROPERTIES TIMEOUT 25)
        add_test(NAME udp_recovery COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/udp_recovery.py
            $<TARGET_FILE:gfx_server> $<TARGET_FILE:gameplay_test_client>)
        set_tests_properties(udp_recovery PROPERTIES TIMEOUT 55)
        add_test(NAME udp_gameplay COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/udp_gameplay.py $<TARGET_FILE:gfx_server>)
        set_tests_properties(udp_gameplay PROPERTIES TIMEOUT 30)
        add_test(NAME rejoin_sessions COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/rejoin_sessions.py $<TARGET_FILE:gfx_server>)
        set_tests_properties(rejoin_sessions PROPERTIES TIMEOUT 40)
        add_test(NAME rejoin_client COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_rejoin_client.py $<TARGET_FILE:gfx_server> $<TARGET_FILE:rejoin_test_client>)
        set_tests_properties(rejoin_client PROPERTIES TIMEOUT 30)
    endif()
endif()

#Server packages need dependency notices but no shader assets or graphics libraries.
foreach(control_target gfx_server gfx_control_client)
    add_custom_command(TARGET ${control_target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${control_target}>/licenses
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${boost_headers_SOURCE_DIR}/LICENSE_1_0.txt
            $<TARGET_FILE_DIR:${control_target}>/licenses/boost.txt
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${json_SOURCE_DIR}/LICENSE.MIT
            $<TARGET_FILE_DIR:${control_target}>/licenses/json.txt
    )
endforeach()
