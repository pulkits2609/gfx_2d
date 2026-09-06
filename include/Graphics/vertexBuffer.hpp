#pragma once
#include<glad/gl.h>

class VertexBuffer{
    private:
        GLuint vbo;
        GLenum target; //this target will be remembered by the class, so that we wont be passing it manually everytime at time of binding and unbinding
    
    public:
        VertexBuffer(GLenum target, GLsizeiptr size, const GLvoid* data, GLenum usage);
        ~VertexBuffer();

        void Bind();
        void Unbind();
};