#include"UI/gameUI.hpp"
#include<imgui.h>
#include<imgui_impl_glfw.h>
#include<imgui_impl_opengl3.h>
#include<algorithm>
#include<cmath>
#include<stdexcept>
#include<string>

namespace{
    ImU32 Color(const glm::vec3& color, int alpha = 255){
        return IM_COL32(static_cast<int>(color.r * 255), static_cast<int>(color.g * 255),
            static_cast<int>(color.b * 255), alpha);
    }
}

GameUI::GameUI(GLFWwindow* window){
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
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

glm::vec3 GameUI::TeamColor(int team){
    static const glm::vec3 colors[]{
        {0.94f, 0.36f, 0.29f}, {0.33f, 0.66f, 0.92f},
        {0.47f, 0.77f, 0.48f}, {0.94f, 0.73f, 0.30f}
    };
    return colors[std::clamp(team, 0, 3)];
}

void GameUI::Draw(const Game& game, std::size_t selectedPlayer,
    const ViewLayout& view, const std::string& connectionStatus){
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImDrawList* draw = ImGui::GetForegroundDrawList();
    ImVec2 screen = ImGui::GetIO().DisplaySize;
    float scale = view.scale;
    ImU32 paper = IM_COL32(233, 232, 221, 255);
    ImU32 muted = IM_COL32(148, 157, 160, 255);
    ImU32 panel = IM_COL32(18, 22, 26, 245);
    const Player& player = game.GetPlayers()[selectedPlayer];
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

    //nameplates are placed using the same camera bounds as world rendering
    for(std::size_t i = 0; i < game.GetPlayers().size(); i++){
        const Player& other = game.GetPlayers()[i];
        if(!other.IsAlive()) continue;
        float x = view.left + (other.position.x / view.halfWidth + 1.0f) * view.width * 0.5f;
        float y = view.top + (1.0f - (other.position.y + MatchRules::PlayerRadius + 0.035f)
            / view.halfHeight) * view.height * 0.5f;
        std::string label = (i == selectedPlayer ? "> " : "") + other.name;
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
    text(margin + 141 * scale, bottom + 13 * scale,
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

    //a small status ribbon leaves the arena clear while alive
    if(!player.IsAlive() || player.IsSpawnProtected(game.GetTime())){
        std::string status = "SPAWN PROTECTION";
        if(!player.IsAlive()){
            status = "RESPAWN IN " + std::to_string(static_cast<int>(std::ceil(
                std::max(0.0, player.respawnTime - game.GetTime())))) + "s";
        }
        ImVec2 size = regularFont->CalcTextSizeA(regularFont->FontSize * scale, 1000, 0, status.c_str());
        float x = (screen.x - size.x) * 0.5f;
        rectangle(x - 12 * scale, screen.y * 0.5f - 13 * scale, size.x + 24 * scale, 30 * scale, panel);
        text(x, screen.y * 0.5f - 6 * scale, status, paper);
    }

    std::string controls = "WASD MOVE   MOUSE AIM   LMB FIRE   1-4 SWITCH";
    ImVec2 controlsSize = regularFont->CalcTextSizeA(13 * scale, 2000, 0, controls.c_str());
    draw->AddText(regularFont, 13 * scale, ImVec2((screen.x - controlsSize.x) * 0.5f, screen.y - 15 * scale), muted, controls.c_str());
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
