#pragma once

#include<glad/gl.h>

class Renderer{
    
    public:
        void Clear(float r, float g, float b, float a); //clear the current framebuffer
        void Draw(GLenum mode, GLint startIndex, GLsizei count); //draw something using already prepared graphics resource
};