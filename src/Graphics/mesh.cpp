#include"Graphics/mesh.hpp"


// TriangleMesh ------------------------------------------------

TriangleMesh::TriangleMesh():
vbo(
    GL_ARRAY_BUFFER,
    sizeof(float)*6,
    nullptr, 
    GL_STATIC_DRAW
),
vao(){
    float vertices[] = {
        0.0f, 0.5f,
        -0.5f, -0.5f,
        0.5f, -0.5f
    };

    vbo.Bind();
    vao.Bind();

    glBufferData(
        GL_ARRAY_BUFFER,
        sizeof(vertices),
        vertices,
        GL_STATIC_DRAW
    );

    vao.AddAttribute(
        0,
        2,
        GL_FLOAT,
        GL_FALSE,
        2* sizeof(float),
        0
    );

    vbo.Unbind();
}

void TriangleMesh::Bind(){
    vao.Bind();
}

void TriangleMesh::Unbind(){
    vao.Unbind();
}

GLsizei TriangleMesh::GetVertexCount(){
    return vertexCount;
}