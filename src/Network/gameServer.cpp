#include"Network/gameServer.hpp"
#include<chrono>
#include<fstream>
#include<iomanip>
#include<iostream>
#include<sstream>
#include<deque>
#include<algorithm>
#ifdef _WIN32
#include<windows.h>
#include<bcrypt.h>
#endif

using boost::asio::ip::tcp;
using boost::asio::ip::udp;
using ErrorCode = boost::system::error_code;

namespace{
    std::string MakeSessionToken(){
        std::array<unsigned char, 32> bytes{};
#ifdef _WIN32
        if(BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) throw std::runtime_error("Session randomness unavailable");
#else
        std::ifstream random("/dev/urandom", std::ios::binary);
        if(!random.read(reinterpret_cast<char*>(bytes.data()), bytes.size()))
            throw std::runtime_error("Session randomness unavailable");
#endif
        std::ostringstream token;
        for(unsigned char byte : bytes) token << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
        return token.str();
    }
}

//one session owns one socket. Callbacks keep it alive until outstanding work finishes.
class ControlSession : public std::enable_shared_from_this<ControlSession>{
    friend class GameServer;
    private:
        GameServer& server;
        tcp::socket socket;
        boost::asio::steady_timer deadline;
        Protocol::Header header{};
        std::string body;
        struct Reply{ std::vector<std::uint8_t> bytes; bool closeAfter; };
        std::deque<Reply> replies;
        boost::asio::steady_timer writeDeadline;
        std::uint64_t lobbyId = 0;
        bool closing = false;
        bool udpReady = false;
        udp::endpoint gameplayEndpoint;
        std::deque<GameplayPackets::InputCommand> inputs;
        PlayerInput lastInput;
        GameplayPackets::InputCommand pendingShot;
        std::chrono::steady_clock::time_point shotTime{};
        std::uint32_t lastReceived = 0, lastProcessed = 0, lastReload = 0;
        std::chrono::steady_clock::time_point inputTime{};
        std::chrono::steady_clock::time_point udpRateWindow{};
        unsigned udpCount = 0;
        std::uint64_t id;
        std::string name;
        std::string sessionToken;
        std::string rejoinToken;
        bool closed = false;
        bool welcomed = false;
        unsigned messagesThisSecond = 0;
        std::chrono::steady_clock::time_point rateWindow = std::chrono::steady_clock::now();

        void SetDeadline(int seconds){
            deadline.expires_after(std::chrono::seconds(seconds));
            auto self = shared_from_this();
            deadline.async_wait([self](ErrorCode error){ if(!error) self->Close(); });
        }

        void ReadHeader(){
            if(closed || closing) return;
            SetDeadline(welcomed ? 30 : 5);
            auto self = shared_from_this();
            boost::asio::async_read(socket, boost::asio::buffer(header), [self](ErrorCode error, std::size_t){
                if(error){ self->Close(); return; }
                std::uint32_t length = Protocol::DecodeLength(self->header);
                if(length == 0 || length > Protocol::MaximumBodySize){ self->Close(); return; }
                self->body.assign(length, '\0');
                self->ReadBody();
            });
        }

        void ReadBody(){
            SetDeadline(5);
            auto self = shared_from_this();
            boost::asio::async_read(socket, boost::asio::buffer(body), [self](ErrorCode error, std::size_t){
                if(error){ self->Close(); return; }
                self->HandleMessage();
                self->ReadHeader();
            });
        }

        void WriteNext(){
            writeDeadline.expires_after(std::chrono::seconds(5));
            auto self = shared_from_this();
            writeDeadline.async_wait([self](ErrorCode error){ if(!error) self->Close(); });
            boost::asio::async_write(socket, boost::asio::buffer(replies.front().bytes),
                [self](ErrorCode error, std::size_t){
                    self->writeDeadline.cancel();
                    if(error || self->closed){ self->Close(); return; }
                    bool closeAfter = self->replies.front().closeAfter;
                    self->replies.pop_front();
                    if(closeAfter){ self->Close(); return; }
                    if(!self->replies.empty()) self->WriteNext();
                });
        }

        void Send(const Protocol::Json& message, bool closeAfter = false){
            if(closed || closing) return;
            if(replies.size() >= 16){ Close(); return; }
            try{ replies.push_back({Protocol::Encode(message), closeAfter}); }
            catch(const std::exception&){ Close(); return; }
            closing = closeAfter;
            if(replies.size() == 1) WriteNext();
        }

        void Reject(const std::string& reason){
            auto response = Protocol::Message("error");
            response["message"] = reason;
            Send(response, true);
        }

        void HandleMessage(){
            auto now = std::chrono::steady_clock::now();
            if(now - rateWindow >= std::chrono::seconds(1)){
                rateWindow = now;
                messagesThisSecond = 0;
            }
            if(++messagesThisSecond > 30){ Reject("Too many control messages"); return; }
            Protocol::Json message;
            std::string error;
            if(!Protocol::Parse(body, message, error)){ Reject(error); return; }
            std::string type = message["type"].get<std::string>();

            if(!welcomed){
                if(type != "hello" || !message.contains("name") || !message["name"].is_string()
                    || !Protocol::ValidName(message["name"].get_ref<const std::string&>())){
                    Reject("First message must be hello with a printable name of 1-24 characters"); return;
                }
                name = message["name"].get<std::string>();
                auto response = Protocol::Message("welcome");
                response["playerId"] = id;
                try{
                    sessionToken = MakeSessionToken();
                    response["sessionToken"] = sessionToken;
                    rejoinToken = MakeSessionToken();
                    response["rejoinToken"] = rejoinToken;
                }
                catch(const std::exception&){ Reject("Could not create session"); return; }
                response["name"] = name;
                response["gameplayPort"] = server.gameplaySocket.local_endpoint().port();
                welcomed = true;
                Send(response);
                return;
            }

            if(server.HandleLobby(id, message)) return;
            auto response = Protocol::Message("error");
            if(type == "ping"){
                response["type"] = "pong";
            }
            else if(type == "players"){
                response["type"] = "players";
                response["players"] = Protocol::Json::array();
                for(const auto& entry : server.sessions){
                    const auto& other = entry.second;
                    if(other->welcomed && !other->closed)
                        response["players"].push_back({{"playerId", other->id}, {"name", other->name}});
                }
            }
            else if(type == "goodbye"){
                response["type"] = "goodbye";
                Send(response, true);
                return;
            }
            else{
                response["message"] = "Unknown control message";
            }
            if(message.contains("requestId")) response["requestId"] = message["requestId"];
            Send(response);
        }

    public:
        ControlSession(GameServer& owner, tcp::socket connection, std::uint64_t playerId):
            server(owner), socket(std::move(connection)), deadline(socket.get_executor()),
            writeDeadline(socket.get_executor()), id(playerId){
            socket.set_option(tcp::no_delay(true));
        }

        void Start(){ ReadHeader(); }

        void Close(){
            if(closed) return;
            closed = true;
            ErrorCode ignored;
            deadline.cancel();
            writeDeadline.cancel();
            server.LeaveLobby(id);
            socket.shutdown(tcp::socket::shutdown_both, ignored);
            socket.close(ignored);
            server.sessions.erase(id);
        }
};

GameServer::GameServer(boost::asio::io_context& io, const std::string& address, unsigned short port,
    unsigned short gameplayPort):context(io), acceptor(io), gameplaySocket(io), tickTimer(io){
    tcp::endpoint endpoint(boost::asio::ip::make_address(address), port);
    acceptor.open(endpoint.protocol());
    acceptor.set_option(tcp::acceptor::reuse_address(true));
    acceptor.bind(endpoint);
    acceptor.listen();
    udp::endpoint gameplay(boost::asio::ip::make_address(address), gameplayPort);
    gameplaySocket.open(gameplay.protocol());
    gameplaySocket.bind(gameplay);
    gameplaySocket.non_blocking(true);
}

GameServer::~GameServer(){ Stop(); }

void GameServer::Start(){
    if(running) return;
    running = true;
    AcceptConnection();
    ReceiveGameplay();
    nextTick = std::chrono::steady_clock::now();
    ScheduleTick();
}

void GameServer::AcceptConnection(){
    acceptor.async_accept([this](ErrorCode error, tcp::socket socket){
        if(!error && running){
            if(sessions.size() >= Protocol::MaximumSessions){
                ErrorCode ignored;
                socket.close(ignored);
            }
            else{
                auto session = std::make_shared<ControlSession>(*this, std::move(socket), nextPlayerId++);
                sessions.emplace(nextPlayerId - 1, session);
                session->Start();
            }
        }
        if(running) AcceptConnection();
    });
}

void GameServer::Stop(){
    running = false;
    ErrorCode ignored;
    acceptor.close(ignored);
    gameplaySocket.close(ignored);
    tickTimer.cancel();
    auto active = sessions;
    for(auto& entry : active) entry.second->Close();
}

unsigned short GameServer::GetPort() const{ return acceptor.local_endpoint().port(); }

//Lobby commands stay on TCP: they are small, reliable decisions rather than movement.
bool GameServer::HandleLobby(std::uint64_t id, const Protocol::Json& message){
    auto session = sessions.at(id);
    std::string type = message["type"].get<std::string>();
    if(type != "create_lobby" && type != "join_lobby" && type != "choose_team"
        && type != "list_lobbies" && type != "join_public"
        && type != "start_match" && type != "leave_lobby" && type != "return_lobby" && type != "rejoin_match") return false;
    auto reject = [&](const std::string& reason){
        auto response = Protocol::Message("notice");
        response["message"] = reason;
        session->Send(response);
    };
    if(type == "list_lobbies"){
        auto response = Protocol::Message("lobby_list");
        response["lobbies"] = Protocol::Json::array();
        //This process owns this list. Private rooms and running matches never appear.
        for(const auto& entry : lobbies){
            const Lobby& lobby = entry.second;
            if(!lobby.isPublic || lobby.playing) continue;
            response["lobbies"].push_back({{"lobbyId", lobby.id}, {"host", lobby.members.front().name},
                {"teams", lobby.teams}, {"minutes", lobby.minutes}, {"players", lobby.members.size()},
                {"capacity", lobby.teams * LobbyRules::PlayersPerTeam(lobby.teams)}});
        }
        session->Send(response);
        return true;
    }
    if(type == "rejoin_match"){
        if(!message.contains("token") || !message["token"].is_string() || message["token"].get_ref<const std::string&>().size() != 64){
            reject("Invalid rejoin ticket"); return true;
        }
        RejoinMatch(id, message["token"].get<std::string>());
        return true;
    }
    if(type == "leave_lobby"){
        LeaveLobby(id);
        session->Send(Protocol::Message("lobby_left"));
        return true;
    }
    if(type == "create_lobby"){
        if(session->lobbyId != 0){ reject("Leave your current lobby first"); return true; }
        if(!message.contains("teams") || !message["teams"].is_number_integer()
            || message["teams"] < 2 || message["teams"] > 4
            || !message.contains("minutes") || !message["minutes"].is_number_integer()
            || message["minutes"] < LobbyRules::MinimumMinutes || message["minutes"] > LobbyRules::MaximumMinutes){
            reject("Choose 2-4 teams and a duration of 1-30 minutes"); return true;
        }
        if(lobbies.size() >= 32){ reject("Server has too many lobbies"); return true; }
        if(message.contains("public") && !message["public"].is_boolean()){
            reject("Choose public or private visibility"); return true;
        }
        Lobby lobby;
        lobby.id = nextLobbyId++;
        lobby.isPublic = message.value("public", false);
        const std::string alphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
        if(!lobby.isPublic) do{
            lobby.code.clear();
            std::string random = MakeSessionToken();
            for(int i = 0; i < 6; i++)
                lobby.code += alphabet[std::stoul(random.substr(i * 2, 2), nullptr, 16) % alphabet.size()];
        }while(std::any_of(lobbies.begin(), lobbies.end(), [&](const auto& entry){
            return entry.second.code == lobby.code;
        }));
        lobby.hostId = id;
        lobby.teams = message["teams"].get<int>();
        lobby.minutes = message["minutes"].get<int>();
        lobby.members.push_back({id, session->name, 0});
        reservations.erase(session->rejoinToken);
        session->lobbyId = lobby.id;
        auto inserted = lobbies.emplace(lobby.id, std::move(lobby));
        BroadcastLobby(inserted.first->second);
        return true;
    }
    if(type == "join_lobby" || type == "join_public"){
        if(session->lobbyId != 0){ reject("Leave your current lobby first"); return true; }
        auto found = lobbies.end();
        if(type == "join_public"){
            if(!message.contains("lobbyId") || !message["lobbyId"].is_number_unsigned()){
                reject("Choose a public lobby from this server's list"); return true;
            }
            found = lobbies.find(message["lobbyId"].get<std::uint64_t>());
            if(found != lobbies.end() && !found->second.isPublic) found = lobbies.end();
        }
        else{
            if(!message.contains("code") || !message["code"].is_string()){
                reject("Enter a six-character lobby code"); return true;
            }
            std::string code = LobbyRules::NormalizeCode(message["code"].get<std::string>());
            if(LobbyRules::ValidCode(code)) found = std::find_if(lobbies.begin(), lobbies.end(), [&](const auto& entry){
                return !entry.second.isPublic && entry.second.code == code;
            });
        }
        if(found == lobbies.end()){ reject("Lobby was not found on this server"); return true; }
        Lobby& lobby = found->second;
        if(lobby.playing){ reject("This match has already started"); return true; }
        if(lobby.members.size() >= static_cast<std::size_t>(lobby.teams * LobbyRules::PlayersPerTeam(lobby.teams))){
            reject("Lobby is full"); return true;
        }
        //Give newcomers an available starting team; they can choose another in the lobby.
        std::array<int, 4> counts{};
        for(const auto& member : lobby.members) counts[member.team]++;
        int team = static_cast<int>(std::min_element(counts.begin(), counts.begin() + lobby.teams) - counts.begin());
        lobby.members.push_back({id, session->name, team});
        reservations.erase(session->rejoinToken);
        session->lobbyId = lobby.id;
        BroadcastLobby(lobby);
        return true;
    }
    auto found = lobbies.find(session->lobbyId);
    if(found == lobbies.end()){ reject("Join or create a lobby first"); return true; }
    Lobby& lobby = found->second;
    if(type == "return_lobby"){
        if(lobby.hostId != id || !lobby.finished){ reject("Only the host can return after the match ends"); return true; }
        lobby.playing = false;
        lobby.finished = false;
        lobby.game.reset();
        lobby.members.erase(std::remove_if(lobby.members.begin(), lobby.members.end(),
            [](const LobbyMember& member){ return !member.connected; }), lobby.members.end());
        BroadcastLobby(lobby);
        return true;
    }
    if(lobby.playing){ reject("Teams are locked during the match"); return true; }
    if(type == "choose_team"){
        if(!message.contains("team") || !message["team"].is_number_integer()
            || message["team"] < 0 || message["team"] >= lobby.teams){ reject("Invalid team"); return true; }
        int team = message["team"].get<int>();
        auto member = std::find_if(lobby.members.begin(), lobby.members.end(),
            [id](const LobbyMember& player){ return player.playerId == id; });
        int count = static_cast<int>(std::count_if(lobby.members.begin(), lobby.members.end(),
            [team](const LobbyMember& player){ return player.team == team; }));
        if(member->team != team && count >= LobbyRules::PlayersPerTeam(lobby.teams)){
            reject("That team is full"); return true;
        }
        member->team = team;
    }
    else if(type == "start_match"){
        if(lobby.hostId != id){ reject("Only the host can start the match"); return true; }
        int hostTeam = lobby.members.front().team;
        bool opponent = std::any_of(lobby.members.begin(), lobby.members.end(),
            [hostTeam](const LobbyMember& player){ return player.team != hostTeam; });
        if(!opponent){ reject("At least one player must be on an opposing team"); return true; }
        for(const auto& member : lobby.members){
            auto other = sessions.at(member.playerId);
            if(!other->udpReady){ reject("Waiting for every player's gameplay connection"); return true; }
        }
        lobby.game = std::make_unique<Game>(lobby.teams, false);
        for(const auto& member : lobby.members){
            lobby.game->AddPlayer(member.playerId, member.name, member.team);
            auto other = sessions.at(member.playerId);
            other->inputs.clear();
            other->lastInput = {};
            other->pendingShot = {};
            other->lastReceived = other->lastProcessed = other->lastReload = 0;
            other->inputTime = std::chrono::steady_clock::now();
        }
        lobby.match = nextMatch++;
        lobby.tick = 0;
        lobby.snapshotTime = 0;
        lobby.finished = false;
        lobby.playing = true;
    }
    BroadcastLobby(lobby);
    return true;
}

void GameServer::BroadcastLobby(const Lobby& lobby){
    auto message = Protocol::Message("lobby");
    message["lobbyId"] = lobby.id;
    message["public"] = lobby.isPublic;
    message["code"] = lobby.code;
    message["hostId"] = lobby.hostId;
    message["teams"] = lobby.teams;
    message["minutes"] = lobby.minutes;
    message["playing"] = lobby.playing;
    message["match"] = lobby.match;
    message["members"] = Protocol::Json::array();
    for(const auto& member : lobby.members)
        message["members"].push_back({{"playerId", member.playerId}, {"name", member.name}, {"team", member.team}, {"connected", member.connected}});
    //Copy recipients: a slow client's full queue may close it and change the lobby.
    std::vector<std::shared_ptr<ControlSession>> recipients;
    for(const auto& member : lobby.members){
        auto found = sessions.find(member.playerId);
        if(member.connected && found != sessions.end()) recipients.push_back(found->second);
    }
    for(const auto& recipient : recipients) recipient->Send(message);
}

void GameServer::LeaveLobby(std::uint64_t id){
    auto found = sessions.find(id);
    if(found == sessions.end()) return;
    std::uint64_t lobbyId = found->second->lobbyId;
    found->second->lobbyId = 0;
    auto room = lobbies.find(lobbyId);
    if(room == lobbies.end()) return;
    if(room->second.hostId == id){
        auto members = room->second.members;
        lobbies.erase(room);
        for(const auto& member : members){
            auto other = sessions.find(member.playerId);
            if(other == sessions.end() || member.playerId == id || other->second->lobbyId != lobbyId) continue;
            other->second->lobbyId = 0;
            auto closed = Protocol::Message("lobby_closed");
            closed["message"] = "The host left the lobby";
            other->second->Send(closed);
        }
    }
    else{
        Lobby& lobby = room->second;
        if(lobby.playing && !lobby.finished && running){
            auto member = std::find_if(lobby.members.begin(), lobby.members.end(),
                [id](const LobbyMember& player){ return player.playerId == id; });
            if(member == lobby.members.end()) return;
            member->connected = false;
            lobby.game->RemovePlayer(id);
            double remaining = lobby.minutes * 60.0 - lobby.game->GetTime();
            if(LobbyRules::CanRejoin(0, remaining, false)){
                reservations[found->second->rejoinToken] = {lobbyId, id, std::chrono::steady_clock::now()};
                auto offer = Protocol::Message("rejoin_available");
                offer["token"] = found->second->rejoinToken;
                offer["code"] = lobby.code;
                offer["remaining"] = remaining;
                offer["seconds"] = LobbyRules::RejoinWindow;
                found->second->Send(offer);
            }
        }
        else{
            auto& members = lobby.members;
            members.erase(std::remove_if(members.begin(), members.end(),
                [id](const LobbyMember& player){ return player.playerId == id; }), members.end());
        }
        BroadcastLobby(lobby);
    }
    ExpireReservations();
}

bool GameServer::RejoinMatch(std::uint64_t id, const std::string& token){
    auto session = sessions.at(id);
    auto deny = [&](const std::string& reason){
        auto response = Protocol::Message("rejoin_denied"); response["message"] = reason; session->Send(response);
    };
    if(session->lobbyId != 0){ deny("Leave the current lobby before rejoining"); return false; }
    if(!session->udpReady){
        auto response = Protocol::Message("notice"); response["message"] = "Waiting for the gameplay connection"; session->Send(response); return false;
    }
    auto ticket = reservations.find(token);
    if(ticket == reservations.end()){ deny("This rejoin window has expired or the host ended the match"); return false; }
    auto room = lobbies.find(ticket->second.lobbyId);
    double away = std::chrono::duration<double>(std::chrono::steady_clock::now() - ticket->second.leftAt).count();
    if(room == lobbies.end() || !room->second.playing || !room->second.game
        || !LobbyRules::CanRejoin(away, room->second.minutes * 60.0 - room->second.game->GetTime(), room->second.finished)){
        reservations.erase(ticket);
        deny("Cannot rejoin: the minute expired, the match ended, or fewer than 2 minutes remain"); return false;
    }
    Lobby& lobby = room->second;
    auto member = std::find_if(lobby.members.begin(), lobby.members.end(), [&](const LobbyMember& player){
        return player.playerId == ticket->second.playerId && !player.connected;
    });
    if(member == lobby.members.end() || !lobby.game->ResumePlayer(ticket->second.playerId, id)){
        deny("The reserved player could not be restored"); return false;
    }
    session->name = member->name;
    member->playerId = id;
    member->connected = true;
    session->lobbyId = lobby.id;
    session->inputs.clear(); session->lastInput = {}; session->pendingShot = {};
    session->lastReceived = session->lastProcessed = session->lastReload = 0;
    session->inputTime = std::chrono::steady_clock::now();
    reservations.erase(ticket); //consume this reservation; a later leave starts a new window
    BroadcastLobby(lobby);
    return true;
}

void GameServer::ExpireReservations(){
    auto now = std::chrono::steady_clock::now();
    std::vector<std::shared_ptr<ControlSession>> expired;
    for(auto ticket = reservations.begin(); ticket != reservations.end();){
        auto room = lobbies.find(ticket->second.lobbyId);
        double away = std::chrono::duration<double>(now - ticket->second.leftAt).count();
        bool allowed = room != lobbies.end() && room->second.playing && room->second.game
            && LobbyRules::CanRejoin(away, room->second.minutes * 60.0 - room->second.game->GetTime(), room->second.finished);
        if(allowed){ ++ticket; continue; }
        auto previous = sessions.find(ticket->second.playerId);
        ticket = reservations.erase(ticket);
        if(previous != sessions.end()) expired.push_back(previous->second);
    }
    //Send after iteration because closing a slow connection can change the rooms.
    for(const auto& session : expired){
        auto response = Protocol::Message("rejoin_denied");
        response["message"] = "Rejoin is no longer available: the window expired, less than 2 minutes remain, or the host left";
        session->Send(response);
    }
}

void GameServer::SendDatagram(const std::vector<std::uint8_t>& bytes, const udp::endpoint& endpoint){
    //A nonblocking send either reaches the OS queue immediately or is dropped.
    //UDP snapshots are replaceable, so there is no growing send queue.
    ErrorCode error;
    gameplaySocket.send_to(boost::asio::buffer(bytes), endpoint, 0, error);
}

void GameServer::ReceiveGameplay(){
    gameplaySocket.async_receive_from(boost::asio::buffer(datagram), sender, [this](ErrorCode error, std::size_t size){
        if(!running) return;
        if(!error) HandleGameplay(size);
        ReceiveGameplay();
    });
}

void GameServer::HandleGameplay(std::size_t size){
    GameplayPackets::Packet packet;
    if(!GameplayPackets::Decode(datagram.data(), size, packet)) return;
    auto found = sessions.find(packet.identity.id);
    if(found == sessions.end()) return;
    auto session = found->second;
    if(!session->welcomed || session->closed || packet.identity.token != session->sessionToken) return;
    auto now = std::chrono::steady_clock::now();
    if(now - session->udpRateWindow >= std::chrono::seconds(1)){
        session->udpRateWindow = now;
        session->udpCount = 0;
    }
    if(++session->udpCount > 80) return;
    if(packet.type == GameplayPackets::Type::Register){
        ErrorCode error;
        auto tcpPeer = session->socket.remote_endpoint(error);
        if(error || sender.address() != tcpPeer.address()) return;
        if(session->udpReady && sender != session->gameplayEndpoint) return;
        session->gameplayEndpoint = sender;
        session->udpReady = true;
        SendDatagram(GameplayPackets::Registration(GameplayPackets::Type::Registered, packet.identity), sender);
        return;
    }
    if(packet.type != GameplayPackets::Type::Input || !session->udpReady || sender != session->gameplayEndpoint) return;
    auto room = lobbies.find(session->lobbyId);
    if(room == lobbies.end() || !room->second.playing || room->second.finished || packet.match != room->second.match) return;
    for(const auto& command : packet.commands){
        if(!GameplayPackets::Newer(command.sequence, session->lastReceived)) continue;
        if(session->inputs.size() >= 8) break;
        session->inputs.push_back(command);
        session->lastReceived = command.sequence;
        session->inputTime = now;
        if(command.input.shoot && GameplayPackets::Newer(command.shot, session->pendingShot.shot)){
            session->pendingShot = command;
            session->shotTime = now;
        }
    }
}

void GameServer::ScheduleTick(){
    nextTick += std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(MatchRules::ServerStep));
    tickTimer.expires_at(nextTick);
    tickTimer.async_wait([this](ErrorCode error){
        if(error || !running) return;
        Tick();
        //A long OS stall must not create an unbounded catch-up loop.
        auto now = std::chrono::steady_clock::now();
        if(now - nextTick > std::chrono::milliseconds(250)) nextTick = now;
        ScheduleTick();
    });
}

void GameServer::Tick(){
    auto now = std::chrono::steady_clock::now();
    std::vector<std::shared_ptr<ControlSession>> disconnected;
    for(auto& entry : lobbies){
        Lobby& lobby = entry.second;
        if(!lobby.playing || !lobby.game) continue;
        lobby.tick++;
        std::array<PlayerInput, Game::PlayerCount> inputs{};
        if(!lobby.finished){
            const auto& players = lobby.game->GetPlayers();
            for(std::size_t i = 0; i < players.size(); i++){
                if(!players[i].active) continue;
                auto found = sessions.find(players[i].id);
                if(found == sessions.end()) continue;
                auto session = found->second;
                if(now - session->inputTime > std::chrono::seconds(5)){
                    disconnected.push_back(session);
                    continue;
                }
                session->lastInput.reload = false;
                if(now - session->inputTime > std::chrono::duration<double>(MatchRules::InputTimeout)){
                    session->lastInput = {};
                    session->inputs.clear();
                }
                else if(!session->inputs.empty()){
                    auto command = session->inputs.front();
                    session->inputs.pop_front();
                    session->lastInput = command.input;
                    session->lastInput.shotId = command.shot;
                    session->lastInput.shoot = command.input.shoot && command.shot != 0;
                    session->lastInput.reload = GameplayPackets::Newer(command.reload, session->lastReload);
                    if(session->lastInput.reload) session->lastReload = command.reload;
                    session->lastProcessed = command.sequence;
                }
                inputs[i] = session->lastInput;
                //Keep a quick click through the next valid firing tick; a release
                //packet must not erase it just because the cooldown has a few ms left.
                if(GameplayPackets::Newer(session->pendingShot.shot, players[i].lastShotId)
                    && now - session->shotTime < std::chrono::milliseconds(250)){
                    inputs[i].shoot = inputs[i].hasAim = true;
                    inputs[i].shotId = session->pendingShot.shot;
                    inputs[i].aimPosition = session->pendingShot.input.aimPosition;
                }
            }
            double remaining = lobby.minutes * 60.0 - lobby.game->GetTime();
            lobby.game->Update(std::min(MatchRules::ServerStep, remaining), inputs);
            lobby.finished = lobby.game->GetTime() + 0.000001 >= lobby.minutes * 60.0;
            if(lobby.finished) SendResults(lobby);
        }
        lobby.snapshotTime += MatchRules::ServerStep;
        if(lobby.snapshotTime + 0.000001 >= MatchRules::SnapshotStep){
            lobby.snapshotTime -= MatchRules::SnapshotStep;
            SendSnapshots(lobby);
        }
    }
    for(const auto& session : disconnected) session->Close();
    ExpireReservations();
}

void GameServer::SendSnapshots(Lobby& lobby){
    GameplayPackets::Snapshot snapshot;
    snapshot.match = lobby.match;
    snapshot.tick = lobby.tick;
    snapshot.state = lobby.game->GetState();
    snapshot.remaining = lobby.finished ? 0.0f
        : static_cast<float>(std::max(0.0, lobby.minutes * 60.0 - snapshot.state.time));
    snapshot.finished = lobby.finished;
    for(const auto& member : lobby.members){
        auto found = sessions.find(member.playerId);
        if(!member.connected || found == sessions.end() || !found->second->udpReady) continue;
        auto session = found->second;
        snapshot.acknowledged = session->lastProcessed;
        SendDatagram(GameplayPackets::State({session->id, session->sessionToken}, snapshot), session->gameplayEndpoint);
    }
}

void GameServer::SendResults(const Lobby& lobby){
    auto result = Protocol::Message("match_results");
    result["match"] = lobby.match;
    result["winningTeam"] = lobby.game->GetWinningTeam();
    result["scores"] = lobby.game->GetTeamScores();
    result["players"] = Protocol::Json::array();
    for(const auto& player : lobby.game->GetPlayers()){
        if(player.id == 0) continue;
        result["players"].push_back({{"playerId", player.id}, {"name", player.name},
            {"team", player.team}, {"kills", player.kills}, {"deaths", player.deaths}});
    }
    std::vector<std::shared_ptr<ControlSession>> recipients;
    for(const auto& member : lobby.members){
        auto found = sessions.find(member.playerId);
        if(member.connected && found != sessions.end()) recipients.push_back(found->second);
    }
    for(const auto& recipient : recipients)
        boost::asio::post(context, [recipient, result]{ recipient->Send(result); });
}
