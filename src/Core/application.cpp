#include"Core/application.hpp"
#include<algorithm>
#include<glm/gtc/matrix_transform.hpp>
#include<cmath>
#include<string>
#include<cstdio>
#include<imgui.h>

Application::Application(int numberOfTeams, const std::string& serverHost,
    unsigned short controlPort, const std::string& name, const std::string& voiceClient):window(
    1280,
    800,
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
hud(window.GetNativeWindow(), settings, controller),
voice(voiceClient){
    settings.Load();
    menu.teams = numberOfTeams;
    menu.port = controlPort;
    std::snprintf(menu.name, sizeof(menu.name), "%s", name.c_str());
    if(!serverHost.empty()) std::snprintf(menu.address, sizeof(menu.address), "%s", serverHost.c_str());
}
    

void Application::RunApplication(){
    GameLoop();
}

void Application::GameLoop(){
    double previousTime = glfwGetTime();
    double accumulator = 0.0;

    while(true){
        window.PollEvents();
        controller.Poll(settings.controllerId);
        if(window.ShouldClose()){
            bool inMatch = network && network->GetLobby().is_object() && network->GetLobby()["playing"].get<bool>();
            if(!inMatch || closingConfirmed) break;
            glfwSetWindowShouldClose(window.GetNativeWindow(), GLFW_FALSE);
            if(glfwGetWindowAttrib(window.GetNativeWindow(), GLFW_ICONIFIED)) glfwRestoreWindow(window.GetNativeWindow());
            quitAfterLeave = true;
            hud.RequestLeave(true);
        }
        if(network) network->Poll();
        voice.Update(network.get());
        if(pendingAction != MenuAction::None && network){
            if(network->GetState() == GameClient::State::Connected
                && (pendingAction != MenuAction::Rejoin || network->UDPReady())){
                if(pendingAction == MenuAction::Create) network->CreateLobby(menu.teams, menu.minutes, menu.isPublic);
                else if(pendingAction == MenuAction::Browse) network->FetchLobbies();
                else if(pendingAction == MenuAction::Rejoin) network->RejoinMatch();
                else network->JoinLobby(menu.code);
                pendingAction = MenuAction::None;
            }
            else if(network->GetState() == GameClient::State::Failed) pendingAction = MenuAction::None;
        }

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
            if(network) network->UpdateGameplay(deltaTime, {});
            glfwWaitEventsTimeout(0.05);
            accumulator = 0.0;
            continue;
        }

        bool focused = glfwGetWindowAttrib(nativeWindow, GLFW_FOCUSED) == GLFW_TRUE;
        bool escape = focused && (glfwGetKey(nativeWindow, GLFW_KEY_ESCAPE) == GLFW_PRESS
            || controller.GetInput(settings.boostBinding, settings.fireBinding).pausePressed);
        bool online = network && network->GetState() == GameClient::State::Connected
            && network->GetLobby().is_object() && network->GetLobby()["playing"].get<bool>()
            && network->HasSnapshot();
        if(wasOnline && !online){ menu.page = 0; quitAfterLeave = false; pendingReload = {}; }
        wasOnline = online;
        if(escape && !escapeWasPressed){
            if(online) hud.RequestLeave();
            else if(offlinePractice) offlinePractice = false;
        }
        escapeWasPressed = escape;
        if(!offlinePractice && !online){
            accumulator = 0;
            renderer.Clear(0.025f, 0.03f, 0.04f, 1.0f);
            HandleMenu(hud.DrawMenu(menu, network.get(), pendingAction != MenuAction::None, &voice));
            window.SwapBuffers();
            continue;
        }

        int windowWidth, windowHeight;
        glfwGetWindowSize(nativeWindow, &windowWidth, &windowHeight);
        ViewLayout view = ViewLayout::Calculate(static_cast<float>(windowWidth), static_cast<float>(windowHeight));
        glm::mat4 projection = glm::ortho(-view.halfWidth, view.halfWidth,
            -view.halfHeight, view.halfHeight, -1.0f, 1.0f);
        if(online){
            game = network->GetGame();
            selectedPlayer = network->GetOwnSlot();
        }
        PlayerInput input = ReadInput(nativeWindow, view);
        if(hud.IsLeaveOpen() || hud.IsSettingsOpen()){
            input = {};
            pendingReload = {};
        }

        input.reload = pendingReload[selectedPlayer];
        if(online){
            network->UpdateGameplay(deltaTime, input);
            game = network->GetGame();
            pendingReload[selectedPlayer] = false;
            accumulator = 0;
        }
        else accumulator += std::min(deltaTime, 0.25);

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
        float pixelX = static_cast<float>(framebufferWidth) / windowWidth;
        float pixelY = static_cast<float>(framebufferHeight) / windowHeight;
        glViewport(static_cast<int>(view.left * pixelX),
            static_cast<int>((windowHeight - view.top - view.height) * pixelY),
            static_cast<int>(view.width * pixelX), static_cast<int>(view.height * pixelY));
        DrawArena(projection);
        DrawPlayers(projection);
        DrawShots(projection);
        glViewport(0, 0, framebufferWidth, framebufferHeight);
        MenuAction hudAction = hud.Draw(game, selectedPlayer, view,
            online ? network->GetStatus() : "OFFLINE PRACTICE / ESC MENU", online ? network.get() : nullptr, &voice);
        HandleMenu(hudAction);

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
    const ViewLayout& view
){
    PlayerInput input;

    //do not move or fire while another window has focus
    if(glfwGetWindowAttrib(nativeWindow, GLFW_FOCUSED) != GLFW_TRUE){
        reloadWasPressed = false;
        return input;
    }

    for(std::size_t i = 0; offlinePractice && i < 4; i++){
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

    ControllerState pad = controller.GetInput(settings.boostBinding, settings.fireBinding);
    if(pad.movement.x != 0 || pad.movement.y != 0) input.movement = pad.movement;
    bool reloadPressed = keyboardinput.isKeyPressed(nativeWindow, GLFW_KEY_R) || pad.reload;
    if(reloadPressed && !reloadWasPressed) pendingReload[selectedPlayer] = true;
    reloadWasPressed = reloadPressed;
    input.boost = keyboardinput.isKeyPressed(nativeWindow, GLFW_KEY_LEFT_SHIFT)
        || keyboardinput.isKeyPressed(nativeWindow, GLFW_KEY_RIGHT_SHIFT) || pad.boost;

    int windowWidth;
    int windowHeight;
    glfwGetWindowSize(nativeWindow, &windowWidth, &windowHeight);

    if(windowWidth <= 0 || windowHeight <= 0){
        return input;
    }

    double cursorX;
    double cursorY;
    glfwGetCursorPos(nativeWindow, &cursorX, &cursorY);

    bool mouseFire = glfwGetMouseButton(nativeWindow, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    glm::dvec2 cursor(cursorX, cursorY);
    bool mouseMoved = cursorKnown && glm::length(cursor - previousCursor) > 1.0;
    previousCursor = cursor;
    cursorKnown = true;
    if(mouseMoved || mouseFire || controller.ActiveId() < 0) controllerAim = false;
    bool padActive = pad.aiming || pad.fire || pad.boost || pad.movement.x != 0 || pad.movement.y != 0;
    if(padActive && !mouseMoved && !mouseFire) controllerAim = true;
    if(pad.aiming && !mouseFire){
        lastControllerAim = pad.aimDirection;
        controllerAim = true;
    }
    input.aimPosition = view.ToWorld(static_cast<float>(cursorX), static_cast<float>(cursorY));
    if(controllerAim){
        const Player& player = game.GetPlayers()[selectedPlayer];
        //Keep the last stick direction when released; never aim back at an idle mouse cursor.
        if(glm::length(lastControllerAim) < 0.001f) lastControllerAim = player.aimDirection;
        input.aimPosition = player.position + lastControllerAim * MatchRules::ShotRange;
    }
    input.hasAim = true;
    bool cursorInside = cursorX >= view.left && cursorX <= view.left + view.width
        && cursorY >= view.top && cursorY <= view.top + view.height;

    input.shoot = pad.fire || (cursorInside && !ImGui::GetIO().WantCaptureMouse && mouseFire);

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

        //reuse the chosen team mesh; collision always uses the shared circular hitbox
        transforms[i].SetPosition({player.position.x, player.position.y});
        transforms[i].SetRotation(aimAngle - halfPi);
        transforms[i].SetScale({MatchRules::PlayerVisualScale, MatchRules::PlayerVisualScale});
        glm::vec3 bodyColor = teamColor;
        bool protectedPlayer = player.IsSpawnProtected(game.GetTime());
        if(protectedPlayer) bodyColor = glm::mix(teamColor, glm::vec3(1.0f), 0.25f);
        int shape = player.team;
        bool hasOnlineShapes = !offlinePractice && network && network->GetLobby().is_object();
        if(hasOnlineShapes){
            shape = network->GetLobby()["shapes"][player.team].get<int>();
        }
        renderer.Draw(meshes[static_cast<std::size_t>(shape)], shader, transforms[i], projection, bodyColor);

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
    constexpr float OuterStreakWidth = 0.010f;
    constexpr float InnerStreakWidth = 0.004f;
    constexpr float OuterBrightness = 0.65f;
    constexpr float InnerHighlight = 0.35f;

    for(const Projectile& bullet : game.GetProjectiles()){
        float age = static_cast<float>(game.GetTime() - bullet.firedAt);
        float length = std::min(MatchRules::TracerLength, age * MatchRules::ProjectileSpeed);
        if(length <= 0) continue;
        glm::vec2 centre = bullet.position - bullet.direction * (length * 0.5f);
        float angle = std::atan2(bullet.direction.y, bullet.direction.x);
        //Keep the bright streak, but use its team colour to identify friendly fire.
        glm::vec3 teamColor = GameUI::TeamColor(bullet.team);
        glm::vec3 innerColor = glm::mix(teamColor, glm::vec3(1.0f), InnerHighlight);
        DrawRectangle(centre, glm::vec2(length, OuterStreakWidth), teamColor * OuterBrightness, projection, angle);
        DrawRectangle(centre, glm::vec2(length, InnerStreakWidth), innerColor, projection, angle);
    }
}

void Application::UpdateWindowTitle(){
    const Player& player = game.GetPlayers()[selectedPlayer];

    std::string title = offlinePractice ? "GFX_2D | 1-4 select | OFFLINE" : "GFX_2D | ONLINE";
    title += " | WASD move | Mouse aim | LMB shoot";
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

void Application::HandleMenu(MenuAction action){
    if(action != MenuAction::None) menu.error.clear();
    if(action == MenuAction::AskLeave) hud.RequestLeave();
    else if(action == MenuAction::CancelLeave) quitAfterLeave = false;
    else if(action == MenuAction::Offline){
        network.reset();
        pendingAction = MenuAction::None;
        game = Game(menu.teams);
        selectedPlayer = 0;
        pendingReload = {};
        offlinePractice = true;
    }
    else if(action == MenuAction::Rejoin && network){
        if(network->RejoinSecondsLeft() <= 0){ menu.error = "Rejoining is no longer available."; return; }
        if(network->GetState() == GameClient::State::Connected && network->UDPReady()) network->RejoinMatch();
        else{
            RejoinInfo ticket = network->GetRejoinInfo();
            network = std::make_unique<GameClient>();
            network->SetRejoinInfo(ticket);
            network->Connect(ticket.host, ticket.port, ticket.name);
            pendingAction = MenuAction::Rejoin;
        }
    }
    else if(action == MenuAction::Create || action == MenuAction::Join || action == MenuAction::Browse){
        if(network && network->GetState() == GameClient::State::Connected){
            if(action == MenuAction::Create) network->CreateLobby(menu.teams, menu.minutes, menu.isPublic);
            else if(action == MenuAction::Browse) network->FetchLobbies();
            else network->JoinLobby(menu.code);
        }
        else{
            if(menu.port < 1 || menu.port > 65535){ menu.error = "Choose a TCP port from 1 to 65535."; return; }
            if(!Protocol::ValidName(menu.name)){ menu.error = "Use a name of 1-24 printable characters, without spaces at either end."; return; }
            if(!menu.address[0]){ menu.error = "Enter the server address."; return; }
            if(action == MenuAction::Join && !LobbyRules::ValidCode(LobbyRules::NormalizeCode(menu.code))){
                menu.error = "Enter the six-character lobby code shown by the host."; return;
            }
            network = std::make_unique<GameClient>();
            network->Connect(menu.address, static_cast<unsigned short>(menu.port), menu.name);
            pendingAction = action;
        }
    }
    else if(action == MenuAction::Disconnect){
        network.reset();
        pendingAction = MenuAction::None;
    }
    else if(network){
        if(action == MenuAction::JoinPublic) network->JoinPublicLobby(menu.chosenLobby);
        else if(action == MenuAction::Ready) network->SetReady(menu.ready);
        else if(action == MenuAction::ChooseShape) network->ChooseTeamShape(menu.chosenTeam, menu.chosenShape);
        else if(action == MenuAction::ChooseTeam) network->ChooseTeam(menu.chosenTeam);
        else if(action == MenuAction::Start) network->StartMatch();
        else if(action == MenuAction::Leave){
            network->LeaveLobby();
            menu.page = 0;
            if(quitAfterLeave){
                closingConfirmed = true;
                glfwSetWindowShouldClose(window.GetNativeWindow(), GLFW_TRUE);
            }
        }
        else if(action == MenuAction::Return) network->ReturnToLobby();
    }
}
