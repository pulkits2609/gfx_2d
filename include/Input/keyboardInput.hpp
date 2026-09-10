#pragma once

#include<GLFW/glfw3.h>
#include<iostream>

//keeping the responsibility separate , keyboardInput doesnt care about transforms and locations
//it just knows whether or not keys are being pressed or not

class KeyboardInput{
    public:
        bool isKeyPressed(GLFWwindow* window, GLint key); //1 generic function for all the keys requested

        // void TestFunc(GLFWwindow* window); debug func
};