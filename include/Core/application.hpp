#pragma once
#include "window.hpp"
#include "Graphics/vertexBuffer.hpp"
#include "Graphics/vertexArray.hpp"
#include "Graphics/shader.hpp"
#include "Graphics/renderer.hpp"
#include "Graphics/mesh.hpp"
#include "Graphics/transform.hpp"
#include "Input/keyboardInput.hpp"
#include<array>
#include<cstddef>


//this will be the main application, that owns the window, renderer, game etc

class Application{
    private:
        Window window; //this should internally call its constructor
        Shader shader;
        Renderer renderer;
        std::array<Mesh, 4> meshes;
        std::array<Transform, 4> transforms; //one position for each displayed shape
        KeyboardInput keyboardinput;

        std::size_t selectedShape = 0;
        static constexpr float movementSpeed = 0.6f; //world units per second

    public:
        Application();
        void RunApplication();
        void GameLoop();
};