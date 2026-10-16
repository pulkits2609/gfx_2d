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

MenuAction GameUI::Draw(const Game& game, std::size_t selectedPlayer,
    const ViewLayout& view, const std::string& connectionStatus, const GameClient* network){
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

    //a small status ribbon leaves the arena clear while alive
    if((!network || !network->MatchFinished()) && (!player.IsAlive() || player.IsSpawnProtected(game.GetTime()))){
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

    std::string controls = network ? "WASD MOVE   MOUSE AIM   LMB FIRE   ESC LEAVE" : "WASD MOVE   MOUSE AIM   LMB FIRE   1-4 SWITCH";
    ImVec2 controlsSize = regularFont->CalcTextSizeA(13 * scale, 2000, 0, controls.c_str());
    draw->AddText(regularFont, 13 * scale, ImVec2((screen.x - controlsSize.x) * 0.5f, screen.y - 15 * scale), muted, controls.c_str());
    MenuAction action = MenuAction::None;
    if(network && network->MatchFinished()){
        ImGui::SetNextWindowPos(ImVec2(screen.x * 0.5f, screen.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(390, 0));
        ImGui::Begin("Match results", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove);
        const auto& results = network->GetResults();
        if(results.is_object()){
            int winner = results["winningTeam"].get<int>();
            ImGui::PushFont(numberFont);
            ImGui::TextUnformatted(winner == -1 ? "DRAW" : winner == player.team ? "YOU WIN" : "YOU LOSE");
            ImGui::PopFont();
            ImGui::TextDisabled("Time up / final server result");
            for(int team = 0; team < game.GetTeamCount(); team++)
                ImGui::Text("Team %d: %d", team + 1, results["scores"][team].get<int>());
            ImGui::Separator();
            for(const auto& entry : results["players"])
                ImGui::Text("%s   K %d / D %d", entry["name"].get_ref<const std::string&>().c_str(),
                    entry["kills"].get<int>(), entry["deaths"].get<int>());
        }
        else ImGui::TextUnformatted("Confirming the final result...");
        if(network->GetLobby()["hostId"] == network->GetPlayerId()){
            if(ImGui::Button("Return to lobby")) action = MenuAction::Return;
        }
        else ImGui::TextDisabled("Waiting for the host to return to the lobby.");
        if(ImGui::Button("Leave lobby")) action = MenuAction::Leave;
        ImGui::End();
    }
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    return action;
}

MenuAction GameUI::DrawMenu(MenuSettings& settings, const GameClient* network, bool busy){
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImVec2 screen = ImGui::GetIO().DisplaySize;
    ImGui::StyleColorsDark();
    ImGui::GetStyle().WindowRounding = 3;
    ImGui::GetStyle().FrameRounding = 2;
    ImGui::GetStyle().WindowPadding = ImVec2(24, 20);
    ImGui::GetStyle().ItemSpacing = ImVec2(10, 12);
    ImGui::SetNextWindowPos(ImVec2(screen.x * 0.5f, screen.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(680.0f, screen.x - 40), std::min(590.0f, screen.y - 40)));
    ImGui::Begin("GFX_2D", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar);
    ImGui::PushFont(numberFont);
    ImGui::TextUnformatted("GFX_2D");
    ImGui::PopFont();
    ImGui::TextDisabled("One arena. Pick a shape. Find your team.");
    ImGui::Separator();
    MenuAction action = MenuAction::None;
    bool connected = network && network->GetState() == GameClient::State::Connected;
    bool inLobby = connected && network->GetLobby().is_object();
    if(inLobby){
        const auto& lobby = network->GetLobby();
        ImGui::Text("LOBBY  %s", lobby["code"].get_ref<const std::string&>().c_str());
        ImGui::SameLine();
        if(ImGui::SmallButton("Copy code")) ImGui::SetClipboardText(lobby["code"].get_ref<const std::string&>().c_str());
        ImGui::Text("%d teams / %d minutes / %d players per team", lobby["teams"].get<int>(),
            lobby["minutes"].get<int>(), LobbyRules::PlayersPerTeam(lobby["teams"].get<int>()));
        static const char* shapes[]{"Triangle", "Square", "Circle", "Hexagon"};
        int ownTeam = 0;
        int hostTeam = 0;
        for(const auto& member : lobby["members"]){
            if(member["playerId"] == network->GetPlayerId()) ownTeam = member["team"].get<int>();
            if(member["playerId"] == lobby["hostId"]) hostTeam = member["team"].get<int>();
        }
        for(int team = 0; team < lobby["teams"].get<int>(); team++){
            glm::vec3 color = TeamColor(team);
            int count = 0;
            for(const auto& member : lobby["members"]) if(member["team"] == team) count++;
            ImGui::PushID(team);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(color.r * 0.35f, color.g * 0.35f, color.b * 0.35f, 1));
            bool full = count >= LobbyRules::PlayersPerTeam(lobby["teams"].get<int>());
            ImGui::BeginDisabled(lobby["playing"].get<bool>() || (full && ownTeam != team));
            std::string label = std::string(shapes[team]) + "  " + std::to_string(count) + "/"
                + std::to_string(LobbyRules::PlayersPerTeam(lobby["teams"].get<int>()))
                + (ownTeam == team ? "  [YOU]" : "");
            if(ImGui::Button(label.c_str(), ImVec2(235, 0))){ settings.chosenTeam = team; action = MenuAction::ChooseTeam; }
            ImGui::EndDisabled();
            ImGui::PopStyleColor();
            ImGui::SameLine();
            std::string names;
            for(const auto& member : lobby["members"]){
                if(member["team"] != team) continue;
                if(!names.empty()) names += ", ";
                names += member["name"].get<std::string>();
                if(member["playerId"] == lobby["hostId"]) names += " (host)";
            }
            ImGui::TextWrapped("%s", names.empty() ? "Empty" : names.c_str());
            ImGui::PopID();
        }
        ImGui::Separator();
        bool host = lobby["hostId"] == network->GetPlayerId();
        bool opponent = false;
        for(const auto& member : lobby["members"]) if(member["team"] != hostTeam) opponent = true;
        if(host){
            ImGui::BeginDisabled(!opponent || lobby["playing"].get<bool>());
            if(ImGui::Button("Start match", ImVec2(235, 36))) action = MenuAction::Start;
            ImGui::EndDisabled();
            if(!opponent) ImGui::TextDisabled("An opposing team needs at least one player.");
        }
        else ImGui::TextDisabled("Waiting for the host to start.");
        if(ImGui::Button("Leave lobby")) action = MenuAction::Leave;
    }
    else if(settings.page == 0){
        if(ImGui::Button("Create Lobby", ImVec2(-1, 54))) settings.page = 1;
        if(ImGui::Button("Join Lobby", ImVec2(-1, 54))) settings.page = 2;
        if(ImGui::Button("Play Offline", ImVec2(-1, 54))) action = MenuAction::Offline;
        ImGui::TextDisabled("Offline practice: keys 1-4 switch between four local players.");
    }
    else{
        ImGui::TextUnformatted(settings.page == 1 ? "CREATE LOBBY" : "JOIN LOBBY");
        ImGui::BeginDisabled(busy || connected);
        ImGui::InputText("Name", settings.name, sizeof(settings.name));
        ImGui::InputText("Server address", settings.address, sizeof(settings.address));
        ImGui::InputInt("TCP port", &settings.port);
        ImGui::EndDisabled();
        if(settings.page == 1){
            ImGui::SliderInt("Teams", &settings.teams, 2, 4, "%d", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderInt("Minutes", &settings.minutes, 1, 30, "%d", ImGuiSliderFlags_AlwaysClamp);
        }
        else ImGui::InputText("Lobby code", settings.code, sizeof(settings.code), ImGuiInputTextFlags_CharsUppercase);
        ImGui::BeginDisabled(busy);
        if(ImGui::Button(settings.page == 1 ? "Create" : "Join", ImVec2(235, 38)))
            action = settings.page == 1 ? MenuAction::Create : MenuAction::Join;
        ImGui::EndDisabled();
        if(ImGui::Button("Back")){ settings.page = 0; }
        if(connected){
            ImGui::SameLine();
            if(ImGui::Button("Disconnect")) action = MenuAction::Disconnect;
        }
        ImGui::TextWrapped("Run gfx_server first. For another computer, enter that server's LAN address. "
            "A lobby code belongs to the server you connect to.");
    }
    if(!settings.error.empty()) ImGui::TextWrapped("%s", settings.error.c_str());
    if(network){
        ImGui::Separator();
        ImGui::TextWrapped("%s", network->GetStatus().c_str());
        if(!network->GetNotice().empty()) ImGui::TextWrapped("%s", network->GetNotice().c_str());
    }
    ImGui::End();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    return action;
}
