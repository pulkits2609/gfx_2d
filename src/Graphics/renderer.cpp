#include"Graphics/renderer.hpp"

Renderer::Renderer(){}

void Renderer::Clear(float r, float g, float b, float a){
    glClearColor(r,g,b,a);
    glClear(GL_COLOR_BUFFER_BIT);
}

void Renderer::Draw(
    Mesh& mesh,
    Shader& shader,
    Transform& transform,
    const glm::mat4& projection
){
    shader.Bind();
    shader.SetMat4("u_Model", transform.GetModelMatrix());
    shader.SetMat4("u_Projection", projection);
    mesh.Bind();

    glDrawArrays(GL_TRIANGLES, 0, mesh.GetVertexCount());

    mesh.Unbind();
    shader.Unbind();
}