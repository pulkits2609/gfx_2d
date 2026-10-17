#pragma once

#include<boost/asio.hpp>
#include<memory>
#include<unordered_map>
#include"Network/protocol.hpp"
#include"Network/lobby.hpp"
#include"Network/gameplayPackets.hpp"
#include"Network/voiceManager.hpp"
#include<set>

struct LobbyMember{
    std::uint64_t playerId = 0;
    std::string name;
    int team = 0;
    bool connected = true;
    bool ready = false;
};

struct RejoinReservation{
    std::uint64_t lobbyId = 0;
    std::uint64_t playerId = 0;
    std::chrono::steady_clock::time_point leftAt;
};

struct Lobby{
    std::uint64_t id = 0;
    bool isPublic = false;
    std::string code; //only private lobbies have a join code
    std::uint64_t hostId = 0;
    int teams = 2;
    std::array<int, 4> shapes{0, 1, 2, 3};
    int minutes = LobbyRules::DefaultMinutes;
    bool playing = false;
    bool finished = false;
    std::uint32_t match = 0;
    std::uint32_t tick = 0;
    double snapshotTime = 0;
    std::unique_ptr<Game> game;
    std::vector<LobbyMember> members;
    std::uint32_t voiceGroup = 0, voiceAll = 0;
    std::vector<std::uint32_t> voiceTeams;
    std::string voiceInstance;
    std::string voiceState = "unavailable";
    std::string voiceReason = "Voice server is unavailable";
};

class ControlSession;

class GameServer{
    friend class ControlSession;
    private:
        boost::asio::io_context& context;
        boost::asio::ip::tcp::acceptor acceptor;
        boost::asio::ip::udp::socket gameplaySocket;
        boost::asio::ip::udp::endpoint sender;
        std::array<std::uint8_t, GameplayPackets::MaximumSize + 1> datagram{};
        boost::asio::steady_timer tickTimer;
        std::chrono::steady_clock::time_point nextTick;
        std::unordered_map<std::uint64_t, std::shared_ptr<ControlSession>> sessions;
        std::unordered_map<std::uint64_t, Lobby> lobbies;
        std::unordered_map<std::string, RejoinReservation> reservations;
        std::uint64_t nextPlayerId = 1, nextLobbyId = 1;
        std::uint32_t nextMatch = 1;
        bool running = false;
        VoiceManager voice;
        boost::asio::steady_timer voiceShutdown;
        bool voiceSynchronized = false;
        unsigned pendingVoiceGroups = 0;
        std::set<std::uint32_t> voiceAllocations;

        void CreateVoiceGroup(std::uint64_t lobbyId);
        void DisableVoice(std::uint64_t lobbyId, const std::string& reason);
        void VoiceLost(bool permanent, const std::string& reason);
        void ReconcileVoice(std::uint32_t after = 0, std::vector<Protocol::Json> groups = {});
        void DeleteVoiceGroup(std::uint32_t group);
        void VoiceCommand(std::uint64_t lobbyId, const std::string& operation, Protocol::Json fields);
        bool HandleVoice(std::uint64_t id, const Protocol::Json& message);

        void AcceptConnection();
        bool HandleLobby(std::uint64_t id, const Protocol::Json& message);
        void LeaveLobby(std::uint64_t id);
        void BroadcastLobby(const Lobby& lobby);
        void ReceiveGameplay();
        void HandleGameplay(std::size_t size);
        void ScheduleTick();
        void Tick();
        void SendSnapshots(Lobby& lobby);
        void SendResults(const Lobby& lobby);
        bool RejoinMatch(std::uint64_t id, const std::string& token);
        void ExpireReservations();
        void SendDatagram(const std::vector<std::uint8_t>& bytes, const boost::asio::ip::udp::endpoint& endpoint);

    public:
        GameServer(boost::asio::io_context& io, const std::string& address, unsigned short port,
            unsigned short gameplayPort = GameplayPackets::Port, VoiceServerSettings voiceSettings = {});
        ~GameServer();
        void Start();
        void Stop();
        unsigned short GetPort() const;
};
