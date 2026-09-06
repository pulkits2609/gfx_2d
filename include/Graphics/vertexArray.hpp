#pragma once

#include<glad/gl.h>

class VertexArray{
    //this will be owning the VAO (Vertex array object)
    private:
        GLuint vao;
    
    public:
        VertexArray();
        ~VertexArray();

        void Bind();
        void Unbind();
        void AddAttribute(
            GLuint index, 
            GLint size, 
            GLenum type, 
            GLboolean normalized, 
            GLsizei stride, 
            const void* pointer
        );
};