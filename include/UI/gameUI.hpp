#pragma once

#include"Game/game.hpp"
#include"UI/viewLayout.hpp"
#include"Network/gameClient.hpp"
#include"Network/voiceChat.hpp"
#ifdef GFX_ENABLE_LEDGER_UPLOAD
#include"Network/ledgerUpload.hpp"
#endif

enum class MenuAction{ None, Offline, Create, Join, ChooseTeam, Start, Leave, Return, Disconnect, AskLeave, CancelLeave, Rejoin, Browse, JoinPublic, Ready, ChooseShape };

struct MenuSettings{
    char address[128] = "127.0.0.1";
    char name[25] = "Player 1";
    char code[7] = "";
    int port = Protocol::ControlPort;
    int teams = 2;
    int minutes = LobbyRules::DefaultMinutes;
    int chosenTeam = 0;
    int chosenShape = 0;
    std::uint64_t chosenLobby = 0;
    bool isPublic = false;
    bool ready = false;
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
        std::uint64_t previousPlayerId = 0;
        int previousHealth = MatchRules::StartingHealth;
        double damageFlashUntil = 0;
#ifdef GFX_ENABLE_LEDGER_UPLOAD
        LedgerUpload ledgerUpload;
        char ledgerAddress[128] = "127.0.0.1";
        int ledgerPort = 8000;
        std::string ledgerResultId;
        void DrawLedgerUpload(const Protocol::Json& results);
#endif
        MenuAction DrawLeaveConfirmation(const GameClient* network);
        void DrawVoice(GameClient* network, VoiceChat* voice, bool floating);
        void DrawPerformance(const GameClient* network, bool floating);
        void DrawDamageFeedback(const Player& player, const ViewLayout& view);

    public:
        explicit GameUI(GLFWwindow* window);
        ~GameUI();
        GameUI(const GameUI&) = delete;
        GameUI& operator=(const GameUI&) = delete;

        static glm::vec3 TeamColor(int team);
        void RequestLeave(bool closeGame = false){ leaveOpen = openLeavePopup = true; closingGame = closeGame; }
        bool IsLeaveOpen() const{ return leaveOpen; }
        MenuAction DrawMenu(MenuSettings& settings, GameClient* network, bool busy, VoiceChat* voice = nullptr);
        MenuAction Draw(const Game& game, std::size_t selectedPlayer,
            const ViewLayout& view, const std::string& connectionStatus, GameClient* network = nullptr, VoiceChat* voice = nullptr);
};
