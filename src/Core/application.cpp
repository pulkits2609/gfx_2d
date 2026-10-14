#include"Core/application.hpp"
#include<algorithm>
#include<glm/gtc/matrix_transform.hpp>
#include<cmath>
#include<string>

Application::Application(int numberOfTeams, const std::string& serverHost,
    unsigned short controlPort, const std::string& name):window(
    1200,
    1200,
    "GFX_2D - 1-4 select | WASD move | Mouse aim | LMB shoot"
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
keyboardinput(),
game(numberOfTeams),
hud(window.GetNativeWindow()){
    if(!serverHost.empty()){
        network.Connect(serverHost, controlPort, name);
        game.SetPlayerName(0, name);
    }
}
    

void Application::RunApplication(){
    GameLoop();
}

void Application::GameLoop(){
    double previousTime = glfwGetTime();
    double accumulator = 0.0;

    while(!window.ShouldClose()){
        window.PollEvents();
        network.Poll();

        double currentTime = glfwGetTime();
        double deltaTime = currentTime - previousTime;
        previousTime = currentTime;

        GLFWwindow* nativeWindow = window.GetNativeWindow();

        int framebufferWidth;
        int framebufferHeight;
        glfwGetFramebufferSize(
            nativeWindow,
            &framebufferWidth,
            &framebufferHeight
        );

        if(framebufferWidth <= 0 || framebufferHeight <= 0
            || glfwGetWindowAttrib(nativeWindow, GLFW_ICONIFIED) == GLFW_TRUE){
            glfwWaitEventsTimeout(0.05);
            previousTime = glfwGetTime();
            accumulator = 0.0;
            continue;
        }

        glm::vec2 arenaHalfSize = game.GetArena().GetHalfSize();

        //padding keeps walls and health bars visible
        float viewHalfWidth = arenaHalfSize.x + 0.16f;
        float viewHalfHeight = arenaHalfSize.y + 0.5f;

        float aspectRatio = static_cast<float>(framebufferWidth)
            / static_cast<float>(framebufferHeight);

        if(aspectRatio >= viewHalfWidth / viewHalfHeight){
            viewHalfWidth = viewHalfHeight * aspectRatio;
        }
        else{
            viewHalfHeight = viewHalfWidth / aspectRatio;
        }

        glm::mat4 projection = glm::ortho(
            -viewHalfWidth,
            viewHalfWidth,
            -viewHalfHeight,
            viewHalfHeight,
            -1.0f,
            1.0f
        );

        PlayerInput input = ReadInput(
            nativeWindow,
            viewHalfWidth,
            viewHalfHeight
        );

        input.reload = pendingReload[selectedPlayer];
        accumulator += deltaTime;

        //process elapsed time in fixed simulation steps
        while(accumulator >= MatchRules::SimulationStep){
            game.Update(
                MatchRules::SimulationStep,
                selectedPlayer,
                input
            );

            pendingReload[selectedPlayer] = false;
            input.reload = false;
            accumulator -= MatchRules::SimulationStep;
        }

        renderer.Clear(0.025f, 0.03f, 0.04f, 1.0f);
        DrawArena(projection);
        DrawPlayers(projection);
        DrawShots(projection);
        hud.Draw(game, selectedPlayer, viewHalfWidth, viewHalfHeight, network.GetStatus());

        //avoid rebuilding the native window title every frame
        if(currentTime >= nextTitleUpdate){
            UpdateWindowTitle();
            nextTitleUpdate = currentTime + 0.1;
        }

        window.SwapBuffers();
    }
}

PlayerInput Application::ReadInput(
    GLFWwindow* nativeWindow,
    float viewHalfWidth,
    float viewHalfHeight
){
    PlayerInput input;

    //do not move or fire while another window has focus
    if(glfwGetWindowAttrib(nativeWindow, GLFW_FOCUSED) != GLFW_TRUE){
        reloadWasPressed = false;
        return input;
    }

    for(std::size_t i = 0; i < game.GetPlayers().size(); i++){
        int key = GLFW_KEY_1 + static_cast<int>(i);

        if(keyboardinput.isKeyPressed(nativeWindow, key)){
            selectedPlayer = i;
        }
    }

    bool wPressed = keyboardinput.isKeyPressed(nativeWindow, GLFW_KEY_W);
    bool aPressed = keyboardinput.isKeyPressed(nativeWindow, GLFW_KEY_A);
    bool sPressed = keyboardinput.isKeyPressed(nativeWindow, GLFW_KEY_S);
    bool dPressed = keyboardinput.isKeyPressed(nativeWindow, GLFW_KEY_D);

    input.movement = glm::vec2(
        static_cast<float>(dPressed) - static_cast<float>(aPressed),
        static_cast<float>(wPressed) - static_cast<float>(sPressed)
    );

    bool reloadPressed = keyboardinput.isKeyPressed(nativeWindow, GLFW_KEY_R);
    if(reloadPressed && !reloadWasPressed) pendingReload[selectedPlayer] = true;
    reloadWasPressed = reloadPressed;
    input.boost = keyboardinput.isKeyPressed(nativeWindow, GLFW_KEY_LEFT_SHIFT)
        || keyboardinput.isKeyPressed(nativeWindow, GLFW_KEY_RIGHT_SHIFT);

    int windowWidth;
    int windowHeight;
    glfwGetWindowSize(nativeWindow, &windowWidth, &windowHeight);

    if(windowWidth <= 0 || windowHeight <= 0){
        return input;
    }

    double cursorX;
    double cursorY;
    glfwGetCursorPos(nativeWindow, &cursorX, &cursorY);

    //cursor coordinates use window units, not framebuffer pixels
    float normalizedX = 2.0f * static_cast<float>(cursorX)
        / static_cast<float>(windowWidth) - 1.0f;

    float normalizedY = 1.0f - 2.0f * static_cast<float>(cursorY)
        / static_cast<float>(windowHeight);

    input.aimPosition = glm::vec2(
        normalizedX * viewHalfWidth,
        normalizedY * viewHalfHeight
    );

    input.hasAim = true;

    bool cursorInside = cursorX >= 0.0 && cursorX <= windowWidth
        && cursorY >= 0.0 && cursorY <= windowHeight;

    input.shoot = cursorInside
        && glfwGetMouseButton(nativeWindow, GLFW_MOUSE_BUTTON_LEFT)
            == GLFW_PRESS;

    return input;
}

void Application::DrawRectangle(
    const glm::vec2& position,
    const glm::vec2& size,
    const glm::vec3& color,
    const glm::mat4& projection,
    float rotation
){
    if(size.x <= 0.0f || size.y <= 0.0f){
        return;
    }

    Mesh& squareMesh = meshes[1];
    glm::vec2 baseSize = squareMesh.GetHalfExtents() * 2.0f;

    Transform transform;
    transform.SetPosition({position.x, position.y});
    transform.SetRotation(rotation);
    transform.SetScale({
        size.x / baseSize.x,
        size.y / baseSize.y
    });

    renderer.Draw(squareMesh, shader, transform, projection, color);
}

void Application::DrawArena(const glm::mat4& projection){
    glm::vec2 halfSize = game.GetArena().GetHalfSize();

    DrawRectangle(
        glm::vec2(0.0f, 0.0f),
        halfSize * 2.0f,
        glm::vec3(0.08f, 0.09f, 0.12f),
        projection
    );

    constexpr float wallThickness = 0.025f;
    float halfThickness = wallThickness * 0.5f;
    glm::vec3 wallColor(0.4f, 0.45f, 0.55f);

    //walls sit outside the playable bounds
    DrawRectangle(
        glm::vec2(0.0f, halfSize.y + halfThickness),
        glm::vec2(2.0f * halfSize.x + 2.0f * wallThickness, wallThickness),
        wallColor,
        projection
    );

    DrawRectangle(
        glm::vec2(0.0f, -halfSize.y - halfThickness),
        glm::vec2(2.0f * halfSize.x + 2.0f * wallThickness, wallThickness),
        wallColor,
        projection
    );

    DrawRectangle(
        glm::vec2(-halfSize.x - halfThickness, 0.0f),
        glm::vec2(wallThickness, 2.0f * halfSize.y),
        wallColor,
        projection
    );

    DrawRectangle(
        glm::vec2(halfSize.x + halfThickness, 0.0f),
        glm::vec2(wallThickness, 2.0f * halfSize.y),
        wallColor,
        projection
    );
}

void Application::DrawPlayers(const glm::mat4& projection){
    constexpr float halfPi = 1.57079632679489661923f;
    const auto& players = game.GetPlayers();

    for(std::size_t i = 0; i < players.size(); i++){
        const Player& player = players[i];
        if(!player.IsAlive()) continue;
        glm::vec3 teamColor = GameUI::TeamColor(player.team);
        float aimAngle = std::atan2(player.aimDirection.y, player.aimDirection.x);

        //reuse the team's mesh; collision always uses the shared circular hitbox
        transforms[i].SetPosition({player.position.x, player.position.y});
        transforms[i].SetRotation(aimAngle - halfPi);
        transforms[i].SetScale({MatchRules::PlayerVisualScale, MatchRules::PlayerVisualScale});
        glm::vec3 bodyColor = player.IsSpawnProtected(game.GetTime())
            ? glm::vec3(0.85f, 0.87f, 0.84f) : teamColor;
        renderer.Draw(meshes[static_cast<std::size_t>(player.team)], shader, transforms[i], projection, bodyColor);

        if(i != selectedPlayer) continue;
        glm::vec2 forward = player.aimDirection;
        glm::vec2 side(-forward.y, forward.x);
        auto segment = [&](glm::vec2 from, glm::vec2 to, float thickness, glm::vec3 color){
            glm::vec2 difference = to - from;
            DrawRectangle((from + to) * 0.5f, glm::vec2(glm::length(difference), thickness),
                color, projection, std::atan2(difference.y, difference.x));
        };
        //split shaft and an open, notched chevron keep the pointer light
        glm::vec2 base = player.position + forward * (MatchRules::PlayerRadius + 0.025f);
        glm::vec2 tip = base + forward * 0.17f;
        segment(base, base + forward * 0.055f, 0.005f, teamColor * 0.8f);
        segment(base + forward * 0.075f, tip - forward * 0.035f, 0.005f, teamColor);
        segment(tip - forward * 0.045f + side * 0.028f, tip, 0.007f, teamColor);
        segment(tip - forward * 0.045f - side * 0.028f, tip, 0.007f, teamColor);
        segment(base - side * 0.014f, base + side * 0.014f, 0.004f, teamColor * 0.6f);
    }
}

void Application::DrawShots(const glm::mat4& projection){
    for(const ShotTrace& shot : game.GetShots()){
        if(shot.expiresAt <= game.GetTime()) continue;
        glm::vec2 difference = shot.end - shot.start;
        float distance = glm::length(difference);
        if(distance <= 0.00001f) continue;
        glm::vec2 direction = difference / distance;
        float age = static_cast<float>(game.GetTime() - shot.firedAt);
        float travelTime = distance / MatchRules::TracerSpeed;
        float head = std::min(distance, age * MatchRules::TracerSpeed);
        float tail = std::max(0.0f, head - MatchRules::TracerLength
            + std::max(0.0f, age - travelTime) * MatchRules::TracerSpeed);
        if(tail >= head) continue;
        glm::vec2 centre = shot.start + direction * ((head + tail) * 0.5f);
        float angle = std::atan2(direction.y, direction.x);
        DrawRectangle(centre, glm::vec2(head - tail, 0.010f),
            glm::vec3(0.74f, 0.49f, 0.19f), projection, angle);
        DrawRectangle(centre, glm::vec2(head - tail, 0.004f),
            glm::vec3(1.0f, 0.91f, 0.66f), projection, angle);
    }
}

void Application::UpdateWindowTitle(){
    const Player& player = game.GetPlayers()[selectedPlayer];

    std::string title = "GFX_2D | 1-4 select | WASD move | Mouse aim | LMB shoot";
    title += " | Player " + std::to_string(selectedPlayer + 1);
    title += " | HP " + std::to_string(player.health);
    title += " | K " + std::to_string(player.kills);
    title += " D " + std::to_string(player.deaths);

    if(!player.IsAlive()){
        double remaining = std::max(
            0.0,
            player.respawnTime - game.GetTime()
        );

        title += " | Respawn "
            + std::to_string(static_cast<int>(std::ceil(remaining)))
            + "s";
    }
    else if(player.IsSpawnProtected(game.GetTime())){
        title += " | Spawn protection";
    }

    glfwSetWindowTitle(window.GetNativeWindow(), title.c_str());
}
