#include"Core/application.hpp"

Application::Application():window(1200,1200,"GFX_2D"),
shader(
    "assets/shaders/vertex.glsl",
    "assets/shaders/fragment.glsl"
),
renderer(),mesh(){

}
    

void Application::RunApplication(){
    GameLoop();
}

void Application::GameLoop(){
    while(!window.ShouldClose()){
        window.PollEvents();
        renderer.Clear(0.1f, 0.1f, 0.1f, 1.0f);
        renderer.Draw(mesh, shader);
        window.SwapBuffers();
    }
}