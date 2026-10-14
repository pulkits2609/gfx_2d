#pragma once

#include<boost/asio.hpp>
#include<memory>
#include<unordered_map>
#include"Network/protocol.hpp"
#include"Network/lobby.hpp"

struct LobbyMember{
    std::uint64_t playerId = 0;
    std::string name;
    int team = 0;
};

struct Lobby{
    std::string code;
    std::uint64_t hostId = 0;
    int teams = 2;
    int minutes = LobbyRules::DefaultMinutes;
    bool playing = false;
    std::vector<LobbyMember> members;
};

class ControlSession;

class GameServer{
    friend class ControlSession;
    private:
        boost::asio::io_context& context;
        boost::asio::ip::tcp::acceptor acceptor;
        std::unordered_map<std::uint64_t, std::shared_ptr<ControlSession>> sessions;
        std::unordered_map<std::string, Lobby> lobbies;
        std::uint64_t nextPlayerId = 1;
        bool running = false;

        void AcceptConnection();
        bool HandleLobby(std::uint64_t id, const Protocol::Json& message);
        void LeaveLobby(std::uint64_t id);
        void BroadcastLobby(const Lobby& lobby);

    public:
        GameServer(boost::asio::io_context& io, const std::string& address, unsigned short port);
        ~GameServer();
        void Start();
        void Stop();
        unsigned short GetPort() const;
};
