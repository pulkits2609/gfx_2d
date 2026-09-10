#pragma once
#include "window.hpp"
#include "Graphics/vertexBuffer.hpp"
#include "Graphics/vertexArray.hpp"
#include "Graphics/shader.hpp"
#include "Graphics/renderer.hpp"
#include "Graphics/mesh.hpp"
#include "Graphics/transform.hpp"
#include "Input/keyboardInput.hpp"


//this will be the main application, that owns the window, renderer, game etc

class Application{
    private:
        Window window; //this should internally call its constructor
        Shader shader;
        Renderer renderer;
        TriangleMesh mesh;
        Transform transform; //OPTIMUS PRIME YEAHHHHHHH
        KeyboardInput keyboardinput;

    public:
        Application();
        void RunApplication();
        void GameLoop();
};
