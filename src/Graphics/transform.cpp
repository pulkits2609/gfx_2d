#include"Graphics/transform.hpp"
#include <glm/gtc/matrix_transform.hpp>

Transform::Transform():position({0.0f, 0.0f}),rotation(0.0f),scale({1.0f,1.0f}){}

void Transform::SetPosition(const Vector& p){
    position.x = p.x;
    position.y = p.y;
}

Vector Transform::GetPosition(){
    return position;
}

void Transform::SetRotation(const float& r){
    rotation = r;
}

float Transform::GetRotation(){
    return rotation;
}

void Transform::SetScale(const Vector& s){
    scale = s;
}

Vector Transform::GetScale(){
    return scale;
}

void Transform::Translate(float x, float y){
    position.x += x;
    position.y += y;
}

glm::mat4 Transform::GetModelMatrix(){
    glm::mat4 model(1.0f);
    model = glm::translate(
        model,
        glm::vec3(position.x, position.y, 0.0f)
    );
    return model;
}