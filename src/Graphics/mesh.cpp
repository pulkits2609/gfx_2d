#include"Graphics/mesh.hpp"
#include<algorithm>
#include<cmath>
#include<stdexcept>

Mesh::Mesh(MeshShape shape):Mesh(CreateVertices(shape)){

}

Mesh::Mesh(const std::vector<float>& vertices):
vbo(
    GL_ARRAY_BUFFER,
    static_cast<GLsizeiptr>(vertices.size() * sizeof(float)),
    vertices.data(),
    GL_STATIC_DRAW
),
vao(),
vertexCount(static_cast<GLsizei>(vertices.size() / 2)),
halfExtents(0.0f, 0.0f){
    //measure the furthest vertex from the origin on each axis
    for(std::size_t i = 0; i < vertices.size(); i += 2){
        halfExtents.x = std::max(halfExtents.x, std::abs(vertices[i]));
        halfExtents.y = std::max(halfExtents.y, std::abs(vertices[i + 1]));
    }

    vbo.Bind();
    vao.Bind();

    vao.AddAttribute(
        0,
        2,
        GL_FLOAT,
        GL_FALSE,
        2* sizeof(float),
        0
    );

    vao.Unbind();
    vbo.Unbind();
}

std::vector<float> Mesh::CreateVertices(MeshShape shape){
    //all shapes use the same basic size
    constexpr float radius = 0.15f;
    constexpr float pi = 3.14159265358979323846f;

    switch(shape){
        case MeshShape::Triangle:
            return {
                0.0f, radius,
                -radius, -radius,
                radius, -radius
            };

        case MeshShape::Square:
            //two triangles form one square
            return {
                -radius, -radius,
                radius, -radius,
                radius, radius,

                -radius, -radius,
                radius, radius,
                -radius, radius
            };

        case MeshShape::Circle:
        case MeshShape::Hexagon:
            break;

        default:
            throw std::invalid_argument("Unsupported mesh shape");
    }

    //a circle is approximated using many small triangles
    const int segments = shape == MeshShape::Circle ? 64 : 6;

    std::vector<float> vertices;
    vertices.reserve(static_cast<std::size_t>(segments) * 6);

    for(int i = 0; i < segments; i++){
        float angle = 2.0f * pi * static_cast<float>(i)
            / static_cast<float>(segments);

        float nextAngle = 2.0f * pi * static_cast<float>(i + 1)
            / static_cast<float>(segments);

        //each triangle connects the centre to two neighbouring perimeter points
        vertices.push_back(0.0f);
        vertices.push_back(0.0f);

        vertices.push_back(radius * std::cos(angle));
        vertices.push_back(radius * std::sin(angle));

        vertices.push_back(radius * std::cos(nextAngle));
        vertices.push_back(radius * std::sin(nextAngle));
    }

    return vertices;
}

void Mesh::Bind(){
    vao.Bind();
}

void Mesh::Unbind(){
    vao.Unbind();
}

GLsizei Mesh::GetVertexCount() const{
    return vertexCount;
}

glm::vec2 Mesh::GetHalfExtents() const{
    return halfExtents;
}