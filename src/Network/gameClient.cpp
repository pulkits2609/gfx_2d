#include"Network/gameClient.hpp"
#include"Network/lobby.hpp"
#include<algorithm>
#include<chrono>
#include<stdexcept>
#include<cmath>

using boost::asio::ip::tcp;
using boost::asio::ip::udp;
namespace{
    double Now(){ return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
}
using ErrorCode = boost::system::error_code;

GameClient::~GameClient(){
    Disconnect();
    //finish cancellation callbacks while their socket buffers still exist
    context.restart();
    context.run();
}

void GameClient::SetDeadline(int seconds){
    deadline.expires_after(std::chrono::seconds(seconds));
    deadline.async_wait([this](ErrorCode error){ if(!error) Fail("Control connection timed out"); });
}

void GameClient::Connect(const std::string& host, unsigned short port, const std::string& displayName){
    if(started) throw std::logic_error("Create a fresh GameClient to reconnect");
    if(host.empty() || port == 0 || !Protocol::ValidName(displayName))
        throw std::invalid_argument("Invalid server address, port or player name");
    started = true;
    name = displayName;
    serverHost = host;
    serverPort = port;
    state = State::Connecting;
    status = "CONNECTING TCP";
    SetDeadline(5);
    resolver.async_resolve(host, std::to_string(port), [this](ErrorCode error, tcp::resolver::results_type endpoints){
        if(state != State::Connecting) return;
        if(error){ Fail("Could not resolve server"); return; }
        boost::asio::async_connect(socket, endpoints, [this](ErrorCode error, const tcp::endpoint&){
            if(state != State::Connecting) return;
            if(error){ Fail("Could not connect to server"); return; }
            auto hello = Protocol::Message("hello");
            hello["name"] = name;
            Send(hello);
            ReadHeader();
        });
    });
}

void GameClient::Poll(){
    context.restart();
    //bound callback work per frame as well as avoiding blocking reads
    for(int i = 0; i < 32; i++) if(context.poll_one() == 0) break;
    if(state == State::Connected && waitingSince > 0 && !hasSnapshot && Now() - waitingSince > 5)
        Fail("The match started but no gameplay snapshot arrived");
}

void GameClient::Disconnect(){
    if(lobby.is_object() && lobby["playing"].get<bool>() && lobby["hostId"] != playerId
        && hasSnapshot && !latest.finished){
        RememberRejoin(rejoinToken, lobby["code"].get<std::string>(),
            latest.remaining - std::max(0.0, Now() - lastSnapshotAt));
    }
    state = State::Offline;
    ErrorCode ignored;
    resolver.cancel();
    deadline.cancel();
    heartbeat.cancel();
    registrationTimer.cancel();
    gameplaySocket.close(ignored);
    udpReady = false;
    ResetGameplay();
    lobby = nullptr;
    status = "OFFLINE";
    socket.shutdown(tcp::socket::shutdown_both, ignored);
    socket.close(ignored);
    sessionToken.clear();
}

void GameClient::Fail(const std::string& reason){
    if(state == State::Offline || state == State::Failed) return;
    Disconnect();
    state = State::Failed;
    status = "OFFLINE: " + reason.substr(0, 80);
}

void GameClient::Send(const Protocol::Json& message){
    if(state != State::Connected && state != State::Connecting) return;
    if(outgoing.size() >= 8){ Fail("Control output queue full"); return; }
    try{ outgoing.push_back(Protocol::Encode(message)); }
    catch(const std::exception&){ Fail("Invalid outgoing frame"); return; }
    if(outgoing.size() == 1) WriteNext();
}

void GameClient::WriteNext(){
    boost::asio::async_write(socket, boost::asio::buffer(outgoing.front()), [this](ErrorCode error, std::size_t){
        if(state == State::Offline || state == State::Failed) return;
        if(error){ Fail("Control write failed"); return; }
        outgoing.pop_front();
        if(!outgoing.empty()) WriteNext();
    });
}

void GameClient::ReadHeader(){
    if(state == State::Offline || state == State::Failed) return;
    SetDeadline(state == State::Connected ? 30 : 5);
    boost::asio::async_read(socket, boost::asio::buffer(header), [this](ErrorCode error, std::size_t){
        if(state == State::Offline || state == State::Failed) return;
        if(error){ Fail("Server disconnected"); return; }
        std::uint32_t length = Protocol::DecodeLength(header);
        if(length == 0 || length > Protocol::MaximumBodySize){ Fail("Invalid server frame size"); return; }
        body.assign(length, '\0');
        ReadBody();
    });
}

void GameClient::ReadBody(){
    SetDeadline(5);
    boost::asio::async_read(socket, boost::asio::buffer(body), [this](ErrorCode error, std::size_t){
        if(state == State::Offline || state == State::Failed) return;
        if(error){ Fail("Incomplete server frame"); return; }
        HandleMessage();
        ReadHeader();
    });
}

void GameClient::HandleMessage(){
    Protocol::Json message;
    std::string error;
    if(!Protocol::Parse(body, message, error)){ Fail(error); return; }
    std::string type = message["type"].get<std::string>();
    if(type == "error"){
        Fail(message.contains("message") && message["message"].is_string()
            ? message["message"].get<std::string>() : "Control request rejected");
        return;
    }
    if(state == State::Connecting){
        if(type != "welcome" || !message.contains("playerId") || !message["playerId"].is_number_unsigned()
            || message["playerId"].get<std::uint64_t>() == 0 || !message.contains("sessionToken")
            || !message["sessionToken"].is_string()){
            Fail("Invalid welcome response"); return;
        }
        sessionToken = message["sessionToken"].get<std::string>();
        if(sessionToken.size() != 64 || !std::all_of(sessionToken.begin(), sessionToken.end(), [](char c){
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        })){ Fail("Invalid session token"); return; }
        playerId = message["playerId"].get<std::uint64_t>();
        if(!message.contains("rejoinToken") || !message["rejoinToken"].is_string()
            || message["rejoinToken"].get_ref<const std::string&>().size() != 64){ Fail("Invalid rejoin token"); return; }
        rejoinToken = message["rejoinToken"].get<std::string>();
        if(!message.contains("gameplayPort") || !message["gameplayPort"].is_number_unsigned()
            || message["gameplayPort"] == 0 || message["gameplayPort"] > 65535){ Fail("Invalid gameplay port"); return; }
        state = State::Connected;
        StartGameplay(message["gameplayPort"].get<unsigned short>());
        if(state == State::Failed) return;
        status = "TCP #" + std::to_string(playerId) + " / WAITING FOR UDP";
        SendPing();
        Send(Protocol::Message("players"));
        StartHeartbeat();
    }
    else if(type == "rejoin_available"){
        if(!message.contains("token") || !message["token"].is_string() || message["token"] != rejoinToken
            || !message.contains("code") || !message["code"].is_string()
            || !LobbyRules::ValidCode(message["code"].get<std::string>())
            || !message.contains("remaining") || !message["remaining"].is_number()){
            Fail("Invalid rejoin offer"); return;
        }
        RememberRejoin(rejoinToken, message["code"].get<std::string>(), message["remaining"].get<double>());
    }
    else if(type == "rejoin_denied"){
        rejoin = {};
        notice = message.value("message", std::string("Rejoining is no longer available"));
    }
    else if(type == "match_results"){
        if(!lobby.is_object() || !message.contains("match") || message["match"] != lobby["match"]
            || !message.contains("winningTeam") || !message["winningTeam"].is_number_integer()
            || message["winningTeam"] < -1 || message["winningTeam"] >= lobby["teams"]
            || !message.contains("scores") || !message["scores"].is_array() || message["scores"].size() != 4
            || !message.contains("players") || !message["players"].is_array() || message["players"].size() > Game::PlayerCount){
            Fail("Invalid match results"); return;
        }
        for(const auto& score : message["scores"])
            if(!score.is_number_unsigned() || score.get<std::uint64_t>() > 65535){ Fail("Invalid final score"); return; }
        for(const auto& player : message["players"]){
            if(!player.is_object() || !player.contains("name") || !player["name"].is_string()
                || !Protocol::ValidName(player["name"].get<std::string>())
                || !player.contains("kills") || !player["kills"].is_number_unsigned() || player["kills"] > 65535
                || !player.contains("deaths") || !player["deaths"].is_number_unsigned() || player["deaths"] > 65535){
                Fail("Invalid result entry"); return;
            }
        }
        results = std::move(message);
    }
    else if(type == "notice"){
        notice = message.value("message", std::string("Request rejected"));
    }
    else if(type == "lobby"){
        if(!message.contains("code") || !message["code"].is_string()
            || !LobbyRules::ValidCode(message["code"].get<std::string>())
            || !message.contains("teams") || !message["teams"].is_number_integer()
            || message["teams"] < 2 || message["teams"] > 4
            || !message.contains("minutes") || !message["minutes"].is_number_integer()
            || message["minutes"] < 1 || message["minutes"] > 30
            || !message.contains("hostId") || !message["hostId"].is_number_unsigned()
            || !message.contains("match") || !message["match"].is_number_unsigned()
            || message["match"].get<std::uint64_t>() > 0xffffffffu
            || !message.contains("playing") || !message["playing"].is_boolean()
            || !message.contains("members") || !message["members"].is_array()
            || message["members"].empty() || message["members"].size() > 9){
            Fail("Invalid lobby state"); return;
        }
        for(const auto& member : message["members"]){
            if(!member.is_object() || !member.contains("playerId") || !member["playerId"].is_number_unsigned()
                || !member.contains("name") || !member["name"].is_string()
                || !Protocol::ValidName(member["name"].get<std::string>())
                || !member.contains("team") || !member["team"].is_number_integer()
                || member["team"] < 0 || member["team"] >= message["teams"]){
                Fail("Invalid lobby member"); return;
            }
        }
        if(!message["playing"].get<bool>() || !lobby.is_object() || lobby.value("match", 0u) != message["match"].get<std::uint32_t>())
            ResetGameplay();
        lobby = std::move(message);
        rejoin = {};
        if(!lobby["playing"].get<bool>()) status = "CONNECTED / LOBBY";
        if(lobby["playing"].get<bool>() && !hasSnapshot && waitingSince == 0){
            waitingSince = Now();
            status = "MATCH STARTING / WAITING FOR SNAPSHOTS";
        }
        notice.clear();
    }
    else if(type == "lobby_left" || type == "lobby_closed"){
        lobby = nullptr;
        ResetGameplay();
        status = RejoinSecondsLeft() > 0 ? "CONNECTED / REJOIN AVAILABLE" : "CONNECTED / HOME";
        notice = message.value("message", std::string());
    }
    else if(type == "pong"){
        receivedPong = true;
    }
    else if(type == "players"){
        if(!message.contains("players") || !message["players"].is_array()
            || message["players"].size() > Protocol::MaximumSessions){ Fail("Invalid player list"); return; }
        for(const auto& peer : message["players"]){
            if(!peer.is_object() || !peer.contains("playerId") || !peer["playerId"].is_number_unsigned()
                || !peer.contains("name") || !peer["name"].is_string()
                || !Protocol::ValidName(peer["name"].get_ref<const std::string&>())){
                Fail("Invalid player entry"); return;
            }
        }
        peers = message["players"];
        receivedPlayers = true;
    }
    else Fail("Unexpected server message");
}

void GameClient::SendPing(){
    if(state != State::Connected) return;
    auto message = Protocol::Message("ping");
    message["requestId"] = ++requestId;
    Send(message);
}

void GameClient::StartHeartbeat(){
    heartbeat.expires_after(std::chrono::seconds(10));
    heartbeat.async_wait([this](ErrorCode error){
        if(error || state != State::Connected) return;
        SendPing();
        StartHeartbeat();
    });
}

void GameClient::CreateLobby(int teams, int minutes){
    notice.clear();
    auto message = Protocol::Message("create_lobby");
    message["teams"] = teams;
    message["minutes"] = minutes;
    Send(message);
}

void GameClient::JoinLobby(const std::string& code){
    notice.clear();
    auto message = Protocol::Message("join_lobby");
    message["code"] = LobbyRules::NormalizeCode(code);
    Send(message);
}

void GameClient::ChooseTeam(int team){
    auto message = Protocol::Message("choose_team");
    message["team"] = team;
    Send(message);
}

void GameClient::StartMatch(){ Send(Protocol::Message("start_match")); }
void GameClient::LeaveLobby(){ Send(Protocol::Message("leave_lobby")); }

void GameClient::ReturnToLobby(){ Send(Protocol::Message("return_lobby")); }

void GameClient::StartGameplay(unsigned short port){
    ErrorCode error;
    auto remote = socket.remote_endpoint(error);
    if(error){ Fail("Missing server endpoint"); return; }
    udp::endpoint endpoint(remote.address(), port);
    gameplaySocket.open(endpoint.protocol(), error);
    if(!error) gameplaySocket.connect(endpoint, error);
    if(!error) gameplaySocket.non_blocking(true, error);
    if(error){ Fail("Could not open gameplay socket"); return; }
    ReceiveGameplay();
    RegisterGameplay();
}

void GameClient::SendDatagram(const std::vector<std::uint8_t>& bytes){
    if(!gameplaySocket.is_open()) return;
    ErrorCode error;
    gameplaySocket.send(boost::asio::buffer(bytes), 0, error);
}

void GameClient::RegisterGameplay(){
    if(state != State::Connected || udpReady) return;
    if(++registrationAttempts > 10){ Fail("UDP registration timed out; check the gameplay port"); return; }
    SendDatagram(GameplayPackets::Registration(GameplayPackets::Type::Register, {playerId, sessionToken}));
    registrationTimer.expires_after(std::chrono::milliseconds(500));
    registrationTimer.async_wait([this](ErrorCode error){ if(!error) RegisterGameplay(); });
}

void GameClient::ReceiveGameplay(){
    //The connected UDP socket accepts datagrams only from our chosen server endpoint.
    gameplaySocket.async_receive(boost::asio::buffer(datagram), [this](ErrorCode error, std::size_t size){
        if(state != State::Connected) return;
        GameplayPackets::Packet packet;
        if(!error && GameplayPackets::Decode(datagram.data(), size, packet)
            && packet.identity.id == playerId && packet.identity.token == sessionToken){
            if(packet.type == GameplayPackets::Type::Registered){
                udpReady = true;
                registrationTimer.cancel();
                status = "ONLINE / UDP READY";
            }
            else if(packet.type == GameplayPackets::Type::Snapshot && udpReady) AcceptSnapshot(std::move(packet.snapshot));
        }
        ReceiveGameplay();
    });
}

void GameClient::ResetGameplay(){
    results = nullptr;
    hasSnapshot = false;
    shotPreview.Reset();
    latest = {};
    snapshots.clear();
    pendingInputs.clear();
    sequence = reloadCounter = lastSentShot = 0;
    inputAccumulator = predictionTime = 0;
    waitingSince = 0;
    reloadPending = false;
    correction = {0, 0};
    previewInput = {};
    ownSlot = 0;
}

void GameClient::AcceptSnapshot(GameplayPackets::Snapshot snapshot){
    if(!lobby.is_object() || !lobby["playing"].get<bool>()
        || snapshot.match != lobby["match"].get<std::uint32_t>()
        || (hasSnapshot && !GameplayPackets::Newer(snapshot.tick, latest.tick))) return;
    std::size_t slot = Game::PlayerCount;
    for(std::size_t i = 0; i < snapshot.state.players.size(); i++){
        auto& player = snapshot.state.players[i];
        if(!player.active) continue;
        for(const auto& member : lobby["members"]){
            if(member["playerId"] == player.id){ player.name = member["name"].get<std::string>(); break; }
        }
        if(player.name.empty()) player.name = "Player";
        if(player.id == playerId) slot = i;
    }
    if(slot == Game::PlayerCount) return;
    glm::vec2 oldPosition = renderedGame.GetPlayers()[slot].position;
    bool sameLife = hasSnapshot && predicted.IsAlive() == snapshot.state.players[slot].IsAlive()
        && predicted.deaths == snapshot.state.players[slot].deaths
        && glm::length(predicted.position - snapshot.state.players[slot].position) < 0.5f;
    while(!pendingInputs.empty() && !GameplayPackets::Newer(pendingInputs.front().sequence, snapshot.acknowledged))
        pendingInputs.pop_front();
    predicted = snapshot.state.players[slot];
    predictionTime = snapshot.state.time;
    for(const auto& command : pendingInputs){
        predictionTime += MatchRules::ServerStep;
        Game::PredictMovement(predicted, command.input, MatchRules::ServerStep, predictionTime);
    }
    Player preview = predicted;
    Game::PredictMovement(preview, previewInput, inputAccumulator, predictionTime + inputAccumulator);
    correction = sameLife ? oldPosition - preview.position : glm::vec2(0);
    if(glm::length(correction) > 0.25f) correction = {0, 0};
    ownSlot = slot;
    shotPreview.Reconcile(snapshot, slot, Now());
    latest = std::move(snapshot);
    snapshots.push_back(latest);
    if(snapshots.size() > 32) snapshots.pop_front();
    lastSnapshotAt = Now();
    hasSnapshot = true;
    status = "ONLINE / 30 TICK / PREDICTION";
}

void GameClient::UpdateGameplay(double deltaTime, const PlayerInput& input){
    if(state != State::Connected || !hasSnapshot) return;
    previewInput = input;
    double age = Now() - lastSnapshotAt;
    if(age > 5){ Fail("Gameplay snapshots timed out"); return; }
    if(age > 1){ status = "WAITING FOR GAMEPLAY SNAPSHOTS"; return; }
    reloadPending = reloadPending || input.reload;
    if(!latest.finished){
        Player shooter = predicted;
        Game::PredictMovement(shooter, input, inputAccumulator, predictionTime + inputAccumulator);
        shotPreview.Update(shooter, input, ownSlot, predictionTime + inputAccumulator, Now());
    }
    inputAccumulator += std::clamp(deltaTime, 0.0, 0.25);
    while(inputAccumulator + 0.000001 >= MatchRules::ServerStep && !latest.finished){
        if(pendingInputs.size() >= 128){ Fail("Server stopped acknowledging inputs"); return; }
        GameplayPackets::InputCommand command;
        command.sequence = ++sequence;
        if(reloadPending){ ++reloadCounter; reloadPending = false; }
        command.reload = reloadCounter;
        command.shot = shotPreview.GetRequest();
        command.input = input;
        if(GameplayPackets::Newer(command.shot, lastSentShot)){
            //A click released between two 30 Hz input ticks still needs to reach the server.
            command.input.shoot = command.input.hasAim = true;
            command.input.aimPosition = shotPreview.GetAim();
            lastSentShot = command.shot;
        }
        command.input.reload = false;
        pendingInputs.push_back(command);
        //Repeat the last four commands. Sequence numbers make duplicates harmless,
        //and the reload counter survives a lost R packet without reloading twice.
        std::vector<GameplayPackets::InputCommand> recent;
        auto begin = pendingInputs.size() > 4 ? pendingInputs.end() - 4 : pendingInputs.begin();
        for(auto it = begin; it != pendingInputs.end(); ++it) recent.push_back(*it);
        SendDatagram(GameplayPackets::Inputs({playerId, sessionToken}, latest.match, recent));
        predictionTime += MatchRules::ServerStep;
        Game::PredictMovement(predicted, input, MatchRules::ServerStep, predictionTime);
        inputAccumulator -= MatchRules::ServerStep;
    }
    if(latest.finished) inputAccumulator = 0;
    RenderGameplay(deltaTime, input);
}

void GameClient::RenderGameplay(double deltaTime, const PlayerInput& input){
    if(latest.finished){
        //The final score must not come from an older interpolation frame.
        renderedGame.ApplyState(latest.state);
        return;
    }
    //Render other players about 100 ms behind the newest server time.
    //That gives us two real positions to blend instead of guessing their movement.
    double target = latest.state.time + std::max(0.0, Now() - lastSnapshotAt) - 0.10;
    while(snapshots.size() > 2 && snapshots[1].state.time <= target) snapshots.pop_front();
    const auto* before = &snapshots.front();
    const auto* after = before;
    for(const auto& snapshot : snapshots){
        if(snapshot.state.time <= target) before = &snapshot;
        if(snapshot.state.time >= target){ after = &snapshot; break; }
        after = &snapshot;
    }
    Game::State display = before->state;
    double span = after->state.time - before->state.time;
    float fraction = span > 0 ? static_cast<float>(std::clamp((target - before->state.time) / span, 0.0, 1.0)) : 0;
    display.time = before->state.time + span * fraction;
    for(std::size_t i = 0; i < display.players.size(); i++){
        auto& player = display.players[i];
        const auto& next = after->state.players[i];
        if(!player.active || !next.active || player.id != next.id || !player.IsAlive() || !next.IsAlive()
            || player.deaths != next.deaths || glm::length(player.position - next.position) > 0.5f) continue;
        player.position = glm::mix(player.position, next.position, fraction);
        glm::vec2 aim = glm::mix(player.aimDirection, next.aimDirection, fraction);
        if(glm::length(aim) > 0.0001f) player.aimDirection = glm::normalize(aim);
    }
    for(auto& bullet : display.projectiles){
        float travel = static_cast<float>(std::max(0.0, display.time - before->state.time)) * MatchRules::ProjectileSpeed;
        travel = std::min(travel, Arena().GetRayExitDistance(bullet.position, bullet.direction, bullet.remainingDistance));
        bullet.position += bullet.direction * travel;
    }
    Player local = predicted;
    if(!latest.finished) Game::PredictMovement(local, input, inputAccumulator, predictionTime + inputAccumulator);
    if(input.hasAim){
        glm::vec2 aim = input.aimPosition - local.position;
        if(glm::length(aim) > 0.0001f) local.aimDirection = glm::normalize(aim);
    }
    correction *= std::exp(-18.0f * static_cast<float>(std::clamp(deltaTime, 0.0, 0.25)));
    local.position = Arena().ClampPosition(local.position + correction, MatchRules::PlayerRadius);
    display.players[ownSlot] = local;
    shotPreview.Draw(display, ownSlot, Now());
    renderedGame.ApplyState(display);
}

void GameClient::RememberRejoin(const std::string& token, const std::string& code, double remaining){
    if(token.empty() || !LobbyRules::CanRejoin(0, remaining, false)) return;
    rejoin = {token, code, serverHost, name, serverPort, Now(), remaining};
}

double GameClient::RejoinSecondsLeft() const{ return rejoin.SecondsLeft(Now()); }

void GameClient::RejoinMatch(){
    if(RejoinSecondsLeft() <= 0){ notice = "The rejoin window has expired or fewer than 2 minutes remain"; return; }
    auto message = Protocol::Message("rejoin_match");
    message["token"] = rejoin.token;
    Send(message);
}
