#include"UI/gameUI.hpp"
#include"Core/buildInfo.hpp"
#include<imgui.h>
#include<imgui_impl_glfw.h>
#include<imgui_impl_opengl3.h>
#include<algorithm>
#include<cmath>
#include<cstdio>
#include<stdexcept>
#include<string>

namespace{
    void BeginInterfaceFrame(GLFWwindow* window, int controllerId, bool allowController){
        ImGui_ImplOpenGL3_NewFrame();
        ImGuiIO& io = ImGui::GetIO();
        //The GLFW backend only reads joystick 1. Feed the selected controller instead.
        io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
        ImGui_ImplGlfw_NewFrame();
        if(allowController) io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        GLFWgamepadstate state{};
        bool connected = allowController && glfwGetWindowAttrib(window, GLFW_FOCUSED) == GLFW_TRUE
            && controllerId >= 0 && glfwGetGamepadState(controllerId, &state);
        io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
        if(connected) io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
        const ImGuiKey keys[]{ImGuiKey_GamepadFaceDown, ImGuiKey_GamepadFaceRight,
            ImGuiKey_GamepadFaceLeft, ImGuiKey_GamepadFaceUp, ImGuiKey_GamepadL1,
            ImGuiKey_GamepadR1, ImGuiKey_GamepadBack, ImGuiKey_GamepadStart,
            ImGuiKey_GamepadL3, ImGuiKey_GamepadR3, ImGuiKey_GamepadDpadUp,
            ImGuiKey_GamepadDpadRight, ImGuiKey_GamepadDpadDown, ImGuiKey_GamepadDpadLeft};
        const int buttons[]{GLFW_GAMEPAD_BUTTON_A, GLFW_GAMEPAD_BUTTON_B,
            GLFW_GAMEPAD_BUTTON_X, GLFW_GAMEPAD_BUTTON_Y, GLFW_GAMEPAD_BUTTON_LEFT_BUMPER,
            GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER, GLFW_GAMEPAD_BUTTON_BACK, GLFW_GAMEPAD_BUTTON_START,
            GLFW_GAMEPAD_BUTTON_LEFT_THUMB, GLFW_GAMEPAD_BUTTON_RIGHT_THUMB,
            GLFW_GAMEPAD_BUTTON_DPAD_UP, GLFW_GAMEPAD_BUTTON_DPAD_RIGHT,
            GLFW_GAMEPAD_BUTTON_DPAD_DOWN, GLFW_GAMEPAD_BUTTON_DPAD_LEFT};
        for(int i = 0; i < 14; i++) io.AddKeyEvent(keys[i], connected && state.buttons[buttons[i]] == GLFW_PRESS);
        auto analog = [&](ImGuiKey key, int axis, float direction){
            float value = 0;
            if(connected) value = std::clamp((state.axes[axis] * direction - 0.25f) / 0.75f, 0.0f, 1.0f);
            io.AddKeyAnalogEvent(key, value > 0.1f, value);
        };
        analog(ImGuiKey_GamepadLStickLeft, GLFW_GAMEPAD_AXIS_LEFT_X, -1);
        analog(ImGuiKey_GamepadLStickRight, GLFW_GAMEPAD_AXIS_LEFT_X, 1);
        analog(ImGuiKey_GamepadLStickUp, GLFW_GAMEPAD_AXIS_LEFT_Y, -1);
        analog(ImGuiKey_GamepadLStickDown, GLFW_GAMEPAD_AXIS_LEFT_Y, 1);
        ImGui::NewFrame();
    }

    enum class PixelIcon{ Gear, Microphone, Speaker, Team };

    //Seven little rows keep the icons crisp without a font or an image dependency.
    void DrawPixelIcon(PixelIcon icon, ImVec2 position, ImU32 color, float pixel){
        static const char* pictures[][7]{
            {"0010100", "0111110", "1100011", "0101010", "1100011", "0111110", "0010100"},
            {"0011100", "0011100", "1011101", "1011101", "0111110", "0001000", "0011100"},
            {"0001000", "0011010", "1111001", "1111001", "1111001", "0011010", "0001000"},
            {"0110110", "0110110", "0000000", "1111111", "1111111", "1101011", "1101011"}
        };
        ImDrawList* draw = ImGui::GetWindowDrawList();
        for(int row = 0; row < 7; row++){
            for(int column = 0; column < 7; column++){
                if(pictures[static_cast<int>(icon)][row][column] != '1') continue;
                ImVec2 corner(std::floor(position.x + column * pixel), std::floor(position.y + row * pixel));
                draw->AddRectFilled(corner, ImVec2(corner.x + pixel, corner.y + pixel), color);
            }
        }
    }

    bool IconButton(const char* label, PixelIcon icon, bool active = false){
        ImGui::PushID(label);
        float pixel = ImGui::GetFontSize() >= 18 ? 2.0f : 1.0f;
        float iconWidth = 7 * pixel;
        ImVec2 size(ImGui::CalcTextSize(label).x + iconWidth + 18, ImGui::GetFrameHeight());
        if(active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.36f, 0.33f, 1));
        bool pressed = ImGui::Button("##icon", size);
        if(active) ImGui::PopStyleColor();
        ImVec2 corner = ImGui::GetItemRectMin();
        ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
        DrawPixelIcon(icon, ImVec2(corner.x + 6, corner.y + (size.y - iconWidth) * 0.5f), color, pixel);
        ImGui::GetWindowDrawList()->AddText(ImVec2(corner.x + iconWidth + 12,
            corner.y + (size.y - ImGui::GetFontSize()) * 0.5f), color, label);
        ImGui::PopID();
        return pressed;
    }

    constexpr double DamageFlashDuration = 0.18;
    constexpr int DamageFlashAlpha = 150;
    constexpr float DamageBorderThickness = 4.0f;
    constexpr float ShieldRingPadding = 0.012f;
    constexpr int ShieldRingSegments = 32;

    ImU32 Color(const glm::vec3& color, int alpha = 255){
        return IM_COL32(static_cast<int>(color.r * 255), static_cast<int>(color.g * 255),
            static_cast<int>(color.b * 255), alpha);
    }
}

GameUI::GameUI(GLFWwindow* window, ClientSettings& settings, ControllerInput& input)
    : nativeWindow(window), preferences(settings), controller(input){
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    ImFontConfig font;
    font.SizePixels = 16.0f;
    regularFont = ImGui::GetIO().Fonts->AddFontDefault(&font);
    font.SizePixels = 30.0f;
    numberFont = ImGui::GetIO().Fonts->AddFontDefault(&font);
    if(!ImGui_ImplGlfw_InitForOpenGL(window, true)){
        ImGui::DestroyContext();
        throw std::runtime_error("Could not initialize HUD input");
    }
    if(!ImGui_ImplOpenGL3_Init("#version 330")){
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        throw std::runtime_error("Could not initialize HUD renderer");
    }
}

GameUI::~GameUI(){
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void GameUI::ApplyTheme(){
    //Start from an unscaled style each frame so repeated settings changes never compound.
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle();
    ImGui::StyleColorsDark();
    style.WindowRounding = style.FrameRounding = style.PopupRounding = 0;
    style.GrabRounding = style.ScrollbarRounding = 0;
    style.WindowBorderSize = style.FrameBorderSize = 1;
    style.WindowPadding = ImVec2(22, 18);
    style.FramePadding = ImVec2(8, 6);
    style.ItemSpacing = ImVec2(10, 10);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.055f, 0.075f, 0.095f, 0.98f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.075f, 0.10f, 0.125f, 0.85f);
    style.Colors[ImGuiCol_PopupBg] = ImVec4(0.07f, 0.09f, 0.12f, 1);
    style.Colors[ImGuiCol_Text] = ImVec4(0.91f, 0.92f, 0.85f, 1);
    style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.57f, 0.65f, 0.68f, 1);
    style.Colors[ImGuiCol_Border] = ImVec4(0.22f, 0.32f, 0.37f, 1);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.10f, 0.15f, 0.19f, 1);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.14f, 0.24f, 0.28f, 1);
    style.Colors[ImGuiCol_Button] = ImVec4(0.13f, 0.23f, 0.28f, 1);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.20f, 0.37f, 0.40f, 1);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.20f, 0.45f, 0.40f, 1);
    style.Colors[ImGuiCol_Header] = ImVec4(0.14f, 0.27f, 0.29f, 1);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.21f, 0.38f, 0.39f, 1);
    style.Colors[ImGuiCol_CheckMark] = style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.55f, 0.82f, 0.65f, 1);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.23f, 0.26f, 1);
    style.ScaleAllSizes(preferences.uiScale);
    ImGui::GetIO().FontGlobalScale = preferences.uiScale;
}

void GameUI::DrawSettingsButton(){
    if(IconButton("Settings", PixelIcon::Gear)) settingsOpen = true;
}

void GameUI::SavePreferences(){
    preferencesSaved = preferences.Save();
}

void GameUI::DrawSettings(){
    if(!settingsOpen) return;
    ImGui::OpenPopup("Settings");
    ImVec2 screen = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(screen.x * 0.5f, screen.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(510.0f * preferences.uiScale, screen.x - 32),
        std::min(500.0f * preferences.uiScale, screen.y - 32)));
    if(ImGui::BeginPopupModal("Settings", &settingsOpen, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)){
        ImGui::TextDisabled("DISPLAY");
        ImGui::SliderFloat("UI scale", &preferences.uiScale, ClientSettings::MinimumUiScale,
            ClientSettings::MaximumUiScale, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
        if(ImGui::IsItemDeactivatedAfterEdit()) SavePreferences();
        ImGui::TextWrapped("Adjust menus and labels. The arena and gameplay dimensions stay fixed.");
        ImGui::Separator();
        ImGui::TextDisabled("CONTROLLER");
        std::string selected = "Automatic";
        for(const auto& device : controller.Devices()){
            if(device.id == preferences.controllerId) selected = device.name;
        }
        if(preferences.controllerId >= 0 && selected == "Automatic") selected = "Selected controller disconnected";
        if(ImGui::BeginCombo("Device", selected.c_str())){
            if(ImGui::Selectable("Automatic", preferences.controllerId == -1)){
                preferences.controllerId = -1;
                SavePreferences();
            }
            for(const auto& device : controller.Devices()){
                ImGui::PushID(device.id);
                ImGui::BeginDisabled(!device.mapped);
                if(ImGui::Selectable(device.name.c_str(), preferences.controllerId == device.id)){
                    preferences.controllerId = device.id;
                    SavePreferences();
                }
                ImGui::EndDisabled();
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::TextWrapped("Active: %s", controller.ActiveName().c_str());
        ImGui::TextUnformatted("Left stick: move  /  Right stick: aim");
        auto binding = [&](const char* label, int& value, int& other){
            if(ImGui::BeginCombo(label, ControllerInput::BindingName(value))){
                for(int choice = 0; choice < ControllerInput::BindingCount; choice++){
                    if(ImGui::Selectable(ControllerInput::BindingName(choice), value == choice)){
                        //Swap the other action to keep both actions usable after a remap.
                        if(other == choice) other = value;
                        value = choice;
                        SavePreferences();
                    }
                }
                ImGui::EndCombo();
            }
        };
        binding("Boost", preferences.boostBinding, preferences.fireBinding);
        binding("Fire", preferences.fireBinding, preferences.boostBinding);
        ImGui::TextWrapped("Reload: X / Square or Back / Select. Start opens the leave menu.");
        ImGui::TextDisabled("Keyboard and mouse remain available.");
        ImGui::Separator();
        if(ImGui::Button("Restore defaults")){
            preferences.uiScale = 1;
            preferences.boostBinding = 0;
            preferences.fireBinding = 1;
            preferences.controllerId = -1;
            SavePreferences();
        }
        ImGui::SameLine();
        if(ImGui::Button("Done")){
            SavePreferences();
            settingsOpen = false;
            ImGui::CloseCurrentPopup();
        }
        if(!preferencesSaved) ImGui::TextWrapped("Could not save settings. Changes apply for this session only.");
        ImGui::EndPopup();
    }
}

glm::vec3 GameUI::TeamColor(int team){
    static const glm::vec3 colors[]{
        {0.94f, 0.36f, 0.29f}, {0.33f, 0.66f, 0.92f},
        {0.47f, 0.77f, 0.48f}, {0.94f, 0.73f, 0.30f}
    };
    return colors[std::clamp(team, 0, 3)];
}

void GameUI::DrawPerformance(const GameClient* network, bool floating){
    const ImGuiIO& io = ImGui::GetIO();
    float fps = io.Framerate;
    float frameMilliseconds = 0;
    if(fps > 0) frameMilliseconds = 1000.0f / fps;
    char frames[64];
    std::snprintf(frames, sizeof(frames), "%.0f FPS  |  %.1f ms/frame", fps, frameMilliseconds);

    bool connected = network && network->GetState() == GameClient::State::Connected;
    double ping = -1;
    double updateAge = -1;
    if(connected){
        ping = network->GetPingMilliseconds();
        updateAge = network->GetSnapshotAgeMilliseconds();
    }
    std::string connection = "PING --  |  OFFLINE";
    if(connected){
        connection = "PING ... (TCP RTT)";
        if(ping >= 0) connection = "PING " + std::to_string(static_cast<int>(std::lround(ping))) + " ms (TCP RTT)";
    }
    std::string updates = "OFFLINE PRACTICE";
    if(connected){
        updates = "WAITING FOR GAMEPLAY";
        if(updateAge >= 0) updates = "UPDATE " + std::to_string(static_cast<int>(std::lround(updateAge))) + " ms";
        updates += "  |  " + std::to_string(network->GetTickRate()) + " Hz";
    }
    if(!floating){
        ImGui::TextDisabled("%s", frames);
        ImGui::TextDisabled("%s", connection.c_str());
        return;
    }

    //Use the empty top-left HUD space; never cover the arena or voice buttons.
    float scale = ViewLayout::Calculate(io.DisplaySize.x, io.DisplaySize.y).scale;
    ImVec2 corner(18 * scale, 18 * scale);
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    draw->AddRectFilled(corner, ImVec2(corner.x + 224 * scale, corner.y + 82 * scale), IM_COL32(18, 22, 26, 245));
    ImU32 muted = IM_COL32(148, 157, 160, 255);
    ImU32 warning = IM_COL32(241, 163, 102, 255);
    auto line = [&](float y, const char* text, ImU32 color, float size = 13){
        draw->AddText(regularFont, size * scale, ImVec2(corner.x + 10 * scale, corner.y + y * scale), color, text);
    };
    std::string version = "CLIENT " + std::string(BuildInfo::ClientVersion);
    line(7, version.c_str(), muted, 11);
    line(24, frames, fps < 30 ? warning : IM_COL32(233, 232, 221, 255));
    line(42, connection.c_str(), ping >= 150 ? warning : muted);
    line(60, updates.c_str(), updateAge >= 250 ? warning : muted);
}

void GameUI::DrawDamageFeedback(const Player& player, const ViewLayout& view){
    double now = ImGui::GetTime();
    bool samePlayer = player.id == previousPlayerId;
    bool healthDecreased = player.health < previousHealth;
    bool healthRestored = player.health > previousHealth;

    //Switching players and respawning are not damage. Menus reset round history.
    if(!samePlayer || healthRestored) damageFlashUntil = 0;
    if(samePlayer && healthDecreased) damageFlashUntil = now + DamageFlashDuration;
    previousPlayerId = player.id;
    previousHealth = player.health;

    if(now >= damageFlashUntil) return;
    float remaining = static_cast<float>((damageFlashUntil - now) / DamageFlashDuration);
    int alpha = static_cast<int>(DamageFlashAlpha * remaining);
    ImVec2 topLeft(view.left, view.top);
    ImVec2 bottomRight(view.left + view.width, view.top + view.height);
    //Online health comes from snapshots; predicted shots never trigger this flash.
    ImGui::GetBackgroundDrawList()->AddRect(topLeft, bottomRight, IM_COL32(242, 94, 78, alpha),
        0, 0, DamageBorderThickness * view.scale);
}

MenuAction GameUI::Draw(const Game& game, std::size_t selectedPlayer,
    const ViewLayout& view, const std::string& connectionStatus, GameClient* network, VoiceChat* voice){
    ApplyTheme();
    BeginInterfaceFrame(nativeWindow, controller.ActiveId(), (network && network->MatchFinished()) || leaveOpen);
    DrawPerformance(network, true);

    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    ImVec2 screen = ImGui::GetIO().DisplaySize;
    float scale = view.scale;
    ImU32 paper = IM_COL32(233, 232, 221, 255);
    ImU32 muted = IM_COL32(148, 157, 160, 255);
    ImU32 panel = IM_COL32(18, 22, 26, 245);
    const Player& player = game.GetPlayers()[selectedPlayer];
    DrawDamageFeedback(player, view);
    glm::vec3 teamColor = TeamColor(player.team);

    auto text = [&](float x, float y, const std::string& value, ImU32 color, bool large = false){
        ImFont* font = large ? numberFont : regularFont;
        draw->AddText(font, font->FontSize * scale, ImVec2(x, y), color, value.c_str());
    };
    auto rectangle = [&](float x, float y, float width, float height, ImU32 color){
        draw->AddRectFilled(ImVec2(x, y), ImVec2(x + width, y + height), color);
    };
    auto bar = [&](float x, float y, float width, float height, float ratio, ImU32 color){
        rectangle(x, y, width, height, IM_COL32(48, 54, 58, 255));
        rectangle(x, y, width * std::clamp(ratio, 0.0f, 1.0f), height, color);
    };

    //the scoreboard is built from the actual team count, not four fixed widgets
    int count = game.GetTeamCount();
    float cardWidth = 104.0f * scale;
    float gap = 5.0f * scale;
    float startX = (screen.x - count * cardWidth - (count - 1) * gap) * 0.5f;
    for(int team = 0; team < count; team++){
        float x = startX + team * (cardWidth + gap);
        float y = 18.0f * scale;
        glm::vec3 color = TeamColor(team);
        rectangle(x, y, cardWidth, 61.0f * scale, panel);
        rectangle(x, y, cardWidth, 3.0f * scale, Color(color));
        text(x + 10 * scale, y + 10 * scale, "TEAM " + std::to_string(team + 1), Color(color));
        text(x + 10 * scale, y + 28 * scale, std::to_string(game.GetTeamScores()[team]), paper, true);
        if(team == player.team){
            rectangle(x + cardWidth - 12 * scale, y + 14 * scale, 4 * scale, 4 * scale, paper);
        }
    }

    std::string connectionLabel = connectionStatus.substr(0, 65);
    ImVec2 connectionSize = regularFont->CalcTextSizeA(regularFont->FontSize * scale, 2000, 0, connectionLabel.c_str());
    text((screen.x - connectionSize.x) * 0.5f, 88 * scale, connectionLabel, muted);

    if(network){
        int seconds = static_cast<int>(std::ceil(network->RemainingTime()));
        std::string timer = std::to_string(seconds / 60) + ":" + (seconds % 60 < 10 ? "0" : "") + std::to_string(seconds % 60);
        ImVec2 size = regularFont->CalcTextSizeA(regularFont->FontSize * scale, 2000, 0, timer.c_str());
        text((screen.x - size.x) * 0.5f, 107 * scale, timer, paper);
    }

    //nameplates are placed using the same camera bounds as world rendering
    for(std::size_t i = 0; i < game.GetPlayers().size(); i++){
        const Player& other = game.GetPlayers()[i];
        if(!other.IsAlive()) continue;
        float x = view.left + (other.position.x / view.halfWidth + 1.0f) * view.width * 0.5f;
        bool protectedPlayer = other.IsSpawnProtected(game.GetTime());
        if(protectedPlayer){
            float centreY = view.top + (1.0f - other.position.y / view.halfHeight) * view.height * 0.5f;
            float radius = (MatchRules::PlayerRadius + ShieldRingPadding) * view.width / (2 * view.halfWidth);
            glm::vec3 shieldColor = glm::mix(TeamColor(other.team), glm::vec3(1.0f), 0.6f);
            draw->AddCircle(ImVec2(x, centreY), radius, Color(shieldColor), ShieldRingSegments, 1.5f * scale);
        }
        float y = view.top + (1.0f - (other.position.y + MatchRules::PlayerRadius + 0.035f)
            / view.halfHeight) * view.height * 0.5f;
        std::string label = (i == selectedPlayer ? "> " : "") + other.name;
        if(protectedPlayer) label += " [SHIELD]";
        ImVec2 size = regularFont->CalcTextSizeA(regularFont->FontSize * scale, 1000, 0, label.c_str());
        float left = std::clamp(x - size.x * 0.5f, 4.0f, std::max(4.0f, screen.x - size.x - 4));
        draw->AddText(regularFont, regularFont->FontSize * scale, ImVec2(left + 1, y - size.y - 5 * scale + 1),
            IM_COL32(0, 0, 0, 230), label.c_str());
        text(left, y - size.y - 5 * scale, label, i == selectedPlayer ? paper : Color(TeamColor(other.team)));
        bar(x - 23 * scale, y - 1 * scale, 46 * scale, 3 * scale,
            static_cast<float>(other.health) / MatchRules::StartingHealth, Color(TeamColor(other.team)));
    }

    float margin = 20 * scale;
    float bottom = screen.y - margin - 102 * scale;
    float leftWidth = 280 * scale;
    rectangle(margin, bottom, leftWidth, 102 * scale, panel);
    rectangle(margin, bottom, 3 * scale, 102 * scale, Color(teamColor));
    text(margin + 14 * scale, bottom + 10 * scale, player.name, paper);
    text(margin + 14 * scale, bottom + 32 * scale, std::to_string(player.health), paper, true);
    text(margin + 75 * scale, bottom + 43 * scale, "HP", muted);
    text(margin + 141 * scale, bottom + 43 * scale,
        "K " + std::to_string(player.kills) + "  D " + std::to_string(player.deaths), muted);
    bar(margin + 14 * scale, bottom + 67 * scale, 250 * scale, 4 * scale,
        static_cast<float>(player.health) / MatchRules::StartingHealth, Color(teamColor));
    text(margin + 14 * scale, bottom + 80 * scale, "SHIFT", muted);
    bar(margin + 69 * scale, bottom + 84 * scale, 195 * scale, 5 * scale,
        player.stamina / MatchRules::MaximumStamina, player.boostExhausted
            ? IM_COL32(126, 132, 137, 255) : IM_COL32(222, 190, 111, 255));

    float rightWidth = 190 * scale;
    float right = screen.x - margin - rightWidth;
    rectangle(right, bottom, rightWidth, 102 * scale, panel);
    rectangle(right + rightWidth - 3 * scale, bottom, 3 * scale, 102 * scale, Color(teamColor));
    text(right + 14 * scale, bottom + 10 * scale, "RIFLE", muted);
    text(right + 14 * scale, bottom + 32 * scale, std::to_string(player.ammunition),
        player.ammunition <= 5 ? IM_COL32(241, 137, 102, 255) : paper, true);
    text(right + 70 * scale, bottom + 43 * scale, "/ 30", muted);
    std::string reloadLabel = player.ammunition == 0 ? "EMPTY  /  R RELOAD" : "R  RELOAD";
    if(player.reloading){
        reloadLabel = "RELOADING";
        float progress = 1.0f - static_cast<float>((player.reloadEndTime - game.GetTime()) / MatchRules::ReloadDuration);
        bar(right + 14 * scale, bottom + 69 * scale, 160 * scale, 4 * scale, progress, Color(teamColor));
    }
    text(right + 14 * scale, bottom + 80 * scale, reloadLabel, muted);

    //Keep the shield countdown beside health; only respawn uses the centre.
    bool showPlayerStatus = !network || !network->MatchFinished();
    bool protectedPlayer = player.IsSpawnProtected(game.GetTime());
    if(showPlayerStatus && (!player.IsAlive() || protectedPlayer)){
        std::string status;
        if(player.IsAlive()){
            double shieldSeconds = std::max(0.0, player.protectionEndTime - game.GetTime());
            status = "SHIELD " + std::to_string(static_cast<int>(std::ceil(shieldSeconds))) + "s  /  FIRE ENABLED";
        }
        else{
            status = "RESPAWN IN " + std::to_string(static_cast<int>(std::ceil(
                std::max(0.0, player.respawnTime - game.GetTime())))) + "s";
        }
        if(player.IsAlive()){
            rectangle(margin, bottom - 24 * scale, leftWidth, 20 * scale, panel);
            text(margin + 14 * scale, bottom - 22 * scale, status, paper);
        }
        else{
            ImVec2 size = regularFont->CalcTextSizeA(regularFont->FontSize * scale, 1000, 0, status.c_str());
            float x = (screen.x - size.x) * 0.5f;
            rectangle(x - 12 * scale, screen.y * 0.5f - 13 * scale, size.x + 24 * scale, 30 * scale, panel);
            text(x, screen.y * 0.5f - 6 * scale, status, paper);
        }
    }

    std::string controls = network ? "WASD MOVE   MOUSE AIM   LMB FIRE   ESC LEAVE" : "WASD MOVE   MOUSE AIM   LMB FIRE   1-4 SWITCH";
    if(controller.ActiveId() >= 0) controls = "LEFT STICK MOVE   RIGHT STICK AIM   BACK RELOAD   START MENU";
    ImVec2 controlsSize = regularFont->CalcTextSizeA(13 * scale, 2000, 0, controls.c_str());
    draw->AddText(regularFont, 13 * scale, ImVec2((screen.x - controlsSize.x) * 0.5f, screen.y - 15 * scale), muted, controls.c_str());
    MenuAction action = MenuAction::None;
#ifdef GFX_ENABLE_LEDGER_UPLOAD
    ledgerUpload.Poll();
    if(!network || !network->MatchFinished()){
        ledgerUpload.Reset();
        ledgerResultId.clear();
    }
#endif
    if(network && network->MatchFinished()){
        ImGui::SetNextWindowPos(ImVec2(screen.x * 0.5f, screen.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        //A fixed height keeps the result buttons reachable on the smallest window.
        ImGui::SetNextWindowSize(ImVec2(std::min(470.0f * preferences.uiScale, screen.x - 40), std::min(540.0f * preferences.uiScale, screen.y - 80)));
        ImGui::Begin("Match results", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove);
        DrawSettingsButton();
        ImGui::Separator();
        const auto& results = network->GetResults();
        if(results.is_object()){
            int winner = results["winningTeam"].get<int>();
            ImGui::PushFont(numberFont);
            ImGui::TextUnformatted(winner == -1 ? "DRAW" : winner == player.team ? "YOU WIN" : "YOU LOSE");
            ImGui::PopFont();
            ImGui::TextDisabled("Time up / final server result");
            for(int team = 0; team < game.GetTeamCount(); team++)
                ImGui::TextColored(ImVec4(TeamColor(team).r, TeamColor(team).g, TeamColor(team).b, 1),
                    "Team %d: %d", team + 1, results["scores"][team].get<int>());
            ImGui::Separator();
            for(const auto& entry : results["players"])
                ImGui::Text("%s   K %d / D %d", entry["name"].get_ref<const std::string&>().c_str(),
                    entry["kills"].get<int>(), entry["deaths"].get<int>());
        }
        else ImGui::TextUnformatted("Confirming the final result...");
        if(network->GetLobby()["hostId"] == network->GetPlayerId()){
            if(ImGui::Button("Return to lobby")) action = MenuAction::Return;
#ifdef GFX_ENABLE_LEDGER_UPLOAD
            if(results.is_object()) DrawLedgerUpload(results);
#endif
        }
        else ImGui::TextDisabled("Waiting for the host to return to the lobby.");
        if(ImGui::Button("Leave lobby")) action = MenuAction::AskLeave;
        ImGui::End();
    }
    if(network) DrawVoice(network, voice, true);
    if(network && network->MatchFinished()) DrawSettings();
    else{
        settingsOpen = false;
        //A host can start while another player has Settings open in the lobby.
        if(ImGui::BeginPopupModal("Settings", nullptr, ImGuiWindowFlags_NoResize)){
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }
    MenuAction confirmation = DrawLeaveConfirmation(network);
    if(confirmation != MenuAction::None) action = confirmation;
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    return action;
}

MenuAction GameUI::DrawMenu(MenuSettings& settings, GameClient* network, bool busy, VoiceChat* voice){
    //A new game may reuse player IDs, so do not carry a previous damage flash into it.
    previousPlayerId = 0;
    damageFlashUntil = 0;
#ifdef GFX_ENABLE_LEDGER_UPLOAD
    ledgerUpload.Reset();
    ledgerResultId.clear();
#endif
    ApplyTheme();
    BeginInterfaceFrame(nativeWindow, controller.ActiveId(), true);
    ImVec2 screen = ImGui::GetIO().DisplaySize;
    float uiScale = preferences.uiScale;
    ImGui::SetNextWindowPos(ImVec2(screen.x * 0.5f, screen.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(680.0f * uiScale, screen.x - 40), std::min(720.0f * uiScale, screen.y - 40)));
    ImGui::Begin("GFX_2D", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar);
    ImGui::PushFont(numberFont);
    ImGui::TextUnformatted("GFX_2D");
    ImGui::PopFont();
    ImGui::SameLine(ImGui::GetWindowWidth() - 145 * uiScale);
    DrawSettingsButton();
    ImGui::TextDisabled("CLIENT %s", BuildInfo::ClientVersion);
    ImGui::TextDisabled("One arena. Pick a shape. Find your team.");
    ImGui::Separator();
    MenuAction action = MenuAction::None;
    bool connected = network && network->GetState() == GameClient::State::Connected;
    bool inLobby = connected && network->GetLobby().is_object();
    if(connected) ImGui::TextDisabled("SERVER  %s", network->GetServerAddress().c_str());
    if(inLobby){
        DrawVoice(network, voice, false);
        const auto& lobby = network->GetLobby();
        bool host = lobby["hostId"] == network->GetPlayerId();
        bool playing = lobby["playing"].get<bool>();
        if(lobby["public"].get<bool>()) ImGui::TextUnformatted("PUBLIC LOBBY / no join code");
        else{
            ImGui::Text("PRIVATE LOBBY  %s", lobby["code"].get_ref<const std::string&>().c_str());
            ImGui::SameLine();
            if(ImGui::SmallButton("Copy code")) ImGui::SetClipboardText(lobby["code"].get_ref<const std::string&>().c_str());
        }
        ImGui::Text("%d teams / %d minutes / %d players per team", lobby["teams"].get<int>(),
            lobby["minutes"].get<int>(), LobbyRules::PlayersPerTeam(lobby["teams"].get<int>()));
        static const char* shapes[]{"Triangle", "Square", "Circle", "Hexagon"};
        int ownTeam = 0;
        int hostTeam = 0;
        bool ownReady = false, allReady = true;
        for(const auto& member : lobby["members"]){
            if(member["playerId"] == network->GetPlayerId()){
                ownTeam = member["team"].get<int>();
                ownReady = member["ready"].get<bool>();
            }
            allReady = allReady && member["ready"].get<bool>();
            if(member["playerId"] == lobby["hostId"]) hostTeam = member["team"].get<int>();
        }
        //Keep match controls visible even with a full roster or a smaller window.
        float rosterHeight = std::clamp(ImGui::GetContentRegionAvail().y - 240.0f, 130.0f, 270.0f);
        ImGui::BeginChild("Team roster", ImVec2(0, rosterHeight), ImGuiChildFlags_Borders);
        if(ImGui::BeginTable("Teams", 3, ImGuiTableFlags_SizingStretchProp)){
            ImGui::TableSetupColumn("Team", ImGuiTableColumnFlags_WidthFixed, 210);
            ImGui::TableSetupColumn("Shape", ImGuiTableColumnFlags_WidthFixed, 110);
            ImGui::TableSetupColumn("Players");
            ImGui::TableHeadersRow();
            for(int team = 0; team < lobby["teams"].get<int>(); team++){
                glm::vec3 color = TeamColor(team);
                int count = 0;
                for(const auto& member : lobby["members"]) if(member["team"] == team) count++;
                ImGui::PushID(team);
                ImGui::TableNextRow(0, 44 * uiScale);
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, Color(color, 18));
                ImGui::TableNextColumn();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(color.r * 0.35f, color.g * 0.35f, color.b * 0.35f, 1));
                bool full = count >= LobbyRules::PlayersPerTeam(lobby["teams"].get<int>());
                ImGui::BeginDisabled(playing || (full && ownTeam != team));
                std::string label = "Team " + std::to_string(team + 1) + "  " + std::to_string(count) + "/"
                    + std::to_string(LobbyRules::PlayersPerTeam(lobby["teams"].get<int>()))
                    + (ownTeam == team ? "  [YOU]" : "");
                if(ImGui::Button(label.c_str(), ImVec2(-1, 32))){ settings.chosenTeam = team; action = MenuAction::ChooseTeam; }
                ImGui::EndDisabled();
                ImGui::PopStyleColor();
                ImGui::TableNextColumn();
                int shape = lobby["shapes"][team].get<int>();
                if(host){
                    ImGui::BeginDisabled(playing);
                    ImGui::SetNextItemWidth(-1);
                    if(ImGui::Combo("##shape", &shape, shapes, 4)){
                        settings.chosenTeam = team;
                        settings.chosenShape = shape;
                        action = MenuAction::ChooseShape;
                    }
                    ImGui::EndDisabled();
                }
                else ImGui::TextUnformatted(shapes[shape]);
                ImGui::TableNextColumn();
                for(const auto& member : lobby["members"]){
                    if(member["team"] != team) continue;
                    ImGui::TextWrapped("%s%s  [%s]", member["name"].get_ref<const std::string&>().c_str(),
                        member["playerId"] == lobby["hostId"] ? " (host)" : "",
                        member["ready"].get<bool>() ? "ready" : "waiting");
                }
                if(count == 0) ImGui::TextDisabled("Empty");
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();
        ImGui::TextDisabled("Team or shape changes reset everyone's ready state.");
        ImGui::Separator();
        ImGui::BeginDisabled(playing || !network->UDPReady());
        if(ImGui::Checkbox("I'm ready", &ownReady)){
            settings.ready = ownReady;
            action = MenuAction::Ready;
        }
        ImGui::EndDisabled();
        bool opponent = false;
        for(const auto& member : lobby["members"]) if(member["team"] != hostTeam) opponent = true;
        if(host){
            ImGui::BeginDisabled(!opponent || !allReady || playing || !network->UDPReady());
            if(ImGui::Button("Start match", ImVec2(235, 36))) action = MenuAction::Start;
            ImGui::EndDisabled();
            if(!opponent) ImGui::TextDisabled("An opposing team needs at least one player.");
            else if(!allReady) ImGui::TextDisabled("Waiting for everyone to ready up, including you.");
        }
        else ImGui::TextDisabled(playing ? "Entering the match..." : "Ready up, then wait for the host to start.");
        if(ImGui::Button("Leave lobby")) action = lobby["playing"].get<bool>() ? MenuAction::AskLeave : MenuAction::Leave;
    }
    else if(settings.page == 0){
        if(network && network->RejoinSecondsLeft() > 0){
            std::string label = "Rejoin Match  (" + std::to_string(static_cast<int>(std::ceil(network->RejoinSecondsLeft()))) + "s)";
            if(ImGui::Button(label.c_str(), ImVec2(-1, 54))) action = MenuAction::Rejoin;
            ImGui::TextWrapped("Return to your original team through respawn. Rejoining closes when fewer than 2 minutes remain.");
            ImGui::Separator();
        }
        auto homeButton = [&](const char* label, const char* hint, int colorIndex){
            ImGui::PushID(label);
            bool pressed = ImGui::Button("##home", ImVec2(-1, 60));
            ImVec2 corner = ImGui::GetItemRectMin();
            ImVec2 end = ImGui::GetItemRectMax();
            ImDrawList* draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(corner, ImVec2(corner.x + 4, end.y), Color(TeamColor(colorIndex)));
            draw->AddText(ImVec2(corner.x + 18, corner.y + 9), ImGui::GetColorU32(ImGuiCol_Text), label);
            draw->AddText(regularFont, ImGui::GetFontSize() * 0.75f,
                ImVec2(corner.x + 18, corner.y + 34), ImGui::GetColorU32(ImGuiCol_TextDisabled), hint);
            ImGui::PopID();
            return pressed;
        };
        if(homeButton("Create Lobby", "Host a match with your own rules", 0)) settings.page = 1;
        if(homeButton("Browse Public Lobbies", "Find a waiting match on your server", 1)){
            settings.page = 3;
            if(connected) action = MenuAction::Browse;
        }
        if(homeButton("Join Private Lobby", "Enter a code shared by your host", 2)) settings.page = 2;
        if(homeButton("Play Offline", "Try movement, aiming and combat locally", 3)) action = MenuAction::Offline;
        ImGui::TextDisabled("Offline practice: keys 1-4 switch between four local players.");
    }
    else{
        ImGui::TextUnformatted(settings.page == 1 ? "CREATE LOBBY" : settings.page == 2 ? "JOIN PRIVATE LOBBY" : "PUBLIC LOBBIES");
        if(!connected){
            ImGui::BeginDisabled(busy);
            ImGui::InputText("Name", settings.name, sizeof(settings.name));
            ImGui::InputText("Server address", settings.address, sizeof(settings.address));
            ImGui::InputInt("TCP port", &settings.port);
            ImGui::EndDisabled();
        }
        if(settings.page == 1){
            ImGui::SliderInt("Teams", &settings.teams, 2, 4, "%d", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderInt("Minutes", &settings.minutes, 1, 30, "%d", ImGuiSliderFlags_AlwaysClamp);
            if(ImGui::RadioButton("Private / join code", !settings.isPublic)) settings.isPublic = false;
            ImGui::SameLine();
            if(ImGui::RadioButton("Public / server listing", settings.isPublic)) settings.isPublic = true;
            ImGui::TextWrapped(settings.isPublic ? "Anyone browsing this server can join before the match starts. No code is created."
                : "Share the server address, port and private code with your friends.");
        }
        else if(settings.page == 2) ImGui::InputText("Lobby code", settings.code, sizeof(settings.code), ImGuiInputTextFlags_CharsUppercase);
        ImGui::BeginDisabled(busy);
        const char* button = settings.page == 1 ? "Create" : settings.page == 2 ? "Join" : connected ? "Refresh listings" : "Connect and fetch listings";
        if(ImGui::Button(button, ImVec2(270, 38)))
            action = settings.page == 1 ? MenuAction::Create : settings.page == 2 ? MenuAction::Join : MenuAction::Browse;
        ImGui::EndDisabled();
        if(settings.page == 3){
            ImGui::TextWrapped("Listings belong only to the server above. Disconnect to browse a different address or port.");
            //The list scrolls inside its own area, leaving Back and Disconnect reachable.
            ImGui::BeginChild("Public rooms", ImVec2(0, std::max(100.0f, ImGui::GetContentRegionAvail().y - 145)), ImGuiChildFlags_Borders);
            if(!connected) ImGui::TextWrapped(busy ? "Connecting to the selected server..." : "Enter a server address and port, then fetch its public lobbies.");
            else if(!network->HasLobbyList()) ImGui::TextUnformatted("Fetching public lobbies...");
            else if(network->GetPublicLobbies().empty()) ImGui::TextWrapped("No public lobbies are waiting on this server. Refresh or create one.");
            else for(const auto& room : network->GetPublicLobbies()){
                std::string rowId = std::to_string(room["lobbyId"].get<std::uint64_t>());
                ImGui::PushID(rowId.c_str());
                bool full = room["players"] == room["capacity"];
                ImGui::BeginDisabled(full || busy);
                if(ImGui::Button(full ? "Full" : "Join", ImVec2(65, 34))){
                    settings.chosenLobby = room["lobbyId"].get<std::uint64_t>();
                    action = MenuAction::JoinPublic;
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::Text("%s's lobby", room["host"].get_ref<const std::string&>().c_str());
                ImGui::TextDisabled("%d/%d players  /  %d teams  /  %d min", room["players"].get<int>(),
                    room["capacity"].get<int>(), room["teams"].get<int>(), room["minutes"].get<int>());
                ImGui::EndGroup();
                ImGui::Separator();
                ImGui::PopID();
            }
            ImGui::EndChild();
        }
        if(ImGui::Button(busy ? "Cancel" : "Back")){
            settings.page = 0;
            if(busy) action = MenuAction::Disconnect;
        }
        if(connected){
            ImGui::SameLine();
            if(ImGui::Button("Disconnect / change server")) action = MenuAction::Disconnect;
        }
        if(settings.page != 3) ImGui::TextWrapped("Run gfx_server first. For another computer, enter that server's LAN address. "
            "Lobbies and their private codes belong to that server only.");
    }
    if(!settings.error.empty()){
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.58f, 0.43f, 1));
        ImGui::TextWrapped("%s", settings.error.c_str());
        ImGui::PopStyleColor();
    }
    if(network){
        ImGui::Separator();
        ImGui::TextWrapped("%s", network->GetStatus().c_str());
        if(!network->GetNotice().empty()) ImGui::TextWrapped("%s", network->GetNotice().c_str());
    }
    if(!inLobby) DrawPerformance(network, false);
    ImGui::End();
    DrawSettings();
    MenuAction confirmation = DrawLeaveConfirmation(network);
    if(confirmation != MenuAction::None) action = confirmation;
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    return action;
}

void GameUI::DrawVoice(GameClient* network, VoiceChat* voice, bool floating){
    if(!voice || !network) return;
    if(floating){
        ImVec2 screen = ImGui::GetIO().DisplaySize;
        const float scale = ViewLayout::Calculate(screen.x, screen.y).scale;
        ImGui::SetNextWindowPos(ImVec2(screen.x - 12 * scale, 12 * scale), ImGuiCond_Always, ImVec2(1, 0));
        ImGui::SetNextWindowSize(ImVec2(246 * scale, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 6));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(5, 4));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
        ImGui::Begin("Voice", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
        //The match HUD must stay inside its reserved strip even at maximum menu scale.
        ImGui::SetWindowFontScale(std::clamp(scale, 0.8f, 1.0f) / preferences.uiScale);
    }
    else{ ImGui::TextDisabled("VOICE"); ImGui::SameLine(); }
    ImGui::BeginDisabled(!voice->Connected());
    if(IconButton(voice->MicrophoneMuted() ? "Mic OFF" : "Mic ON", PixelIcon::Microphone, !voice->MicrophoneMuted()))
        voice->SetMicrophone(!voice->MicrophoneMuted());
    ImGui::SameLine();
    if(IconButton("All", PixelIcon::Speaker, !voice->TransmittingTeam())) voice->SelectTransmit(*network, false);
    ImGui::SameLine();
    ImGui::BeginDisabled(!voice->TeamEnabled());
    if(IconButton("Team", PixelIcon::Team, voice->TransmittingTeam())) voice->SelectTransmit(*network, true);
    ImGui::EndDisabled();
    if(!floating) ImGui::SameLine();
    ImGui::TextDisabled("Hear"); ImGui::SameLine();
    if(IconButton(voice->AllMuted() ? "All OFF" : "All ON", PixelIcon::Speaker, !voice->AllMuted())) voice->SetIncomingAll(!voice->AllMuted());
    ImGui::SameLine();
    if(IconButton(voice->TeamMuted() ? "Team OFF" : "Team ON", PixelIcon::Team, !voice->TeamMuted())) voice->SetIncomingTeam(!voice->TeamMuted());
    ImGui::EndDisabled();
    if(!voice->Connected() && voice->Available()){
        if(ImGui::SmallButton("Retry voice")) voice->Retry();
        ImGui::SameLine();
    }
    if(floating){
        const std::string label = voice->Connected() ? voice->Status() : "Voice not connected";
        ImGui::TextDisabled("%s", label.c_str());
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s", voice->Status().c_str());
        ImGui::End();
        ImGui::PopStyleVar(3);
    }
    else{
        ImGui::TextWrapped("%s", voice->Status().c_str());
        ImGui::Separator();
    }
}

MenuAction GameUI::DrawLeaveConfirmation(const GameClient* network){
    if(!network || !network->GetLobby().is_object()){
        leaveOpen = openLeavePopup = false;
        return MenuAction::None;
    }
    if(openLeavePopup){ ImGui::OpenPopup("Leave match?"); openLeavePopup = false; }
    ImVec2 screen = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(screen.x * 0.5f, screen.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(470.0f, screen.x - 40), 0));
    MenuAction action = MenuAction::None;
    if(ImGui::BeginPopupModal("Leave match?", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)){
        bool host = network->GetLobby()["hostId"] == network->GetPlayerId();
        if(host){
            ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.4f, 1), "YOU ARE THE HOST");
            ImGui::TextWrapped("Leaving will stop the match for everyone and close the lobby.");
        }
        else if(closingGame){
            ImGui::TextWrapped("Close the game and leave this match?");
            ImGui::TextWrapped("Rejoining is available only while this app stays open. To keep that option, cancel and leave using Escape instead.");
        }
        else if(!network->MatchFinished()){
            ImGui::TextWrapped("Leave this match and return to the home screen?");
            ImGui::TextWrapped("You have 60 seconds to rejoin, provided at least 2 minutes remain. You return through respawn.");
        }
        else ImGui::TextWrapped("Leave the results and return to the home screen?");
        ImGui::TextWrapped("The match keeps running while this dialog is open.");
        ImGui::Separator();
        if(ImGui::Button("Keep playing", ImVec2(170, 34))){
            leaveOpen = false; action = MenuAction::CancelLeave; ImGui::CloseCurrentPopup();
        }
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if(ImGui::Button(host ? "End match and leave" : closingGame ? "Close game" : "Leave match", ImVec2(210, 34))){
            leaveOpen = false; action = MenuAction::Leave; ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    return action;
}

#ifdef GFX_ENABLE_LEDGER_UPLOAD
void GameUI::DrawLedgerUpload(const Protocol::Json& results){
    std::string resultId;
    if(results.contains("resultId") && results["resultId"].is_string())
        resultId = results["resultId"].get<std::string>();
    if(resultId != ledgerResultId){
        ledgerUpload.Reset();
        ledgerResultId = resultId;
    }
    if(!ImGui::CollapsingHeader("Upload results to ledger (optional)")) return;
    ImGui::TextWrapped("Enter the separate ledger server's address and port. Returning to the lobby cancels a pending request.");
    bool pending = ledgerUpload.GetState() == LedgerUpload::State::Pending;
    ImGui::BeginDisabled(pending);
    ImGui::InputText("Address", ledgerAddress, sizeof(ledgerAddress));
    ImGui::InputInt("Port", &ledgerPort);
    if(ImGui::Button("Upload results")) ledgerUpload.Start(ledgerAddress, ledgerPort, results);
    ImGui::EndDisabled();
    ImGui::TextWrapped("%s", ledgerUpload.GetStatusText().c_str());
    if(!ledgerUpload.GetHash().empty()){
        ImGui::TextWrapped("Record hash: %s", ledgerUpload.GetHash().c_str());
        if(ImGui::SmallButton("Copy record hash")) ImGui::SetClipboardText(ledgerUpload.GetHash().c_str());
    }
}
#endif
