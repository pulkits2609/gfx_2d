#include"Core/application.hpp"

Application::Application():window(1200,1200,"GFX_2D Move Triangle Using WASD , more FPS = faster movement"),
shader(
    "assets/shaders/vertex.glsl",
    "assets/shaders/fragment.glsl"
),
renderer(),mesh(),transform(),keyboardinput(){

}
    

void Application::RunApplication(){
    GameLoop();
}

void Application::GameLoop(){
    while(!window.ShouldClose()){
        window.PollEvents();
        bool wPressed = keyboardinput.isKeyPressed(window.GetNativeWindow(),GLFW_KEY_W);
        bool aPressed = keyboardinput.isKeyPressed(window.GetNativeWindow(),GLFW_KEY_A);
        bool sPressed = keyboardinput.isKeyPressed(window.GetNativeWindow(),GLFW_KEY_S);
        bool dPressed = keyboardinput.isKeyPressed(window.GetNativeWindow(),GLFW_KEY_D);
        // if(wPressed){
        //     transform.Translate(0.0f,1.0f);
        // }
        // if(aPressed){
        //     transform.Translate(-1.0f,0.0f);
        // }
        // if(sPressed){
        //     transform.Translate(0.0f,-1.0f);
        // }
        // if(dPressed){
        //     transform.Translate(1.0f,0.0f);
        // }
        if(wPressed){
            transform.Translate(0.0f, 0.01f);
        }
        if(aPressed){
            transform.Translate(-0.01f, 0.0f);
        }
        if(sPressed){
            transform.Translate(0.0f, -0.01f);
        }
        if(dPressed){
            transform.Translate(0.01f, 0.0f);
        }
        //this will be changed later on by adding delta time , instead of currently relying on FPS

        //rendering is done after the input
        renderer.Clear(0.1f, 0.1f, 0.1f, 1.0f);
        renderer.Draw(mesh, shader, transform);
        window.SwapBuffers();
    }
}