#pragma once

#include<glad/gl.h>
#include<fstream>
#include<string>
#include<iostream>
#include<glm/glm.hpp>

// Shader
// │
// ├── Read shader source
// ├── Create vertex shader
// ├── Create fragment shader
// ├── Compile them
// ├── Check compilation
// ├── Create program
// ├── Attach shaders
// ├── Link program
// ├── Check linking
// ├── Delete temporary shader objects
// └── Delete program when Shader dies

class Shader{
    private:
        GLuint shaderProgram;
        std::string readShader(const std::string& shaderName);

    public:
        Shader(const std::string& fragmentFile, const std::string& vertexFile);
        ~Shader();

        void CompileShader(GLuint shaderToCompile, const std::string& shaderData);
        void LinkProgram(GLuint shaderProgram, GLuint vertexShader, GLuint fragmentShader);
        void Bind();
        void Unbind();

        void SetMat4(const std::string& name, const glm::mat4& matrix);
};