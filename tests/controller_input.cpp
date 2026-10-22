#include "Input/controllerInput.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace{
    void Check(bool condition, const char* message){
        if(!condition) throw std::runtime_error(message);
    }

    GLFWgamepadstate Resting(){
        GLFWgamepadstate state{};
        state.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER]=-1.0f;
        state.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER]=-1.0f;
        return state;
    }
}

int main(){
    try{
        auto state=Resting();
        auto input=ControllerInput::Decode(state, 0, 1);
        Check(input.movement==glm::vec2(0.0f) && !input.aiming && !input.boost && !input.fire,
            "resting controller must not move or hold triggers");
        state.axes[GLFW_GAMEPAD_AXIS_LEFT_X]=0.15f;
        state.axes[GLFW_GAMEPAD_AXIS_LEFT_Y]=-0.15f;
        state.axes[GLFW_GAMEPAD_AXIS_RIGHT_X]=0.21f;
        input=ControllerInput::Decode(state, 0, 1);
        Check(input.movement==glm::vec2(0.0f) && !input.aiming, "radial deadzone ignores stick drift");

        const glm::vec2 directions[]={{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1},{0,-1},{1,-1}};
        for(const glm::vec2 direction:directions){
            state=Resting();
            state.axes[GLFW_GAMEPAD_AXIS_LEFT_X]=direction.x;
            state.axes[GLFW_GAMEPAD_AXIS_LEFT_Y]=-direction.y;
            input=ControllerInput::Decode(state, 0, 1);
            Check(input.movement==direction, "all eight movement directions use digital axes");
        }
        state.axes[GLFW_GAMEPAD_AXIS_LEFT_X]=1.0f;
        state.axes[GLFW_GAMEPAD_AXIS_LEFT_Y]=0.1f;
        Check(ControllerInput::Decode(state, 0, 1).movement==glm::vec2(1,0),
            "small perpendicular stick drift must not force diagonal movement");

        state=Resting();
        state.axes[GLFW_GAMEPAD_AXIS_RIGHT_X]=0.6f;
        state.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y]=-0.8f;
        input=ControllerInput::Decode(state, 0, 1);
        Check(input.aiming && std::abs(input.aimDirection.x-0.6f)<0.0001f
            && std::abs(input.aimDirection.y-0.8f)<0.0001f, "right stick aim points into world coordinates");
        state.axes[GLFW_GAMEPAD_AXIS_RIGHT_X]=0.3f;
        state.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y]=0.4f;
        input=ControllerInput::Decode(state, 0, 1);
        Check(std::abs(input.aimDirection.x*input.aimDirection.x+input.aimDirection.y*input.aimDirection.y-1.0f)<0.0001f,
            "aim direction is normalized independently of stick displacement");

        state=Resting();
        state.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER]=-0.6f;
        Check(!ControllerInput::Decode(state, 0, 1).boost, "trigger threshold ignores a light accidental press");
        state.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER]=0.0f;
        state.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER]=1.0f;
        input=ControllerInput::Decode(state, 0, 1);
        Check(input.boost && input.fire, "default left boost and right fire triggers work");
        input=ControllerInput::Decode(state, 1, 0);
        Check(input.boost && input.fire, "trigger bindings can be swapped");

        constexpr int buttons[]={GLFW_GAMEPAD_BUTTON_LEFT_BUMPER, GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER,
            GLFW_GAMEPAD_BUTTON_A, GLFW_GAMEPAD_BUTTON_B, GLFW_GAMEPAD_BUTTON_X, GLFW_GAMEPAD_BUTTON_Y,
            GLFW_GAMEPAD_BUTTON_LEFT_THUMB, GLFW_GAMEPAD_BUTTON_RIGHT_THUMB};
        for(int binding=2; binding<ControllerInput::BindingCount; ++binding){
            state=Resting();
            state.buttons[buttons[binding-2]]=GLFW_PRESS;
            input=ControllerInput::Decode(state, binding, binding);
            Check(input.boost && input.fire, "every exposed button binding can activate an action");
            if(binding==6) Check(!input.reload, "remapped X does not also reload");
            state.buttons[buttons[binding-2]]=GLFW_RELEASE;
            input=ControllerInput::Decode(state, binding, binding);
            Check(!input.boost && !input.fire, "released actions never stay pressed");
        }
        state=Resting();
        state.buttons[GLFW_GAMEPAD_BUTTON_X]=GLFW_PRESS;
        Check(ControllerInput::Decode(state, 0, 1).reload, "X reloads with default bindings");
        Check(!ControllerInput::Decode(state, 6, 1).reload && !ControllerInput::Decode(state, 0, 6).reload,
            "either action using X suppresses reload");
        state.buttons[GLFW_GAMEPAD_BUTTON_BACK]=GLFW_PRESS;
        Check(ControllerInput::Decode(state, 6, 1).reload && ControllerInput::Decode(state, 0, 6).reload,
            "Back keeps reload available even when X is assigned to fire or boost");
        input=ControllerInput::Decode(state, -1, 100);
        Check(!input.boost && !input.fire, "invalid bindings remain inactive");
        Check(std::string(ControllerInput::BindingName(-1))=="Unassigned", "invalid binding label is safe");
        state.axes[GLFW_GAMEPAD_AXIS_LEFT_X]=std::numeric_limits<float>::quiet_NaN();
        state.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y]=std::numeric_limits<float>::infinity();
        input=ControllerInput::Decode(state, 0, 1);
        Check(input.movement==glm::vec2(0.0f) && !input.aiming, "invalid analog values do not reach gameplay");
        std::cout << "Controller input checks passed\n";
    }
    catch(const std::exception& error){
        std::cerr << error.what() << '\n';
        return 1;
    }
}
