#pragma once
#include "window.hpp"
#include "Graphics/vertexBuffer.hpp"
#include "Graphics/vertexArray.hpp"
#include "Graphics/shader.hpp"
#include "Graphics/renderer.hpp"
#include "Graphics/mesh.hpp"
#include "Graphics/transform.hpp"
#include "Input/keyboardInput.hpp"
#include "Game/game.hpp"
#include "UI/gameUI.hpp"
#include "Network/gameClient.hpp"
#include<array>
#include<cstddef>
#include<memory>


//this will be the main application, that owns the window, renderer, game etc

class Application{
    private:
        Window window; //this should internally call its constructor
        Shader shader;
        Renderer renderer;
        std::array<Mesh, 4> meshes;
        std::array<Transform, Game::PlayerCount> transforms; //presentation transforms for the players
        KeyboardInput keyboardinput;
        Game game;
        GameUI hud;
        std::unique_ptr<GameClient> network;
        MenuSettings menu;
        MenuAction pendingAction = MenuAction::None;
        bool offlinePractice = false;
        bool escapeWasPressed = false;

        void HandleMenu(MenuAction action);

        std::size_t selectedPlayer = 0;
        double nextTitleUpdate = 0.0;
        bool reloadWasPressed = false;
        std::array<bool, Game::PlayerCount> pendingReload{};

        PlayerInput ReadInput(
            GLFWwindow* nativeWindow,
            const ViewLayout& view
        );

        void DrawRectangle(
            const glm::vec2& position,
            const glm::vec2& size,
            const glm::vec3& color,
            const glm::mat4& projection,
            float rotation = 0.0f
        );

        void DrawArena(const glm::mat4& projection);
        void DrawPlayers(const glm::mat4& projection);
        void DrawShots(const glm::mat4& projection);
        void UpdateWindowTitle();

    public:
        explicit Application(int numberOfTeams = 4, const std::string& serverHost = "",
            unsigned short controlPort = Protocol::ControlPort, const std::string& name = "Player 1");
        void RunApplication();
        void GameLoop();
};
