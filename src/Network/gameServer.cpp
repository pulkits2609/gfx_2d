#include"Network/gameServer.hpp"
#include"Core/debugLog.hpp"
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
    long long AgeMilliseconds(std::chrono::steady_clock::time_point time){
        if(time == std::chrono::steady_clock::time_point{}) return -1;
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - time).count();
    }

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
        //These counters describe server activity, not proof of remote delivery.
        std::uint64_t udpPackets = 0, inputCommands = 0, snapshotAttempts = 0, snapshotErrors = 0;
        std::chrono::steady_clock::time_point lastGameplayPacket{}, nextGameplayLog{}, nextUdpWarning{};
        std::string closeReason = "server closed the control session";
        std::uint64_t id;
        std::string name;
        std::string sessionToken;
        std::string rejoinToken;
        bool closed = false;
        bool welcomed = false;
        std::uint32_t voiceSession = 0;
        bool voiceBound = false;
        unsigned messagesThisSecond = 0;
        std::chrono::steady_clock::time_point rateWindow = std::chrono::steady_clock::now();

        void SetDeadline(int seconds, const char* phase){
            deadline.expires_after(std::chrono::seconds(seconds));
            auto self = shared_from_this();
            deadline.async_wait([self, seconds, phase](ErrorCode error){
                if(!error) self->Close(std::string(phase) + " timed out after " + std::to_string(seconds) + "s");
            });
        }

        void ReadHeader(){
            if(closed || closing) return;
            SetDeadline(welcomed ? 30 : 5, welcomed ? "TCP control header" : "TCP hello");
            auto self = shared_from_this();
            boost::asio::async_read(socket, boost::asio::buffer(header), [self](ErrorCode error, std::size_t){
                if(error){ self->Close("TCP header read: " + error.message()); return; }
                std::uint32_t length = Protocol::DecodeLength(self->header);
                if(length == 0 || length > Protocol::MaximumBodySize){ self->Close("invalid TCP frame length: " + std::to_string(length)); return; }
                self->body.assign(length, '\0');
                self->ReadBody();
            });
        }

        void ReadBody(){
            SetDeadline(5, "TCP control body");
            auto self = shared_from_this();
            boost::asio::async_read(socket, boost::asio::buffer(body), [self](ErrorCode error, std::size_t){
                if(error){ self->Close("TCP body read: " + error.message()); return; }
                self->HandleMessage();
                self->ReadHeader();
            });
        }

        void WriteNext(){
            writeDeadline.expires_after(std::chrono::seconds(5));
            auto self = shared_from_this();
            writeDeadline.async_wait([self](ErrorCode error){ if(!error) self->Close("TCP write timed out after 5s"); });
            boost::asio::async_write(socket, boost::asio::buffer(replies.front().bytes),
                [self](ErrorCode error, std::size_t){
                    self->writeDeadline.cancel();
                    if(error || self->closed){ self->Close("TCP write: " + error.message()); return; }
                    bool closeAfter = self->replies.front().closeAfter;
                    self->replies.pop_front();
                    if(closeAfter){ self->Close(self->closeReason); return; }
                    if(!self->replies.empty()) self->WriteNext();
                });
        }

        void Send(const Protocol::Json& message, bool closeAfter = false){
            if(closed || closing) return;
            if(replies.size() >= 16){ Close("TCP reply queue reached 16 messages"); return; }
            try{ replies.push_back({Protocol::Encode(message), closeAfter}); }
            catch(const std::exception&){ Close("could not encode TCP reply"); return; }
            closing = closeAfter;
            if(replies.size() == 1) WriteNext();
        }

        void Reject(const std::string& reason){
            GFX_DEBUG_LOG("[CONTROL REJECT] player=" << id << " reason=" << reason);
            closeReason = reason;
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
                GFX_DEBUG_LOG("[WELCOME] player=" << id << " name=" << std::quoted(name)
                    << " gameplay_port=" << server.gameplaySocket.local_endpoint().port());
                Send(response);
                return;
            }

            if(server.HandleVoice(id, message) || server.HandleLobby(id, message)) return;
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
                closeReason = "client sent goodbye";
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

        void Start(){
            ErrorCode error;
            auto remote = socket.remote_endpoint(error);
            GFX_DEBUG_LOG("[CONNECT] player=" << id << " tcp=" << (error ? "unknown" : remote.address().to_string())
                << ':' << (error ? 0 : remote.port()));
            ReadHeader();
        }

        void Close(const std::string& reason = "connection closed"){
            if(closed) return;
            GFX_DEBUG_LOG("[DISCONNECT] player=" << id << " name=" << std::quoted(name) << " lobby=" << lobbyId
                << " reason=" << std::quoted(reason) << " udp_ready=" << udpReady << " udp_packets=" << udpPackets
                << " inputs=" << inputCommands << " snapshots_attempted=" << snapshotAttempts << " snapshot_errors=" << snapshotErrors
                << " last_udp_ms=" << AgeMilliseconds(lastGameplayPacket) << " last_input_ms=" << AgeMilliseconds(inputTime)
                << " received_seq=" << lastReceived << " processed_seq=" << lastProcessed);
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
    unsigned short gameplayPort, VoiceServerSettings voiceSettings):context(io), acceptor(io), gameplaySocket(io), tickTimer(io),
    voice(io, std::move(voiceSettings)), voiceShutdown(io){
    tcp::endpoint endpoint(boost::asio::ip::make_address(address), port);
    acceptor.open(endpoint.protocol());
    acceptor.set_option(tcp::acceptor::reuse_address(true));
    acceptor.bind(endpoint);
    acceptor.listen();
    udp::endpoint gameplay(boost::asio::ip::make_address(address), gameplayPort);
    gameplaySocket.open(gameplay.protocol());
    gameplaySocket.bind(gameplay);
    gameplaySocket.non_blocking(true);
    voice.onReady = [this](bool){ ReconcileVoice(); };
    voice.onLost = [this](bool permanent, const std::string& reason){ VoiceLost(permanent, reason); };
    voice.onEvent = [this](const Protocol::Json& event){
        if(event.value("event", "") != "group_deleted") return;
        const auto group = event.at("data").at("group_id").get<std::uint32_t>();
        voiceAllocations.erase(group);
        std::vector<std::uint64_t> affected;
        for(const auto& entry : lobbies) if(entry.second.voiceGroup == group) affected.push_back(entry.first);
        for(auto lobby : affected) DisableVoice(lobby, "Voice channels were removed; gameplay continues");
    };
}

GameServer::~GameServer(){ Stop(); }

void GameServer::Start(){
    if(running) return;
    running = true;
    GFX_DEBUG_LOG("[SERVER START] tcp=" << acceptor.local_endpoint() << " udp=" << gameplaySocket.local_endpoint());
    voice.Start();
    AcceptConnection();
    ReceiveGameplay();
    nextTick = std::chrono::steady_clock::now();
    ScheduleTick();
}

void GameServer::AcceptConnection(){
    acceptor.async_accept([this](ErrorCode error, tcp::socket socket){
        if(!error && running){
            if(sessions.size() >= Protocol::MaximumSessions){
                GFX_DEBUG_LOG("[CONNECT REJECT] server session limit reached");
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
    if(!running){ voiceShutdown.cancel(); voice.Stop(); return; }
    running = false;
    GFX_DEBUG_LOG("[SERVER STOP] sessions=" << sessions.size() << " lobbies=" << lobbies.size());
    ErrorCode ignored;
    acceptor.close(ignored);
    gameplaySocket.close(ignored);
    tickTimer.cancel();
    auto active = sessions;
    for(auto& entry : active) entry.second->Close("server stopping");
    //Let host-departure delete requests reach voice_server before closing the
    //manager socket. A crash still uses the separate ten-second recovery rule.
    voiceShutdown.expires_after(std::chrono::milliseconds(250));
    voiceShutdown.async_wait([this](ErrorCode error){ if(!error) voice.Stop(); });
}

unsigned short GameServer::GetPort() const{ return acceptor.local_endpoint().port(); }

void GameServer::DisableVoice(std::uint64_t lobbyId, const std::string& reason){
    auto room = lobbies.find(lobbyId);
    if(room == lobbies.end()) return;
    GFX_DEBUG_LOG("[VOICE DISABLED] lobby=" << lobbyId << " reason=" << reason);
    room->second.voiceState = "unavailable";
    room->second.voiceReason = reason;
    room->second.voiceGroup = room->second.voiceAll = 0;
    room->second.voiceTeams.clear();
    for(const auto& member : room->second.members){
        auto found = sessions.find(member.playerId);
        if(found != sessions.end()){
            found->second->voiceSession = 0;
            found->second->voiceBound = false;
        }
    }
    BroadcastLobby(room->second);
}

void GameServer::CreateVoiceGroup(std::uint64_t lobbyId){
    auto room = lobbies.find(lobbyId);
    if(room == lobbies.end()) return;
    if(!voice.Ready() || !voiceSynchronized) return; //The match never waits for voice.
    if(voiceAllocations.size() + pendingVoiceGroups >= 5){
        DisableVoice(lobbyId, "This GFX server already has five voice groups; gameplay is available");
        return;
    }
    //Reserve before the request: several hosts can create lobbies in the same tick.
    pendingVoiceGroups++;
    room->second.voiceState = "creating";
    room->second.voiceReason = "Creating voice channels";
    const std::string instance = voice.Instance();
    voice.Request("create_group", {{"type", "game"}, {"teams", room->second.teams},
        {"name", "GFX lobby " + std::to_string(lobbyId)}}, [this, lobbyId, instance](bool ok, const Protocol::Json& data){
        pendingVoiceGroups--;
        if(!ok){ DisableVoice(lobbyId, "Could not create voice channels; gameplay continues"); return; }
        const auto group = data.at("group_id").get<std::uint32_t>();
        GFX_DEBUG_LOG("[VOICE GROUP] lobby=" << lobbyId << " group=" << group << " created");
        voiceAllocations.insert(group);
        auto found = lobbies.find(lobbyId);
        if(found == lobbies.end()){ DeleteVoiceGroup(group); return; }
        Lobby& lobby = found->second;
        lobby.voiceGroup = group;
        lobby.voiceAll = data.at("all_channel_id").get<std::uint32_t>();
        lobby.voiceTeams = data.at("team_channel_ids").get<std::vector<std::uint32_t>>();
        lobby.voiceInstance = instance;
        lobby.voiceState = "ready";
        lobby.voiceReason.clear();
        if(lobby.playing) VoiceCommand(lobbyId, "set_phase", {{"phase", "playing"}});
        BroadcastLobby(lobby);
    });
}

void GameServer::DeleteVoiceGroup(std::uint32_t group){
    if(!voice.Ready()) return; //Recovery reconciliation deletes abandoned groups.
    voice.Request("delete_group", {{"group_id", group}}, [this, group](bool ok, const Protocol::Json& error){
        if(ok || error.value("code", "") == "group_missing") voiceAllocations.erase(group);
    });
}

void GameServer::VoiceCommand(std::uint64_t lobbyId, const std::string& operation, Protocol::Json fields){
    auto room = lobbies.find(lobbyId);
    if(room == lobbies.end() || !room->second.voiceGroup || !voice.Ready()) return;
    const auto group = room->second.voiceGroup;
    fields["group_id"] = group;
    voice.Request(operation, std::move(fields), [this, lobbyId, group](bool ok, const Protocol::Json& error){
        auto found = lobbies.find(lobbyId);
        if(!ok && error.value("code", "") == "group_missing"
            && found != lobbies.end() && found->second.voiceGroup == group)
            DisableVoice(lobbyId, "Voice channels no longer exist; gameplay continues");
    });
}

void GameServer::VoiceLost(bool permanent, const std::string& reason){
    GFX_DEBUG_LOG("[VOICE MANAGER] permanent=" << permanent << " reason=" << reason);
    voiceSynchronized = false;
    std::vector<std::uint64_t> affected;
    for(auto& entry : lobbies) if(entry.second.voiceGroup){
        entry.second.voiceState = "recovering";
        entry.second.voiceReason = "Recovering the voice connection";
        affected.push_back(entry.first);
    }
    if(permanent){
        voiceAllocations.clear();
        for(auto id : affected) DisableVoice(id, reason + "; gameplay continues");
    }
    else for(auto id : affected){ auto room = lobbies.find(id); if(room != lobbies.end()) BroadcastLobby(room->second); }
    //During a brief management outage, existing clients keep their voice sockets.
    //Their transmit/mute settings still work; no replacement groups are created.
}

void GameServer::ReconcileVoice(std::uint32_t after, std::vector<Protocol::Json> groups){
    voice.Request("list_groups", {{"after_id", after}}, [this, groups = std::move(groups)](bool ok, const Protocol::Json& data) mutable{
        if(!ok) return;
        for(const auto& group : data.at("groups"))
            if(group.at("owner").value("manager_id", 0u) == voice.Owner()) groups.push_back(group);
        if(!data.at("next_after_id").is_null()){
            ReconcileVoice(data.at("next_after_id").get<std::uint32_t>(), std::move(groups));
            return;
        }
        voiceAllocations.clear();
        for(const auto& group : groups) voiceAllocations.insert(group.at("group_id").get<std::uint32_t>());
        std::vector<std::uint64_t> missing;
        for(auto& entry : lobbies){
            Lobby& lobby = entry.second;
            if(!lobby.voiceGroup) continue;
            if(!voiceAllocations.count(lobby.voiceGroup)){ missing.push_back(entry.first); continue; }
            lobby.voiceState = "ready";
            lobby.voiceReason.clear();
        }
        for(auto id : missing) DisableVoice(id, "Voice channels expired or were removed; gameplay continues");
        //A host or guest may have left while management was disconnected.
        for(const auto& group : groups){
            const auto groupId = group.at("group_id").get<std::uint32_t>();
            auto room = std::find_if(lobbies.begin(), lobbies.end(), [groupId](const auto& entry){ return entry.second.voiceGroup == groupId; });
            if(room == lobbies.end()){ DeleteVoiceGroup(groupId); continue; }
            const auto lobbyId = room->first;
            VoiceCommand(lobbyId, "set_phase", {{"phase", room->second.playing ? "playing" : "waiting"}});
            voice.Request("group_details", {{"group_id", groupId}}, [this, lobbyId, groupId](bool ok, const Protocol::Json& details){
                if(!ok) return;
                auto found = lobbies.find(lobbyId);
                if(found == lobbies.end() || found->second.voiceGroup != groupId){ DeleteVoiceGroup(groupId); return; }
                for(const auto& participant : details.at("participants")){
                    const auto voiceId = participant.at("session_id").get<std::uint32_t>();
                    auto member = std::find_if(found->second.members.begin(), found->second.members.end(), [&](const LobbyMember& item){
                        auto player = sessions.find(item.playerId);
                        return item.connected && player != sessions.end() && player->second->lobbyId == lobbyId
                            && player->second->voiceSession == voiceId;
                    });
                    if(member == found->second.members.end())
                        VoiceCommand(lobbyId, "remove_player", {{"session_id", voiceId}});
                    else VoiceCommand(lobbyId, "set_team", {{"session_id", voiceId}, {"team", member->team + 1}});
                }
            });
        }
        voiceSynchronized = true;
        std::cout << "Voice manager connected to " << voice.Settings().host << ':' << voice.Settings().port << std::endl;
        std::vector<std::uint64_t> retained;
        for(const auto& entry : lobbies) if(entry.second.voiceGroup) retained.push_back(entry.first);
        for(auto id : retained){ auto room = lobbies.find(id); if(room != lobbies.end()) BroadcastLobby(room->second); }
    });
}

bool GameServer::HandleVoice(std::uint64_t id, const Protocol::Json& message){
    const std::string type = message.at("type").get<std::string>();
    if(type != "voice_bind" && type != "voice_transmit") return false;
    auto player = sessions.at(id);
    auto room = lobbies.find(player->lobbyId);
    auto reject = [&](const std::string& reason){
        GFX_DEBUG_LOG("[VOICE REJECT] player=" << id << " command=" << type << " reason=" << reason);
        auto reply = Protocol::Message("voice_error"); reply["message"] = reason; player->Send(reply);
    };
    if(room == lobbies.end() || !room->second.voiceGroup || !voiceSynchronized || !voice.Ready()){
        reject("Voice is unavailable or reconnecting"); return true;
    }
    Lobby& lobby = room->second;
    auto member = std::find_if(lobby.members.begin(), lobby.members.end(), [id](const LobbyMember& item){ return item.playerId == id && item.connected; });
    if(member == lobby.members.end()){ reject("You are not an active member of this lobby"); return true; }
    if(!message.contains("instance") || message["instance"] != lobby.voiceInstance
        || !message.contains("group") || message["group"] != lobby.voiceGroup){ reject("Stale voice connection"); return true; }
    if(type == "voice_transmit"){
        if(!player->voiceBound){ reject("Voice has not connected yet"); return true; }
        if(!message.contains("channel") || !message["channel"].is_string()){ reject("Choose All or Team"); return true; }
        const std::string channel = message.value("channel", "");
        if(channel != "all" && channel != "team"){ reject("Choose All or Team"); return true; }
        if(channel == "team" && !lobby.playing){ reject("Team voice is disabled until the match starts"); return true; }
        auto reply = Protocol::Message("voice_transmit");
        reply["group"] = lobby.voiceGroup;
        reply["instance"] = lobby.voiceInstance;
        reply["session"] = player->voiceSession;
        reply["channel"] = channel == "all" ? lobby.voiceAll : lobby.voiceTeams.at(member->team);
        player->Send(reply);
        return true;
    }
    if(!message.contains("session") || !message["session"].is_number_unsigned()
        || message["session"] == 0 || message["session"].get<std::uint64_t>() > UINT32_MAX){ reject("Invalid voice session"); return true; }
    const auto voiceId = message["session"].get<std::uint32_t>();
    if(player->voiceBound && player->voiceSession == voiceId) return true;
    if(player->voiceSession && player->voiceSession != voiceId)
        VoiceCommand(lobby.id, "remove_player", {{"session_id", player->voiceSession}});
    player->voiceSession = voiceId;
    GFX_DEBUG_LOG("[VOICE BIND] player=" << id << " lobby=" << lobby.id << " voice_session=" << voiceId << " team=" << member->team + 1);
    player->voiceBound = false;
    const auto lobbyId = lobby.id;
    voice.Request("bind_player", {{"group_id", lobby.voiceGroup}, {"session_id", voiceId},
        {"member_id", std::to_string(id)}, {"team", member->team + 1}}, [this, id, lobbyId, voiceId](bool ok, const Protocol::Json& data){
        auto found = sessions.find(id);
        if(found == sessions.end() || found->second->lobbyId != lobbyId || found->second->voiceSession != voiceId) return;
        found->second->voiceBound = ok;
        GFX_DEBUG_LOG("[VOICE BIND RESULT] player=" << id << " voice_session=" << voiceId << " ok=" << ok
            << " error=" << (ok ? "none" : data.value("code", "failed")));
        if(ok){
            auto reply = Protocol::Message("voice_bound");
            reply.update({{"session", voiceId}, {"instance", voice.Instance()}, {"group", data.at("group_id")}});
            found->second->Send(reply);
            auto room = lobbies.find(lobbyId);
            if(room != lobbies.end()) for(const auto& member : room->second.members)
                if(member.playerId == id) VoiceCommand(lobbyId, "set_team", {{"session_id", voiceId}, {"team", member.team + 1}});
        }
        if(!ok){
            auto reply = Protocol::Message("voice_error"); reply["message"] = "Voice connection: " + data.value("code", "failed");
            found->second->Send(reply);
        }
    });
    return true;
}

//Lobby commands stay on TCP: they are small, reliable decisions rather than movement.
bool GameServer::HandleLobby(std::uint64_t id, const Protocol::Json& message){
    auto session = sessions.at(id);
    std::string type = message["type"].get<std::string>();
    if(type != "create_lobby" && type != "join_lobby" && type != "choose_team"
        && type != "set_ready" && type != "choose_shape"
        && type != "list_lobbies" && type != "join_public"
        && type != "start_match" && type != "leave_lobby" && type != "return_lobby" && type != "rejoin_match") return false;
    auto reject = [&](const std::string& reason){
        GFX_DEBUG_LOG("[LOBBY REJECT] player=" << id << " lobby=" << session->lobbyId << " command=" << type << " reason=" << reason);
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
        GFX_DEBUG_LOG("[LOBBY CREATED] lobby=" << inserted.first->first << " host=" << id
            << " teams=" << inserted.first->second.teams << " minutes=" << inserted.first->second.minutes
            << " public=" << inserted.first->second.isPublic);
        BroadcastLobby(inserted.first->second);
        CreateVoiceGroup(inserted.first->first);
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
        for(auto& member : lobby.members) member.ready = false;
        reservations.erase(session->rejoinToken);
        session->lobbyId = lobby.id;
        GFX_DEBUG_LOG("[LOBBY JOIN] lobby=" << lobby.id << " player=" << id << " team=" << team + 1 << " members=" << lobby.members.size());
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
        GFX_DEBUG_LOG("[RETURN TO LOBBY] lobby=" << lobby.id << " match=" << lobby.match);
        VoiceCommand(lobby.id, "set_phase", {{"phase", "waiting"}});
        for(auto& member : lobby.members) member.ready = false;
        lobby.members.erase(std::remove_if(lobby.members.begin(), lobby.members.end(),
            [](const LobbyMember& member){ return !member.connected; }), lobby.members.end());
        BroadcastLobby(lobby);
        return true;
    }
    if(lobby.playing){ reject("Lobby choices are locked during the match"); return true; }
    if(type == "set_ready"){
        if(!message.contains("ready") || !message["ready"].is_boolean()){
            reject("Ready must be true or false"); return true;
        }
        for(auto& member : lobby.members) if(member.playerId == id) member.ready = message["ready"].get<bool>();
    }
    else if(type == "choose_shape"){
        if(lobby.hostId != id){ reject("Only the host can choose team shapes"); return true; }
        if(!message.contains("team") || !message["team"].is_number_integer() || message["team"] < 0 || message["team"] >= lobby.teams
            || !message.contains("shape") || !message["shape"].is_number_integer() || message["shape"] < 0 || message["shape"] > 3){
            reject("Choose triangle, square, circle or hexagon for a team"); return true;
        }
        int team = message["team"].get<int>(), shape = message["shape"].get<int>();
        //Swap the old shape into its previous slot, so teams stay visually distinct.
        auto previous = std::find(lobby.shapes.begin(), lobby.shapes.end(), shape);
        std::iter_swap(lobby.shapes.begin() + team, previous);
        for(auto& member : lobby.members) member.ready = false;
    }
    else if(type == "choose_team"){
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
        if(member->team != team){
            member->team = team;
            for(auto& player : lobby.members) player.ready = false;
            if(session->voiceBound) VoiceCommand(lobby.id, "set_team", {{"session_id", session->voiceSession}, {"team", team + 1}});
        }
    }
    else if(type == "start_match"){
        if(lobby.hostId != id){ reject("Only the host can start the match"); return true; }
        int hostTeam = lobby.members.front().team;
        bool opponent = std::any_of(lobby.members.begin(), lobby.members.end(),
            [hostTeam](const LobbyMember& player){ return player.team != hostTeam; });
        if(!opponent){ reject("At least one player must be on an opposing team"); return true; }
        for(const auto& member : lobby.members){
            if(!member.ready){ reject("Every player must be ready before the host starts"); return true; }
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
        GFX_DEBUG_LOG("[MATCH START] lobby=" << lobby.id << " match=" << lobby.match << " players=" << lobby.members.size()
            << " teams=" << lobby.teams << " duration_seconds=" << lobby.minutes * 60);
        for(const auto& member : lobby.members){
            auto other = sessions.at(member.playerId);
            other->inputCommands = other->snapshotAttempts = other->snapshotErrors = 0;
            other->nextGameplayLog = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            GFX_DEBUG_LOG("[MATCH PLAYER] player=" << member.playerId << " team=" << member.team + 1
                << " udp=" << other->gameplayEndpoint << " last_udp_ms=" << AgeMilliseconds(other->lastGameplayPacket));
        }
        VoiceCommand(lobby.id, "set_phase", {{"phase", "playing"}});
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
    message["shapes"] = lobby.shapes;
    message["minutes"] = lobby.minutes;
    message["playing"] = lobby.playing;
    message["match"] = lobby.match;
    message["voice"] = {{"state", lobby.voiceState}, {"reason", lobby.voiceReason},
        {"group", lobby.voiceGroup}, {"all", lobby.voiceAll}, {"teams", lobby.voiceTeams},
        {"instance", lobby.voiceInstance}, {"managerReady", voiceSynchronized},
        {"host", voice.Settings().publicHost}, {"port", voice.Settings().port}};
    message["members"] = Protocol::Json::array();
    for(const auto& member : lobby.members)
        message["members"].push_back({{"playerId", member.playerId}, {"name", member.name}, {"team", member.team}, {"connected", member.connected}, {"ready", member.ready}});
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
    GFX_DEBUG_LOG("[LOBBY LEAVE] lobby=" << lobbyId << " player=" << id << " host=" << (room->second.hostId == id)
        << " playing=" << room->second.playing << " session_closed=" << found->second->closed);
    if(room->second.hostId == id){
        GFX_DEBUG_LOG("[LOBBY CLOSED] lobby=" << lobbyId << " host=" << id << " remaining_members=" << room->second.members.size() - 1);
        if(room->second.voiceGroup) DeleteVoiceGroup(room->second.voiceGroup);
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
        if(found->second->voiceSession)
            VoiceCommand(lobby.id, "remove_player", {{"session_id", found->second->voiceSession}});
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
            for(auto& member : members) member.ready = false;
            members.erase(std::remove_if(members.begin(), members.end(),
                [id](const LobbyMember& player){ return player.playerId == id; }), members.end());
        }
        BroadcastLobby(lobby);
    }
    found->second->voiceSession = 0;
    found->second->voiceBound = false;
    ExpireReservations();
}

bool GameServer::RejoinMatch(std::uint64_t id, const std::string& token){
    auto session = sessions.at(id);
    auto deny = [&](const std::string& reason){
        GFX_DEBUG_LOG("[REJOIN REJECT] player=" << id << " reason=" << reason);
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
    session->inputCommands = session->snapshotAttempts = session->snapshotErrors = 0;
    session->nextGameplayLog = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    GFX_DEBUG_LOG("[REJOIN] player=" << id << " lobby=" << lobby.id << " team=" << member->team + 1 << " match=" << lobby.match);
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

bool GameServer::SendDatagram(const std::vector<std::uint8_t>& bytes, const udp::endpoint& endpoint){
    //A nonblocking send either reaches the OS queue immediately or is dropped.
    //UDP snapshots are replaceable, so there is no growing send queue.
    ErrorCode error;
    gameplaySocket.send_to(boost::asio::buffer(bytes), endpoint, 0, error);
    if(error && std::chrono::steady_clock::now() >= nextUdpSendError){
        nextUdpSendError = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        GFX_DEBUG_LOG("[UDP SEND ERROR] destination=" << endpoint << " bytes=" << bytes.size() << " reason=" << error.message());
    }
    return !error;
}

void GameServer::ReceiveGameplay(){
    gameplaySocket.async_receive_from(boost::asio::buffer(datagram), sender, [this](ErrorCode error, std::size_t size){
        if(!running) return;
        if(!error) HandleGameplay(size);
        else if(error != boost::asio::error::operation_aborted && std::chrono::steady_clock::now() >= nextUdpWarning){
            nextUdpWarning = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            GFX_DEBUG_LOG("[UDP RECEIVE ERROR] reason=" << error.message());
        }
        ReceiveGameplay();
    });
}

void GameServer::HandleGameplay(std::size_t size){
    GameplayPackets::Packet packet;
    auto now = std::chrono::steady_clock::now();
    if(!GameplayPackets::Decode(datagram.data(), size, packet)){
        if(now >= nextUdpWarning){
            nextUdpWarning = now + std::chrono::seconds(2);
            GFX_DEBUG_LOG("[UDP REJECT] sender=" << sender << " reason=invalid packet bytes=" << size);
        }
        return;
    }
    auto found = sessions.find(packet.identity.id);
    if(found == sessions.end()) return;
    auto session = found->second;
    if(!session->welcomed || session->closed || packet.identity.token != session->sessionToken) return;
    auto reject = [&](const char* reason){
        if(now < session->nextUdpWarning) return;
        session->nextUdpWarning = now + std::chrono::seconds(2);
        GFX_DEBUG_LOG("[UDP REJECT] player=" << session->id << " sender=" << sender << " expected=" << session->gameplayEndpoint
            << " reason=" << reason);
    };
    if(now - session->udpRateWindow >= std::chrono::seconds(1)){
        session->udpRateWindow = now;
        session->udpCount = 0;
    }
    if(++session->udpCount > 80){ reject("packet rate limit"); return; }
    if(packet.type == GameplayPackets::Type::Register){
        ErrorCode error;
        auto tcpPeer = session->socket.remote_endpoint(error);
        if(error || sender.address() != tcpPeer.address()){ reject("registration address differs from TCP address"); return; }
        if(session->udpReady && sender != session->gameplayEndpoint){ reject("registered UDP endpoint changed"); return; }
        GFX_DEBUG_LOG("[UDP REGISTERED] player=" << session->id << " endpoint=" << sender << " first=" << !session->udpReady);
        session->gameplayEndpoint = sender;
        session->udpReady = true;
        session->lastGameplayPacket = now;
        session->udpPackets++;
        SendDatagram(GameplayPackets::Registration(GameplayPackets::Type::Registered, packet.identity), sender);
        return;
    }
    if(packet.type != GameplayPackets::Type::Input) return;
    if(!session->udpReady || sender != session->gameplayEndpoint){ reject("input from an unregistered UDP endpoint"); return; }
    auto room = lobbies.find(session->lobbyId);
    if(room == lobbies.end() || !room->second.playing || room->second.finished || packet.match != room->second.match){
        reject("input for an inactive or different match"); return;
    }
    session->lastGameplayPacket = now;
    session->udpPackets++;
    for(const auto& command : packet.commands){
        if(!GameplayPackets::Newer(command.sequence, session->lastReceived)) continue;
        if(session->inputs.size() >= 8){ reject("input queue full"); break; }
        if(session->inputCommands == 0)
            GFX_DEBUG_LOG("[FIRST INPUT] player=" << session->id << " match=" << packet.match << " sequence=" << command.sequence);
        session->inputCommands++;
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
    for(const auto& session : disconnected) session->Close("gameplay input timeout (5s)");
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
        const auto bytes = GameplayPackets::State({session->id, session->sessionToken}, snapshot);
        const bool sent = SendDatagram(bytes, session->gameplayEndpoint);
        session->snapshotAttempts++;
        if(!sent) session->snapshotErrors++;
        if(session->snapshotAttempts == 1)
            GFX_DEBUG_LOG("[FIRST SNAPSHOT] player=" << session->id << " match=" << lobby.match << " destination=" << session->gameplayEndpoint
                << " bytes=" << bytes.size() << " queued_to_os=" << sent);
        if(std::chrono::steady_clock::now() >= session->nextGameplayLog){
            session->nextGameplayLog = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            GFX_DEBUG_LOG("[GAMEPLAY] player=" << session->id << " match=" << lobby.match << " inputs=" << session->inputCommands
                << " snapshots_attempted=" << session->snapshotAttempts << " snapshot_errors=" << session->snapshotErrors
                << " last_udp_ms=" << AgeMilliseconds(session->lastGameplayPacket) << " last_input_ms=" << AgeMilliseconds(session->inputTime));
        }
    }
}

void GameServer::SendResults(const Lobby& lobby){
    GFX_DEBUG_LOG("[MATCH FINISHED] lobby=" << lobby.id << " match=" << lobby.match << " winner=" << lobby.game->GetWinningTeam());
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
