#include"Network/gameClient.hpp"
#include<algorithm>
#include<chrono>
#include<stdexcept>

using boost::asio::ip::tcp;
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
}

void GameClient::Disconnect(){
    state = State::Offline;
    ErrorCode ignored;
    resolver.cancel();
    deadline.cancel();
    heartbeat.cancel();
    socket.shutdown(tcp::socket::shutdown_both, ignored);
    socket.close(ignored);
    sessionToken.clear();
}

void GameClient::Fail(const std::string& reason){
    if(state == State::Offline || state == State::Failed) return;
    Disconnect();
    state = State::Failed;
    status = "TCP OFFLINE: " + reason.substr(0, 80);
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
        state = State::Connected;
        status = "TCP #" + std::to_string(playerId) + " CONNECTED / LOCAL PRACTICE";
        SendPing();
        Send(Protocol::Message("players"));
        StartHeartbeat();
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
