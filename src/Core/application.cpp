#include"../include/Core/application.hpp"

Application::Application():window(1200,1200,"GFX_2D"){}
    

void Application::RunApplication(){
    while(!window.ShouldClose()){
        window.PollEvents();
        window.SwapBuffers();
    }
}