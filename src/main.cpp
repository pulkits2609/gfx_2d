#include <iostream>
#include <cmath>
#include "../include/Core/application.hpp"

void frameBuffer_size_callback(GLFWwindow* window, int width, int height){
    glViewport(0,0, width, height);
}

int main(){

    try{
        Application app;
        app.RunApplication();
    }
    catch(const std::exception& e){
        std::cerr<<e.what()<<"\n";
    }
    return 0;
}