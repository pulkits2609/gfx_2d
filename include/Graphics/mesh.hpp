#pragma once

#include"Graphics/vertexBuffer.hpp"
#include"Graphics/vertexArray.hpp"
#include<glm/glm.hpp>
#include<vector>

//only these four shape types are available
enum class MeshShape{
    Triangle,
    Square,
    Circle,
    Hexagon
};

//this mesh class will hold the abstraction of VAO and VBO to create different shapes!

class Mesh {
    private:
        VertexBuffer vbo;
        VertexArray vao;
        GLsizei vertexCount;
        glm::vec2 halfExtents;

        static std::vector<float> CreateVertices(MeshShape shape);

        //receives generated vertices and uploads them once
        explicit Mesh(const std::vector<float>& vertices);

    public:
        explicit Mesh(MeshShape shape = MeshShape::Triangle);

        //a mesh owns GPU resources, so copying it would duplicate ownership
        Mesh(const Mesh&) = delete;
        Mesh& operator=(const Mesh&) = delete;

        void Bind();
        void Unbind();
        GLsizei GetVertexCount() const;
        glm::vec2 GetHalfExtents() const;
};