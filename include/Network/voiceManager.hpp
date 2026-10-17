#pragma once

#include<boost/asio.hpp>
#include<deque>
#include<functional>
#include<map>
#include<memory>
#include"Network/protocol.hpp"

struct VoiceServerSettings{
    bool enabled = true;
    std::string host = "127.0.0.1";
    //Empty means clients use the same hostname they used for gfx_server.
    std::string publicHost;
    unsigned short port = 8080;
};

//Only control JSON travels here. The separate voice application owns audio/UDP.
class VoiceManager{
    public:
        using Json = Protocol::Json;
        using Reply = std::function<void(bool, const Json&)>;
        std::function<void(bool)> onReady;
        std::function<void(bool, const std::string&)> onLost;
        std::function<void(const Json&)> onEvent;

        VoiceManager(boost::asio::io_context& context, VoiceServerSettings settings);
        ~VoiceManager();
        void Start();
        void Stop();
        void Request(const std::string& operation, Json fields = Json::object(), Reply reply = {});
        bool Ready() const{ return ready; }
        const std::string& Instance() const{ return instance; }
        std::uint32_t Owner() const{ return managerId; }
        const VoiceServerSettings& Settings() const{ return settings; }

    private:
        using Clock = std::chrono::steady_clock;
        struct Connection;
        struct Pending{ Reply reply; Clock::time_point deadline; };
        boost::asio::io_context& context;
        VoiceServerSettings settings;
        boost::asio::ip::tcp::resolver resolver;
        boost::asio::steady_timer timer;
        std::shared_ptr<Connection> connection;
        std::map<std::string, Pending> pending;
        std::uint64_t nextRequest = 0;
        std::uint32_t managerId = 0;
        std::string instance;
        bool running = false, ready = false;
        Clock::time_point nextConnect{}, lastRead{}, recoveryEnd{};
        void Connect();
        void Read(const std::shared_ptr<Connection>& current);
        void Receive(const Json& message);
        void Send(const Json& message);
        void Write(const std::shared_ptr<Connection>& current);
        void Lost(const std::string& reason, bool permanent = false);
        void Schedule();
};
