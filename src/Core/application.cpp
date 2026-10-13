#include"Core/application.hpp"
#include<algorithm>
#include<glm/gtc/matrix_transform.hpp>

Application::Application():window(
    1200,
    1200,
    "GFX_2D - WASD move | 1 Triangle | 2 Square | 3 Circle | 4 Hexagon"
),
shader(
    "assets/shaders/vertex.glsl",
    "assets/shaders/fragment.glsl"
),
renderer(),
meshes{
    Mesh(MeshShape::Triangle),
    Mesh(MeshShape::Square),
    Mesh(MeshShape::Circle),
    Mesh(MeshShape::Hexagon)
},
transforms(),
keyboardinput(){
    transforms[0].SetPosition({-0.5f, 0.5f});
    transforms[1].SetPosition({0.5f, 0.5f});
    transforms[2].SetPosition({-0.5f, -0.5f});
    transforms[3].SetPosition({0.5f, -0.5f});
}
    

void Application::RunApplication(){
    GameLoop();
}

void Application::GameLoop(){
    double previousTime = glfwGetTime();

    while(!window.ShouldClose()){
        window.PollEvents();

        double currentTime = glfwGetTime();
        float deltaTime = static_cast<float>(currentTime - previousTime);
        previousTime = currentTime;

        GLFWwindow* nativeWindow = window.GetNativeWindow();

        int framebufferWidth;
        int framebufferHeight;
        glfwGetFramebufferSize(
            nativeWindow,
            &framebufferWidth,
            &framebufferHeight
        );

        //a minimized window can have a zero-sized framebuffer
        if(framebufferWidth <= 0 || framebufferHeight <= 0){
            glfwWaitEventsTimeout(0.05);
            previousTime = glfwGetTime();
            continue;
        }

        //preserve proportions while using the entire visible framebuffer
        float aspectRatio = static_cast<float>(framebufferWidth)
            / static_cast<float>(framebufferHeight);

        float halfWidth = 1.0f;
        float halfHeight = 1.0f;

        if(aspectRatio >= 1.0f){
            halfWidth = aspectRatio;
        }
        else{
            halfHeight = 1.0f / aspectRatio;
        }

        glm::mat4 projection = glm::ortho(
            -halfWidth,
            halfWidth,
            -halfHeight,
            halfHeight,
            -1.0f,
            1.0f
        );

        //keys 1-4 select which displayed shape receives movement input
        for(std::size_t i = 0; i < meshes.size(); i++){
            int key = GLFW_KEY_1 + static_cast<int>(i);

            if(keyboardinput.isKeyPressed(nativeWindow, key)){
                selectedShape = i;
            }
        }

        bool wPressed = keyboardinput.isKeyPressed(nativeWindow, GLFW_KEY_W);
        bool aPressed = keyboardinput.isKeyPressed(nativeWindow, GLFW_KEY_A);
        bool sPressed = keyboardinput.isKeyPressed(nativeWindow, GLFW_KEY_S);
        bool dPressed = keyboardinput.isKeyPressed(nativeWindow, GLFW_KEY_D);

        //opposing keys cancel each other
        glm::vec2 direction(
            static_cast<float>(dPressed) - static_cast<float>(aPressed),
            static_cast<float>(wPressed) - static_cast<float>(sPressed)
        );

        //never normalize a zero-length vector
        if(direction.x != 0.0f || direction.y != 0.0f){
            direction = glm::normalize(direction);

            transforms[selectedShape].Translate(
                direction.x * movementSpeed * deltaTime,
                direction.y * movementSpeed * deltaTime
            );
        }

        //clamp every shape, including stationary ones after a window resize
        for(std::size_t i = 0; i < meshes.size(); i++){
            glm::vec2 halfExtents = meshes[i].GetHalfExtents();
            Vector position = transforms[i].GetPosition();

            position.x = std::clamp(
                position.x,
                -halfWidth + halfExtents.x,
                halfWidth - halfExtents.x
            );

            position.y = std::clamp(
                position.y,
                -halfHeight + halfExtents.y,
                halfHeight - halfExtents.y
            );

            transforms[i].SetPosition(position);
        }

        //rendering is done after the input
        renderer.Clear(0.1f, 0.1f, 0.1f, 1.0f);

        for(std::size_t i = 0; i < meshes.size(); i++){
            renderer.Draw(meshes[i], shader, transforms[i], projection);
        }

        window.SwapBuffers();
    }
}