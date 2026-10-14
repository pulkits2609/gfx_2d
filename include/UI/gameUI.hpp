#pragma once

#include"Game/game.hpp"
#include"UI/viewLayout.hpp"
#include<glm/glm.hpp>

struct GLFWwindow;
struct ImFont;

//draws the HUD only; health, ammo and scores still belong to Game
class GameUI{
    private:
        ImFont* regularFont = nullptr;
        ImFont* numberFont = nullptr;

    public:
        explicit GameUI(GLFWwindow* window);
        ~GameUI();
        GameUI(const GameUI&) = delete;
        GameUI& operator=(const GameUI&) = delete;

        static glm::vec3 TeamColor(int team);
        void Draw(const Game& game, std::size_t selectedPlayer,
            const ViewLayout& view, const std::string& connectionStatus);
};
