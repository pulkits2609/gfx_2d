#pragma once

#include<glm/glm.hpp>

struct Vector{
    float x;
    float y;
};

class Transform{
    private:
        Vector position;
        float rotation;
        Vector scale;

    public:
        Transform();
        void SetPosition(const Vector& p);
        Vector GetPosition();
        void SetRotation(const float& r);
        float GetRotation();
        void SetScale(const Vector& s);
        Vector GetScale();
        void Translate(float x, float y);
        glm::mat4 GetModelMatrix();
};