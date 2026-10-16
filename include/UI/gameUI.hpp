#pragma once

#include"Game/game.hpp"
#include"UI/viewLayout.hpp"
#include"Network/gameClient.hpp"

enum class MenuAction{ None, Offline, Create, Join, ChooseTeam, Start, Leave, Return, Disconnect, AskLeave, CancelLeave, Rejoin };

struct MenuSettings{
    char address[128] = "127.0.0.1";
    char name[25] = "Player 1";
    char code[7] = "";
    int port = Protocol::ControlPort;
    int teams = 2;
    int minutes = LobbyRules::DefaultMinutes;
    int chosenTeam = 0;
    int page = 0;
    std::string error;
};
#include<glm/glm.hpp>

struct GLFWwindow;
struct ImFont;

//draws the HUD only; health, ammo and scores still belong to Game
class GameUI{
    private:
        ImFont* regularFont = nullptr;
        ImFont* numberFont = nullptr;
        bool leaveOpen = false;
        bool openLeavePopup = false;
        bool closingGame = false;
        MenuAction DrawLeaveConfirmation(const GameClient* network);

    public:
        explicit GameUI(GLFWwindow* window);
        ~GameUI();
        GameUI(const GameUI&) = delete;
        GameUI& operator=(const GameUI&) = delete;

        static glm::vec3 TeamColor(int team);
        void RequestLeave(bool closeGame = false){ leaveOpen = openLeavePopup = true; closingGame = closeGame; }
        bool IsLeaveOpen() const{ return leaveOpen; }
        MenuAction DrawMenu(MenuSettings& settings, const GameClient* network, bool busy);
        MenuAction Draw(const Game& game, std::size_t selectedPlayer,
            const ViewLayout& view, const std::string& connectionStatus, const GameClient* network = nullptr);
};
