#pragma once

#include<glad/gl.h>
#include"Graphics/shader.hpp"
#include"Graphics/mesh.hpp"
#include"Graphics/transform.hpp"

class Renderer{
    private:
        //no need to store the Shader because it's scope is only within the function Draw
    public:
        Renderer();
        void Clear(float r, float g, float b, float a); //clear the current framebuffer
        void Draw(TriangleMesh& mesh, Shader& shader, Transform& transform);
};