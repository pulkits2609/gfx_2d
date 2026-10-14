#pragma once

#include<boost/asio.hpp>
#include<deque>
#include"Network/protocol.hpp"
#include"Network/lobby.hpp"

//Poll runs ready network callbacks without waiting, so drawing never blocks.
class GameClient{
    public:
        enum class State{ Offline, Connecting, Connected, Failed };

    private:
        boost::asio::io_context context;
        boost::asio::ip::tcp::resolver resolver{context};
        boost::asio::ip::tcp::socket socket{context};
        boost::asio::steady_timer deadline{context};
        boost::asio::steady_timer heartbeat{context};
        Protocol::Header header{};
        std::string body;
        std::deque<std::vector<std::uint8_t>> outgoing;
        State state = State::Offline;
        bool started = false;
        bool receivedPlayers = false;
        bool receivedPong = false;
        std::uint64_t playerId = 0;
        std::uint32_t requestId = 0;
        std::string name;
        std::string sessionToken;
        std::string status = "LOCAL PRACTICE";
        Protocol::Json peers = Protocol::Json::array();
        Protocol::Json lobby;
        std::string notice;

        void SetDeadline(int seconds);
        void ReadHeader();
        void ReadBody();
        void HandleMessage();
        void Send(const Protocol::Json& message);
        void WriteNext();
        void StartHeartbeat();
        void Fail(const std::string& reason);

    public:
        GameClient() = default;
        ~GameClient();
        GameClient(const GameClient&) = delete;
        GameClient& operator=(const GameClient&) = delete;

        void Connect(const std::string& host, unsigned short port, const std::string& displayName);
        void Poll();
        void Disconnect();
        void SendPing();
        void CreateLobby(int teams, int minutes);
        void JoinLobby(const std::string& code);
        void ChooseTeam(int team);
        void StartMatch();
        void LeaveLobby();
        const Protocol::Json& GetLobby() const{ return lobby; }
        const std::string& GetNotice() const{ return notice; }
        State GetState() const{ return state; }
        const std::string& GetStatus() const{ return status; }
        std::uint64_t GetPlayerId() const{ return playerId; }
        std::size_t GetPeerCount() const{ return peers.size(); }
        bool ProbeComplete() const{ return receivedPlayers && receivedPong; }
};
