#pragma once
#include "window.hpp"
#include "../Graphics/vertexBuffer.hpp"
#include "../Graphics/vertexArray.hpp"
#include "../Graphics/shader.hpp"
#include "../Graphics/renderer.hpp"

//this will be the main application, that owns the window, renderer, game etc

class Application{
    private:
        Window window; //this should internally call its constructor
        VertexBuffer vbo;
        VertexArray vao;
        Shader shader;
        Renderer renderer;

    public:
        Application();
        void RunApplication();
        void GameLoop();
};
