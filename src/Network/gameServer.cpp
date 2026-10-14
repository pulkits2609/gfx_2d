#include"Network/gameServer.hpp"
#include<chrono>
#include<fstream>
#include<iomanip>
#include<iostream>
#include<sstream>
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
    private:
        GameServer& server;
        tcp::socket socket;
        boost::asio::steady_timer deadline;
        Protocol::Header header{};
        std::string body;
        std::vector<std::uint8_t> reply;
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
            if(closed) return;
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
            });
        }

        void Send(const Protocol::Json& message, bool closeAfter = false){
            try{ reply = Protocol::Encode(message); }
            catch(const std::exception&){ Close(); return; }
            SetDeadline(5);
            auto self = shared_from_this();
            boost::asio::async_write(socket, boost::asio::buffer(reply), [self, closeAfter](ErrorCode error, std::size_t){
                if(error || closeAfter){ self->Close(); return; }
                //only one reply and one read are in flight, so there is no growing output queue
                self->ReadHeader();
            });
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
            server(owner), socket(std::move(connection)), deadline(socket.get_executor()), id(playerId){}

        void Start(){ ReadHeader(); }

        void Close(){
            if(closed) return;
            closed = true;
            ErrorCode ignored;
            deadline.cancel();
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
