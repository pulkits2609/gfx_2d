#pragma once

#include<algorithm>
#include<glm/glm.hpp>
#include"Game/matchRules.hpp"

//Window coordinates are used for both mouse input and HUD placement.
//The OpenGL viewport converts these to framebuffer pixels (important for DPI).
struct ViewLayout{
    float left = 0, top = 0, width = 1, height = 1;
    float halfWidth = 1, halfHeight = 1;
    float scale = 1;

    static ViewLayout Calculate(float width, float height){
        ViewLayout view;
        view.scale = std::clamp(std::min(width / 900.0f, height / 650.0f), 0.5f, 2.5f);
        view.left = 18 * view.scale;
        view.top = 125 * view.scale;
        view.width = std::max(1.0f, width - 2 * view.left);
        view.height = std::max(1.0f, height - view.top - 140 * view.scale);
        view.halfWidth = MatchRules::ArenaHalfWidth + 0.08f;
        view.halfHeight = MatchRules::ArenaHalfHeight + 0.20f;
        float aspect = view.width / view.height;
        if(aspect >= view.halfWidth / view.halfHeight) view.halfWidth = view.halfHeight * aspect;
        else view.halfHeight = view.halfWidth / aspect;
        return view;
    }

    glm::vec2 ToWorld(float x, float y) const{
        return {(2 * (x - left) / width - 1) * halfWidth,
            (1 - 2 * (y - top) / height) * halfHeight};
    }
};
