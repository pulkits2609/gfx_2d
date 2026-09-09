#pragma once

#include"Graphics/vertexBuffer.hpp"
#include"Graphics/vertexArray.hpp"

//this mesh class will hold the abstraction of VAO and VBO to create different shapes!

class TriangleMesh {
    private:
        VertexBuffer vbo;
        VertexArray vao;
        GLsizei vertexCount = 3; //hardcoding it because why not
    public:
        TriangleMesh();

        void Bind();
        void Unbind();
        GLsizei GetVertexCount();
};