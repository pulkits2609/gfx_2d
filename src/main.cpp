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

    // // glClearColor(1.0f, 1.0f, 1.0f, 1.0f); //this changes the state of OPENGL 
    // // // int cnt = 0;

    // // glClear(GL_COLOR_BUFFER_BIT); //this actually changes the color of the window based on the new state!

    // GLuint vbo;
    // glGenBuffers(1, &vbo);
    // glBindBuffer(GL_ARRAY_BUFFER, vbo);

    // std::cout<<"VBO : "<<vbo<<"\n";
    // float vertices[] = {
    //     0.0f, 0.5f,
    //     -0.5f, -0.5f,
    //     0.5f, -0.5f
    // };

    // glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // GLuint vao;
    // glGenVertexArrays(1,&vao);
    // glBindVertexArray(vao);
    // glVertexAttribPointer(
    //     0, //attribute index
    //     2, //components
    //     GL_FLOAT, // type
    //     GL_FALSE, //normalized ?
    //     2* sizeof(float), //stride
    //     0 // offset
    // );

    // glEnableVertexAttribArray(0);

    // GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    // GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);

    // //trying to load shaders
    // // std::cout<<readFile("src/fragment.glsl");

    // // glShaderSource(
    // //     shader,
    // //     count,
    // //     string,
    // //     length
    // // );
    // std::string vertexStr = readFile("src/vertex.glsl");
    // const GLchar* vertexString = vertexStr.c_str();
    // GLint vertexLength = static_cast<GLint>(vertexStr.size());
    // glShaderSource(
    //     vertexShader, 1, &vertexString,&vertexLength 
    // );

    // std::string fragmentStr = readFile("src/fragment.glsl");
    // const GLchar* fragmentString = fragmentStr.c_str();
    // GLint fragmentLength = static_cast<GLint>(fragmentStr.size());
    // glShaderSource(
    //     fragmentShader, 1, &fragmentString, &fragmentLength
    // );

    // //compile both
    // glCompileShader(vertexShader);
    // glCompileShader(fragmentShader);

    // //check if shader has compiled
    // // glGetShaderInfoLog(
    // //     shader,
    // //     maxLength,
    // //     length,
    // //     infoLog
    // // );
    // GLint Success;
    // glGetShaderiv(
    //     vertexShader, GL_COMPILE_STATUS, &Success
    // );
    // if(!Success){
    //     std::cerr<<"Failed to Compile Vertex Shader\n";
    //     // Allocate buffer (+1 for null terminator)
    //     GLint logLength;
    //     glGetShaderiv(vertexShader, GL_INFO_LOG_LENGTH, &logLength);
    //     GLchar* infoLog = new GLchar[logLength + 1];
    //     glGetShaderInfoLog(vertexShader, logLength, nullptr, infoLog);
    //     std::cerr<<infoLog<<"\n";
    //     delete[] infoLog;
    //     glfwDestroyWindow(window);
    //     glfwTerminate();
    //     return -1;
    // }   

    // glGetShaderiv(
    //     fragmentShader, GL_COMPILE_STATUS, &Success
    // );
    // if(!Success){
    //     std::cerr<<"Failed to Compile Fragment Shader\n";
    //     // Allocate buffer (+1 for null terminator)
    //     GLint logLength;
    //     glGetShaderiv(fragmentShader, GL_INFO_LOG_LENGTH, &logLength);
    //     GLchar* infoLog = new GLchar[logLength + 1];
    //     glGetShaderInfoLog(fragmentShader, logLength, nullptr, infoLog);
    //     std::cerr<<infoLog<<"\n";
    //     delete[] infoLog;
    //     glfwDestroyWindow(window);
    //     glfwTerminate();
    //     return -1;
    // }     

    // //creating the shader program
    // GLuint shaderProgram = glCreateProgram();
    // //this program is currently empty and waiting for shaders
    
    // glAttachShader(shaderProgram, vertexShader);
    // glAttachShader(shaderProgram, fragmentShader);

    // glLinkProgram(shaderProgram);

    // glGetProgramiv(
    //     shaderProgram, GL_LINK_STATUS, &Success
    // );

    // if(!Success){
    //     std::cerr<<"Failed to Link Shader Program\n";
    //     // std::cerr<<glGetProgramInfoLog() what is the format of this function I dunno 
    //     GLint logLength;
    //     glGetProgramiv(shaderProgram, GL_INFO_LOG_LENGTH, &logLength);

    //     GLchar* infoLog = new GLchar[logLength + 1];
    //     glGetProgramInfoLog(shaderProgram, logLength, nullptr, infoLog);
    //     std::cerr<<infoLog<<"\n";

    //     //destroy shaders too
    //     glDeleteShader(vertexShader);
    //     glDeleteShader(fragmentShader);

    //     glfwDestroyWindow(window);
    //     glfwTerminate();
    //     return -1;
    // }

    // glUseProgram(shaderProgram);
    // glDrawArrays(GL_TRIANGLES, 0, 3); 
    // // glDrawArrays(
    // //     ???,   // what primitive?
    // //     ???,   // where do we start?
    // //     ???    // how many vertices?
    // // );

    // return 0;

}