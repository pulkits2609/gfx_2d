#include"Core/application.hpp"
#include<algorithm>
#include<glm/gtc/matrix_transform.hpp>
#include<cmath>
#include<string>

Application::Application():window(
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
game(){

}
    

void Application::RunApplication(){
    GameLoop();
}

void Application::GameLoop(){
    double previousTime = glfwGetTime();
    double accumulator = 0.0;

    while(!window.ShouldClose()){
        window.PollEvents();

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
        float viewHalfWidth = arenaHalfSize.x + 0.1f;
        float viewHalfHeight = arenaHalfSize.y + 0.1f;

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

        accumulator += deltaTime;

        //process elapsed time in fixed simulation steps
        while(accumulator >= MatchRules::SimulationStep){
            game.Update(
                MatchRules::SimulationStep,
                selectedPlayer,
                input
            );

            accumulator -= MatchRules::SimulationStep;
        }

        renderer.Clear(0.025f, 0.03f, 0.04f, 1.0f);
        DrawArena(projection);
        DrawPlayers(projection);
        DrawShots(projection);

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
    static const std::array<glm::vec3, Game::PlayerCount> teamColors{
        glm::vec3(1.0f, 0.25f, 0.25f),
        glm::vec3(0.25f, 0.6f, 1.0f),
        glm::vec3(0.3f, 0.9f, 0.45f),
        glm::vec3(1.0f, 0.8f, 0.2f)
    };

    constexpr float halfPi = 1.57079632679489661923f;
    const auto& players = game.GetPlayers();

    for(std::size_t i = 0; i < players.size(); i++){
        const Player& player = players[i];

        if(!player.IsAlive()){
            continue;
        }

        const glm::vec3& teamColor = teamColors[
            static_cast<std::size_t>(player.team)
        ];

        float aimAngle = std::atan2(
            player.aimDirection.y,
            player.aimDirection.x
        );

        //a circle behind the selected player shows its common hitbox
        if(i == selectedPlayer){
            Transform selection;
            selection.SetPosition({player.position.x, player.position.y});

            float selectionScale = MatchRules::PlayerRadius
                / meshes[2].GetHalfExtents().x;

            selection.SetScale({selectionScale, selectionScale});

            renderer.Draw(
                meshes[2],
                shader,
                selection,
                projection,
                teamColor * 0.35f
            );
        }

        transforms[i].SetPosition({player.position.x, player.position.y});

        //the original triangle points along +Y, rather than +X
        transforms[i].SetRotation(aimAngle - halfPi);

        transforms[i].SetScale({
            MatchRules::PlayerVisualScale,
            MatchRules::PlayerVisualScale
        });

        glm::vec3 bodyColor = player.IsSpawnProtected(game.GetTime())
            ? glm::vec3(0.95f, 0.95f, 0.95f)
            : teamColor;

        renderer.Draw(meshes[i], shader, transforms[i], projection, bodyColor);

        //small direction marker also makes aiming visible on a circle
        DrawRectangle(
            player.position + player.aimDirection * 0.084f,
            glm::vec2(0.09f, 0.012f),
            glm::vec3(1.0f, 1.0f, 1.0f),
            projection,
            aimAngle
        );

        constexpr float barWidth = 0.24f;
        constexpr float barHeight = 0.025f;

        glm::vec2 barPosition(
            player.position.x,
            player.position.y + MatchRules::PlayerRadius + 0.04f
        );

        DrawRectangle(
            barPosition,
            glm::vec2(barWidth, barHeight),
            glm::vec3(0.2f, 0.2f, 0.2f),
            projection
        );

        float healthRatio = static_cast<float>(player.health)
            / static_cast<float>(MatchRules::StartingHealth);

        float filledWidth = barWidth * healthRatio;

        //adjust the centre so the bar shrinks from right to left
        glm::vec2 filledPosition(
            barPosition.x - (barWidth - filledWidth) * 0.5f,
            barPosition.y
        );

        DrawRectangle(
            filledPosition,
            glm::vec2(filledWidth, barHeight),
            glm::vec3(0.25f, 0.9f, 0.35f),
            projection
        );
    }
}

void Application::DrawShots(const glm::mat4& projection){
    for(const ShotTrace& shot : game.GetShots()){
        if(shot.expiresAt <= game.GetTime()){
            continue;
        }

        glm::vec2 difference = shot.end - shot.start;
        float length = glm::length(difference);

        if(length <= 0.0f){
            continue;
        }

        glm::vec2 centre = (shot.start + shot.end) * 0.5f;
        float angle = std::atan2(difference.y, difference.x);

        DrawRectangle(
            centre,
            glm::vec2(length, 0.008f),
            glm::vec3(1.0f, 0.85f, 0.3f),
            projection,
            angle
        );
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