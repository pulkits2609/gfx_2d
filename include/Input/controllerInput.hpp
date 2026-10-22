#pragma once

#include <GLFW/glfw3.h>
#include <glm/vec2.hpp>
#include <string>
#include <vector>

struct ControllerDevice{
    int id;
    std::string name;
    bool mapped;
};

struct ControllerState{
    glm::vec2 movement{0.0f};
    glm::vec2 aimDirection{0.0f};
    bool aiming=false;
    bool boost=false;
    bool fire=false;
    bool reload=false;
    bool pausePressed=false;
};

class ControllerInput{
    public:
        void Poll(int preferred=-1);
        ControllerState GetInput(int boostBinding, int fireBinding) const;
        const std::vector<ControllerDevice>& Devices() const;
        int ActiveId() const;
        std::string ActiveName() const;

        static constexpr int BindingCount=10;
        static const char* BindingName(int binding);
        static ControllerState Decode(const GLFWgamepadstate& state, int boostBinding, int fireBinding);

    private:
        std::vector<ControllerDevice> devices;
        GLFWgamepadstate state{};
        int activeId=-1;
        double nextDeviceScan=0.0;
        bool hasState=false;
        bool startWasPressed=false;
        bool pausePressed=false;
};
