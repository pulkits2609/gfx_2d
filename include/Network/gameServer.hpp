#pragma once

#include<boost/asio.hpp>
#include<memory>
#include<unordered_map>
#include"Network/protocol.hpp"

class ControlSession;

class GameServer{
    friend class ControlSession;
    private:
        boost::asio::io_context& context;
        boost::asio::ip::tcp::acceptor acceptor;
        std::unordered_map<std::uint64_t, std::shared_ptr<ControlSession>> sessions;
        std::uint64_t nextPlayerId = 1;
        bool running = false;

        void AcceptConnection();

    public:
        GameServer(boost::asio::io_context& io, const std::string& address, unsigned short port);
        ~GameServer();
        void Start();
        void Stop();
        unsigned short GetPort() const;
};
