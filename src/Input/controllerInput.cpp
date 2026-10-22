#include "Input/controllerInput.hpp"
#include <cmath>

namespace{
    constexpr float StickDeadzone=0.22f;
    constexpr float TriggerThreshold=0.25f;
    constexpr float DiagonalBoundary=0.41421356f; //tan(22.5 degrees): eight equal movement sectors.
    constexpr double DeviceScanInterval=1.0;

    glm::vec2 ReadStick(const GLFWgamepadstate& state, int xAxis, int yAxis){
        const float x=state.axes[xAxis];
        const float y=state.axes[yAxis];
        if(!std::isfinite(x) || !std::isfinite(y)) return glm::vec2(0.0f);
        const float length=std::sqrt(x*x+y*y);
        if(length<=StickDeadzone) return glm::vec2(0.0f);
        //GLFW points down on Y; gameplay world coordinates point up.
        return glm::vec2(x/length, -y/length);
    }

    bool BindingPressed(const GLFWgamepadstate& state, int binding){
        if(binding==0 || binding==1){
            const int axis=binding==0 ? GLFW_GAMEPAD_AXIS_LEFT_TRIGGER : GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER;
            const float amount=(state.axes[axis]+1.0f)*0.5f;
            return std::isfinite(amount) && amount>TriggerThreshold;
        }
        constexpr int buttons[]={GLFW_GAMEPAD_BUTTON_LEFT_BUMPER, GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER,
            GLFW_GAMEPAD_BUTTON_A, GLFW_GAMEPAD_BUTTON_B, GLFW_GAMEPAD_BUTTON_X, GLFW_GAMEPAD_BUTTON_Y,
            GLFW_GAMEPAD_BUTTON_LEFT_THUMB, GLFW_GAMEPAD_BUTTON_RIGHT_THUMB};
        if(binding<2 || binding>=ControllerInput::BindingCount) return false;
        return state.buttons[buttons[binding-2]]==GLFW_PRESS;
    }
}

ControllerState ControllerInput::Decode(const GLFWgamepadstate& state, int boostBinding, int fireBinding){
    ControllerState input;
    const glm::vec2 stick=ReadStick(state, GLFW_GAMEPAD_AXIS_LEFT_X, GLFW_GAMEPAD_AXIS_LEFT_Y);
    const float absX=std::abs(stick.x);
    const float absY=std::abs(stick.y);
    //The network sends keyboard-style axes. Keep diagonal normalization in the shared game rules.
    if(absX>0.0f && absX>=absY*DiagonalBoundary) input.movement.x=stick.x>0.0f ? 1.0f : -1.0f;
    if(absY>0.0f && absY>=absX*DiagonalBoundary) input.movement.y=stick.y>0.0f ? 1.0f : -1.0f;
    input.aimDirection=ReadStick(state, GLFW_GAMEPAD_AXIS_RIGHT_X, GLFW_GAMEPAD_AXIS_RIGHT_Y);
    input.aiming=input.aimDirection.x!=0.0f || input.aimDirection.y!=0.0f;
    input.boost=BindingPressed(state, boostBinding);
    input.fire=BindingPressed(state, fireBinding);
    //X reloads unless the player assigned it to an action instead.
    input.reload=(boostBinding!=6 && fireBinding!=6 && state.buttons[GLFW_GAMEPAD_BUTTON_X]==GLFW_PRESS)
        || state.buttons[GLFW_GAMEPAD_BUTTON_BACK]==GLFW_PRESS;
    return input;
}

void ControllerInput::Poll(int preferred){
    const double now=glfwGetTime();
    const bool activeDisconnected=activeId>=0 && glfwJoystickPresent(activeId)!=GLFW_TRUE;
    if(now>=nextDeviceScan || activeDisconnected){
        devices.clear();
        for(int id=GLFW_JOYSTICK_1; id<=GLFW_JOYSTICK_LAST; ++id){
            if(glfwJoystickPresent(id)!=GLFW_TRUE) continue;
            const bool mapped=glfwJoystickIsGamepad(id)==GLFW_TRUE;
            const char* name=mapped ? glfwGetGamepadName(id) : glfwGetJoystickName(id);
            devices.push_back({id, name ? name : "Controller", mapped});
        }
        nextDeviceScan=now+DeviceScanInterval;
    }

    int selected=-1;
    for(const ControllerDevice& device:devices){
        if(!device.mapped) continue;
        if(preferred>=0 && device.id!=preferred) continue;
        selected=device.id;
        break;
    }
    if(selected!=activeId){
        activeId=selected;
        startWasPressed=false;
    }

    state={};
    state.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER]=-1.0f;
    state.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER]=-1.0f;
    hasState=activeId>=0 && glfwGetGamepadState(activeId, &state)==GLFW_TRUE;
    pausePressed=false;
    if(!hasState){
        activeId=-1;
        startWasPressed=false;
        return;
    }
    const bool startPressed=state.buttons[GLFW_GAMEPAD_BUTTON_START]==GLFW_PRESS;
    pausePressed=startPressed && !startWasPressed;
    startWasPressed=startPressed;
}

ControllerState ControllerInput::GetInput(int boostBinding, int fireBinding) const{
    if(!hasState) return ControllerState{};
    ControllerState input=Decode(state, boostBinding, fireBinding);
    input.pausePressed=pausePressed;
    return input;
}

const std::vector<ControllerDevice>& ControllerInput::Devices() const{
    return devices;
}

int ControllerInput::ActiveId() const{
    return activeId;
}

std::string ControllerInput::ActiveName() const{
    for(const ControllerDevice& device:devices){
        if(device.id==activeId) return device.name;
    }
    return "No controller connected";
}

const char* ControllerInput::BindingName(int binding){
    constexpr const char* names[]={"Left trigger", "Right trigger", "Left bumper", "Right bumper",
        "A / Cross", "B / Circle", "X / Square", "Y / Triangle", "Left stick click", "Right stick click"};
    if(binding<0 || binding>=BindingCount) return "Unassigned";
    return names[binding];
}
