#include"Network/gameClient.hpp"
#include<chrono>
#include<cmath>
#include<iostream>
#include<thread>

//A real client process using the same networking and prediction as the windowed game.
//The Python test starts two of these against an independent server.
int main(int argc, char* argv[]){
    try{
        if(argc < 3) throw std::runtime_error("Expected port and host/join role");
        bool host = std::string(argv[2]) == "host";
        double requiredSeconds = argc > 4 ? std::stod(argv[4]) : 2.0;
        GameClient client;
        client.Connect("127.0.0.1", static_cast<unsigned short>(std::stoi(argv[1])), host ? "Host" : "Guest");
        auto start = std::chrono::steady_clock::now();
        auto previous = start;
        auto nextStart = start;
        bool requested = false, printed = false, sawTwo = false;
        bool localMoved = false, remoteMoved = false;
        bool checkedPrediction = false;
        bool tappedFire = false;
        glm::vec2 localStart{0}, remoteStart{0};
        double playingFor = 0;
        //The UDP recovery fixture deliberately waits in the lobby for 5.2s;
        //leave room for a second client process to start on busy Windows hosts.
        while(std::chrono::steady_clock::now() - start < std::chrono::seconds(20)){
            auto now = std::chrono::steady_clock::now();
            double dt = std::chrono::duration<double>(now - previous).count();
            previous = now;
            client.Poll();
            if(client.GetState() == GameClient::State::Failed) throw std::runtime_error(client.GetStatus());
            if(client.GetState() == GameClient::State::Connected && client.UDPReady() && !requested){
                if(host) client.CreateLobby(2, 1);
                else client.JoinLobby(argv[3]);
                requested = true;
            }
            const auto& lobby = client.GetLobby();
            if(lobby.is_object() && !lobby["playing"].get<bool>() && now >= nextStart){
                bool ownReady = false;
                for(const auto& member : lobby["members"])
                    if(member["playerId"] == client.GetPlayerId()) ownReady = member["ready"].get<bool>();
                if(!ownReady) client.SetReady(true);
                if(!host) nextStart = now + std::chrono::milliseconds(300);
            }
            if(host && lobby.is_object()){
                if(!printed){ std::cout << "CODE " << lobby["code"].get<std::string>() << std::endl; printed = true; }
                if(lobby["members"].size() == 2 && !lobby["playing"].get<bool>() && now >= nextStart){
                    client.StartMatch();
                    nextStart = now + std::chrono::milliseconds(300);
                }
            }
            if(client.HasSnapshot()){
                playingFor += dt;
                PlayerInput input;
                input.movement = {0, playingFor < 0.6 ? (host ? -1.0f : 1.0f) : 0.0f};
                input.boost = true;
                if(playingFor > 1.25 && !tappedFire){
                    PlayerInput tap;
                    tap.shoot = tap.hasAim = true;
                    tap.aimPosition = host ? glm::vec2(-1.5f, 1) : glm::vec2(1.5f, -1);
                    client.UpdateGameplay(0, tap); //release before a complete input tick
                    tappedFire = true;
                }
                if(!checkedPrediction){
                    client.UpdateGameplay(0, input);
                    glm::vec2 before = client.GetGame().GetPlayers()[client.GetOwnSlot()].position;
                    //No Poll call and no full input tick: this movement must
                    //come from local prediction, before any server reply.
                    client.UpdateGameplay(0.005, input);
                    if(glm::length(client.GetGame().GetPlayers()[client.GetOwnSlot()].position - before) < 0.001f)
                        throw std::runtime_error("Local prediction did not respond immediately");
                    checkedPrediction = true;
                }
                client.UpdateGameplay(dt, input);
                const auto& players = client.GetGame().GetPlayers();
                const Player* local = nullptr;
                const Player* remote = nullptr;
                for(const auto& player : players){
                    if(!player.active) continue;
                    if(player.id == client.GetPlayerId()) local = &player;
                    else remote = &player;
                }
                if(local && remote){
                    if(!sawTwo){ localStart = local->position; remoteStart = remote->position; sawTwo = true; }
                    localMoved = localMoved || glm::length(local->position - localStart) > 0.05f;
                    remoteMoved = remoteMoved || glm::length(remote->position - remoteStart) > 0.05f;
                }
                if(playingFor > requiredSeconds && sawTwo && localMoved && remoteMoved){
                    if(client.GetPingMilliseconds() < 0 || client.GetSnapshotAgeMilliseconds() < 0)
                        throw std::runtime_error("Connected gameplay is missing HUD network measurements");
                    if(std::abs(client.GetGame().GetTime() - playingFor) > .5)
                        throw std::runtime_error("Server clock changed speed with the tick rate");
                    if(players[client.GetOwnSlot()].lastShotId == 0)
                        throw std::runtime_error("Quick click was lost before reaching the server");
                    Protocol::Json positions = Protocol::Json::array();
                    for(const auto& player : players) if(player.active)
                        positions.push_back({{"id", player.id}, {"x", player.position.x}, {"y", player.position.y}});
                    std::cout << "STATE " << positions.dump() << std::endl;
                    std::cout << client.GetStatus() << std::endl;
                    std::cout << "PASS " << client.GetPlayerId() << " local and remote movement synchronized" << std::endl;
                    //Keep the host session alive until the other process has also checked its view.
                    auto finish = std::chrono::steady_clock::now() + std::chrono::milliseconds(400);
                    while(std::chrono::steady_clock::now() < finish){ client.Poll(); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
                    return 0;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        throw std::runtime_error("Two-client movement checkpoint timed out");
    }
    catch(const std::exception& error){ std::cerr << error.what() << '\n'; return 1; }
}
