#include<glad/gl.h>
#include<GLFW/glfw3.h>
#include<imgui.h>
#include<imgui_internal.h>
#include"UI/gameUI.hpp"
#include<chrono>
#include<filesystem>
#include<fstream>
#include<iostream>
#include<thread>

void Check(bool condition, const char* message){
    if(!condition) throw std::runtime_error(message);
}

void Inside(ImGuiWindow* window, int width, int height){
    Check(window && window->Pos.x >= 0 && window->Pos.y >= 0
        && window->Pos.x + window->Size.x <= width + 1
        && window->Pos.y + window->Size.y <= height + 1, "UI window extends beyond the screen");
}

void Capture(const char* name, int width, int height){
    std::filesystem::create_directories("ui-previews");
    std::vector<unsigned char> pixels(width * height * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    std::ofstream out(std::string("ui-previews/") + name + ".ppm", std::ios::binary);
    out << "P6\n" << width << ' ' << height << "\n255\n";
    for(int y = height - 1; y >= 0; y--)
        out.write(reinterpret_cast<char*>(pixels.data() + y * width * 3), width * 3);
}

int main(int argc, char** argv){
    try{
        Check(argc == 2 && glfwInit(), "Expected test server port and GLFW context");
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        GLFWwindow* window = glfwCreateWindow(960, 640, "UI layout checks", nullptr, nullptr);
        Check(window != nullptr, "Could not create offscreen window");
        glfwMakeContextCurrent(window);
        Check(gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress)), "Could not load GL");
        {
            ClientSettings preferences;
            ControllerInput controller;
            GameUI ui(window, preferences, controller);
            MenuSettings menu;
            int width = 960, height = 640;
            auto clear = [&]{
                glfwPollEvents();
                glfwGetFramebufferSize(window, &width, &height);
                glViewport(0, 0, width, height);
                glClearColor(.025f, .03f, .04f, 1); glClear(GL_COLOR_BUFFER_BIT);
            };
            auto drawMenu = [&](GameClient* client = nullptr, VoiceChat* voice = nullptr){
                clear(); ui.DrawMenu(menu, client, false, voice); glFinish();
            };
            auto openSettings = [&]{
                drawMenu();
                ImGuiWindow* main = ImGui::FindWindowByName("GFX_2D");
                ImVec2 point(main->Pos.x + main->Size.x - 145 * preferences.uiScale + 25,
                    main->Pos.y + ImGui::GetStyle().WindowPadding.y + 10);
                ImGui::GetIO().AddMousePosEvent(point.x, point.y);
                drawMenu();
                ImGui::GetIO().AddMouseButtonEvent(0, true); drawMenu();
                ImGui::GetIO().AddMouseButtonEvent(0, false); drawMenu(); drawMenu();
                Check(ui.IsSettingsOpen(), "Settings button did not open its panel");
                Inside(ImGui::FindWindowByName("Settings"), width, height);
            };
            //Every menu page must fit at each supported scale on the minimum window.
            for(float scale : {0.85f, 1.0f, 1.2f}){
                preferences.uiScale = scale;
                for(int page = 0; page < 4; page++){
                    menu.page = page; drawMenu(); drawMenu();
                    Inside(ImGui::FindWindowByName("GFX_2D"), width, height);
                }
            }
            preferences.uiScale = 1; menu.page = 0; drawMenu();
            Capture("menu", width, height);
            openSettings();
            Capture("settings", width, height);
            preferences.uiScale = 1.2f; drawMenu(); drawMenu();
            Inside(ImGui::FindWindowByName("Settings"), width, height);
            Capture("settings-large", width, height);

            //A real lobby can start while a guest still has settings open.
            GameClient host, guest;
            auto port = static_cast<unsigned short>(std::stoi(argv[1]));
            host.Connect("127.0.0.1", port, "Exhibition host");
            guest.Connect("127.0.0.1", port, "Exhibition guest");
            auto wait = [&](auto condition){
                auto end = std::chrono::steady_clock::now() + std::chrono::seconds(6);
                while(!condition()){
                    host.Poll(); guest.Poll();
                    Check(host.GetState() != GameClient::State::Failed && guest.GetState() != GameClient::State::Failed,
                        "Test clients disconnected");
                    Check(std::chrono::steady_clock::now() < end, "UI test lobby setup timed out");
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
            };
            wait([&]{ return host.UDPReady() && guest.UDPReady(); });
            host.CreateLobby(4, 1, true);
            wait([&]{ return host.GetLobby().is_object(); });
            guest.JoinPublicLobby(host.GetLobby()["lobbyId"].get<std::uint64_t>());
            wait([&]{ return host.GetLobby()["members"].size() == 2 && guest.GetLobby().is_object(); });
            auto closeSettings = [&]{
                auto* panel = ImGui::FindWindowByName("Settings");
                ImGui::GetIO().AddMousePosEvent(panel->Pos.x + panel->Size.x - 15, panel->Pos.y + 15);
                drawMenu();
                ImGui::GetIO().AddMouseButtonEvent(0, true); drawMenu();
                ImGui::GetIO().AddMouseButtonEvent(0, false); drawMenu();
                Check(!ui.IsSettingsOpen(), "Settings close button did not dismiss the panel");
            };
            closeSettings();
            VoiceChat voice("", false);
            drawMenu(&guest, &voice); drawMenu(&guest, &voice);
            Capture("lobby-large", width, height);
            openSettings();
            host.SetReady(true); guest.SetReady(true);
            wait([&]{ for(const auto& member : host.GetLobby()["members"]) if(!member["ready"].get<bool>()) return false; return true; });
            host.StartMatch();
            wait([&]{ return host.HasSnapshot() && guest.HasSnapshot(); });
            guest.UpdateGameplay(0, {});
            Check(guest.GetGame().GetTeamCount() == 4, "HUD check did not use the four-team match");
            auto drawGame = [&]{
                clear();
                ui.Draw(guest.GetGame(), guest.GetOwnSlot(), ViewLayout::Calculate(width, height), guest.GetStatus(), &guest, &voice);
                glFinish();
            };
            drawGame(); drawGame();
            Check(!ui.IsSettingsOpen(), "Settings remained open during active gameplay");
            Check(ImGui::GetTopMostPopupModal() == nullptr, "Invisible settings modal blocked gameplay");
            auto* voiceWindow = ImGui::FindWindowByName("Voice");
            Inside(voiceWindow, width, height);
            Check(voiceWindow->Pos.y + voiceWindow->Size.y <= ViewLayout::Calculate(width, height).top,
                "Voice controls overlap the arena");
            Capture("hud-large", width, height);
            std::cout << "UI layout passed: menu pages, safe scales, settings, and active-match modal cleanup\n";
        }
        glfwDestroyWindow(window); glfwTerminate();
    }
    catch(const std::exception& error){ std::cerr << error.what() << '\n'; return 1; }
}
