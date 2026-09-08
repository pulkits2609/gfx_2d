#include"../include/Graphics/renderer.hpp"

void Renderer::Clear(float r, float g, float b, float a){
    glClearColor(r,g,b,a);
    glClear(GL_COLOR_BUFFER_BIT);
}

void Renderer::Draw(GLenum mode, GLint startIndex, GLsizei count){
    glDrawArrays(
        mode, startIndex, count
    );
}