#pragma once
#define GLFW_INCLUDE_NONE

#include<GLFW/glfw3.h>
#include<glad/gl.h>
#include<string>

//this class will contain all the window specific functionality

class Window{
    private:
        GLFWwindow* window;
    
    public:
        Window(int width, int height, const std::string& windowName);
        ~Window();

        bool ShouldClose();
        void PollEvents();
        void SwapBuffers();

        GLFWwindow* GetNativeWindow();

        static void frameBuffer_size_callback(GLFWwindow* window, int width, int height);
};