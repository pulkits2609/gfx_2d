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
        std::string lobbyCode;
        bool closing = false;
        std::uint64_t id;
        std::string name;
        std::string sessionToken;
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
                }
                catch(const std::exception&){ Reject("Could not create session"); return; }
                response["name"] = name;
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

GameServer::GameServer(boost::asio::io_context& io, const std::string& address, unsigned short port):
    context(io), acceptor(io){
    tcp::endpoint endpoint(boost::asio::ip::make_address(address), port);
    acceptor.open(endpoint.protocol());
    acceptor.set_option(tcp::acceptor::reuse_address(true));
    acceptor.bind(endpoint);
    acceptor.listen();
}

GameServer::~GameServer(){ Stop(); }

void GameServer::Start(){
    if(running) return;
    running = true;
    AcceptConnection();
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
    auto active = sessions;
    for(auto& entry : active) entry.second->Close();
}

unsigned short GameServer::GetPort() const{ return acceptor.local_endpoint().port(); }

//Lobby commands stay on TCP: they are small, reliable decisions rather than movement.
bool GameServer::HandleLobby(std::uint64_t id, const Protocol::Json& message){
    auto session = sessions.at(id);
    std::string type = message["type"].get<std::string>();
    if(type != "create_lobby" && type != "join_lobby" && type != "choose_team"
        && type != "start_match" && type != "leave_lobby") return false;
    auto reject = [&](const std::string& reason){
        auto response = Protocol::Message("notice");
        response["message"] = reason;
        session->Send(response);
    };
    if(type == "leave_lobby"){
        LeaveLobby(id);
        session->Send(Protocol::Message("lobby_left"));
        return true;
    }
    if(type == "create_lobby"){
        if(!session->lobbyCode.empty()){ reject("Leave your current lobby first"); return true; }
        if(!message.contains("teams") || !message["teams"].is_number_integer()
            || message["teams"] < 2 || message["teams"] > 4
            || !message.contains("minutes") || !message["minutes"].is_number_integer()
            || message["minutes"] < LobbyRules::MinimumMinutes || message["minutes"] > LobbyRules::MaximumMinutes){
            reject("Choose 2-4 teams and a duration of 1-30 minutes"); return true;
        }
        if(lobbies.size() >= 32){ reject("Server has too many lobbies"); return true; }
        Lobby lobby;
        const std::string alphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
        do{
            lobby.code.clear();
            std::string random = MakeSessionToken();
            for(int i = 0; i < 6; i++)
                lobby.code += alphabet[std::stoul(random.substr(i * 2, 2), nullptr, 16) % alphabet.size()];
        }while(lobbies.count(lobby.code));
        lobby.hostId = id;
        lobby.teams = message["teams"].get<int>();
        lobby.minutes = message["minutes"].get<int>();
        lobby.members.push_back({id, session->name, 0});
        session->lobbyCode = lobby.code;
        auto inserted = lobbies.emplace(lobby.code, std::move(lobby));
        BroadcastLobby(inserted.first->second);
        return true;
    }
    if(type == "join_lobby"){
        if(!session->lobbyCode.empty()){ reject("Leave your current lobby first"); return true; }
        if(!message.contains("code") || !message["code"].is_string()){
            reject("Enter a six-character lobby code"); return true;
        }
        std::string code = LobbyRules::NormalizeCode(message["code"].get<std::string>());
        auto found = lobbies.find(code);
        if(!LobbyRules::ValidCode(code) || found == lobbies.end()){
            reject("Lobby code was not found"); return true;
        }
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
        session->lobbyCode = code;
        BroadcastLobby(lobby);
        return true;
    }
    auto found = lobbies.find(session->lobbyCode);
    if(found == lobbies.end()){ reject("Join or create a lobby first"); return true; }
    Lobby& lobby = found->second;
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
        lobby.playing = true;
    }
    BroadcastLobby(lobby);
    return true;
}

void GameServer::BroadcastLobby(const Lobby& lobby){
    auto message = Protocol::Message("lobby");
    message["code"] = lobby.code;
    message["hostId"] = lobby.hostId;
    message["teams"] = lobby.teams;
    message["minutes"] = lobby.minutes;
    message["playing"] = lobby.playing;
    message["members"] = Protocol::Json::array();
    for(const auto& member : lobby.members)
        message["members"].push_back({{"playerId", member.playerId}, {"name", member.name}, {"team", member.team}});
    //Copy recipients: a slow client's full queue may close it and change the lobby.
    std::vector<std::shared_ptr<ControlSession>> recipients;
    for(const auto& member : lobby.members){
        auto found = sessions.find(member.playerId);
        if(found != sessions.end()) recipients.push_back(found->second);
    }
    for(const auto& recipient : recipients) recipient->Send(message);
}

void GameServer::LeaveLobby(std::uint64_t id){
    auto found = sessions.find(id);
    if(found == sessions.end()) return;
    std::string code = found->second->lobbyCode;
    found->second->lobbyCode.clear();
    auto room = lobbies.find(code);
    if(room == lobbies.end()) return;
    if(room->second.hostId == id){
        auto members = room->second.members;
        lobbies.erase(room);
        for(const auto& member : members){
            auto other = sessions.find(member.playerId);
            if(other == sessions.end() || member.playerId == id) continue;
            other->second->lobbyCode.clear();
            auto closed = Protocol::Message("lobby_closed");
            closed["message"] = "The host left the lobby";
            other->second->Send(closed);
        }
    }
    else{
        auto& members = room->second.members;
        members.erase(std::remove_if(members.begin(), members.end(),
            [id](const LobbyMember& player){ return player.playerId == id; }), members.end());
        BroadcastLobby(room->second);
    }
}
