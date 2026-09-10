#include"Graphics/renderer.hpp"

Renderer::Renderer(){}

void Renderer::Clear(float r, float g, float b, float a){
    glClearColor(r,g,b,a);
    glClear(GL_COLOR_BUFFER_BIT);
}

void Renderer::Draw(TriangleMesh& mesh, Shader& shader, Transform& transform){
    shader.Bind();
    shader.SetMat4("u_Model", transform.GetModelMatrix());
    mesh.Bind();
    
    glDrawArrays(GL_TRIANGLES, 0, 3);

    mesh.Unbind();
    shader.Unbind();
}