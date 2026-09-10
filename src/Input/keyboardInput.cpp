#include"Input/keyboardInput.hpp"

bool KeyboardInput::isKeyPressed(GLFWwindow* window, GLint key){
    if(glfwGetKey(window, key)==GLFW_PRESS){
        return true;
    }
    return false;
}

// void KeyboardInput::TestFunc(GLFWwindow* window){
//     bool wkey = isKeyPressedW(window);
//     bool akey = isKeyPressedA(window);
//     bool skey = isKeyPressedS(window);
//     bool dkey = isKeyPressedD(window);

//     if(wkey)std::cout<<"W Key Pressed\n";
//     if(akey)std::cout<<"A Key Pressed\n";
//     if(skey)std::cout<<"S Key Pressed\n";
//     if(dkey)std::cout<<"D Key Pressed\n";
// }