#include "Core/window.hpp"
#include<iostream>

Window::Window(int width, int height, const std::string& windowName):window(nullptr){
    if(!glfwInit()){
        throw std::runtime_error("Failed to initialize GLFW");
        //1 thing I learned, when we throw error, it automatically returns, no need to add return manually
    }
    else{
        std::cout<<"GLFW Initialized Successfully\n";
    }
    window = glfwCreateWindow(width, height, windowName.c_str(), nullptr, nullptr);
    if(!window){
        throw std::runtime_error("Failed to Create Window");
    }
    else{
        std::cout<<"Window Created Successfully\n";
    }

    //forgot about make Context current bruh, it should be done immediately after window creation, NOTE TO YOURSELF 
    glfwMakeContextCurrent(window);

    //initialize GLAD after GLFW
    if(!gladLoadGL(glfwGetProcAddress)){
        throw std::runtime_error("Failed to Initialize GLAD");
    }
    std::cout<<"Glad Initialized Successfully\n";

    glfwSetFramebufferSizeCallback(window, frameBuffer_size_callback);

    //setting the initial viewport
    glViewport(0, 0, width, height);
}

Window::~Window(){
    if(window){
        glfwDestroyWindow(window);
    }
    glfwTerminate();
    std::cout<<"Destroyed Window and Terminated GLFW\n";
}

bool Window::ShouldClose(){
    return glfwWindowShouldClose(window);
}

void Window::PollEvents(){
    glfwPollEvents();
}

void Window::SwapBuffers(){
    glfwSwapBuffers(window);
}

GLFWwindow* Window::GetNativeWindow(){
    return window;
}

void Window::frameBuffer_size_callback(GLFWwindow* window, int width, int height){
    glViewport(0,0, width, height);
}