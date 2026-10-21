#pragma once

#include<boost/asio.hpp>
#include<deque>
#include"Network/protocol.hpp"
#include"Network/lobby.hpp"
#include"Network/gameplayPackets.hpp"
#include"Network/shotPreview.hpp"
#include"Network/rejoinInfo.hpp"

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
        boost::asio::ip::udp::socket gameplaySocket{context};
        boost::asio::steady_timer registrationTimer{context};
        std::array<std::uint8_t, GameplayPackets::MaximumSize + 1> datagram{};
        bool udpReady = false;
        unsigned tickRate = MatchRules::DefaultTickRate;
        double tickStep = MatchRules::ServerStep;
        bool gameplayRefreshPending = false;
        unsigned registrationAttempts = 0;
        double nextGameplayRefresh = 0;
        double nextGameplayWarning = 0;
        bool hasSnapshot = false;
        GameplayPackets::Snapshot latest;
        std::deque<GameplayPackets::Snapshot> snapshots;
        std::deque<GameplayPackets::InputCommand> pendingInputs;
        std::uint32_t sequence = 0, reloadCounter = 0, lastSentShot = 0;
        bool reloadPending = false;
        double inputAccumulator = 0, predictionTime = 0, lastSnapshotAt = 0, waitingSince = 0;
        double lastAcknowledgementAt = 0;
        Player predicted;
        ShotPreview shotPreview;
        PlayerInput previewInput;
        glm::vec2 correction{0};
        Game renderedGame{2, false};
        std::size_t ownSlot = 0;
        Protocol::Header header{};
        std::string body;
        std::deque<std::vector<std::uint8_t>> outgoing;
        State state = State::Offline;
        bool started = false;
        bool receivedPlayers = false;
        bool receivedPong = false;
        std::uint64_t playerId = 0;
        std::uint32_t requestId = 0;
        std::uint32_t pendingPingRequest = 0;
        double pingSentAt = 0, lastPongAt = 0, pingMilliseconds = -1;
        std::string name;
        std::string sessionToken;
        std::string rejoinToken, serverHost;
        unsigned short serverPort = Protocol::ControlPort;
        RejoinInfo rejoin;
        std::string status = "LOCAL PRACTICE";
        Protocol::Json peers = Protocol::Json::array();
        Protocol::Json lobby;
        Protocol::Json publicLobbies = Protocol::Json::array();
        bool listingReceived = false;
        Protocol::Json results;
        std::string notice;
        std::string voiceNotice;
        Protocol::Json voiceTransmit;
        std::uint32_t voiceBoundSession = 0;

        void SetDeadline(int seconds);
        void ReadHeader();
        void ReadBody();
        void HandleMessage();
        void Send(const Protocol::Json& message);
        void WriteNext();
        void StartHeartbeat();
        void Fail(const std::string& reason);
        void StartGameplay(unsigned short port);
        void RegisterGameplay();
        void RefreshGameplay();
        void ReceiveGameplay();
        void AcceptSnapshot(GameplayPackets::Snapshot snapshot);
        void ResetGameplay();
        void SendDatagram(const std::vector<std::uint8_t>& bytes);
        void LogGameplayError(const char* operation, const boost::system::error_code& error);
        void RenderGameplay(double deltaTime, const PlayerInput& input);
        void RememberRejoin(const std::string& token, const std::string& code, double remaining);

    public:
        GameClient() = default;
        ~GameClient();
        GameClient(const GameClient&) = delete;
        GameClient& operator=(const GameClient&) = delete;

        void Connect(const std::string& host, unsigned short port, const std::string& displayName);
        void Poll();
        void Disconnect();
        void SendPing();
        void CreateLobby(int teams, int minutes, bool isPublic = false);
        void FetchLobbies();
        void JoinPublicLobby(std::uint64_t lobbyId);
        void JoinLobby(const std::string& code);
        void ChooseTeam(int team);
        void ChooseTeamShape(int team, int shape);
        void SetReady(bool ready);
        void StartMatch();
        void LeaveLobby();
        void ReturnToLobby();
        void RejoinMatch();
        void BindVoice(std::uint32_t session, const std::string& instance, std::uint32_t group);
        void SelectVoiceTransmit(bool team);
        Protocol::Json TakeVoiceTransmit(){ auto approval = std::move(voiceTransmit); voiceTransmit = nullptr; return approval; }
        const std::string& GetVoiceNotice() const{ return voiceNotice; }
        void ClearVoiceNotice(){ voiceNotice.clear(); }
        bool VoiceSessionReady(std::uint32_t session) const{ return session && voiceBoundSession == session; }
        const std::string& GetServerHost() const{ return serverHost; }
        const std::string& GetName() const{ return name; }
        double RejoinSecondsLeft() const;
        const RejoinInfo& GetRejoinInfo() const{ return rejoin; }
        void SetRejoinInfo(const RejoinInfo& info){ rejoin = info; }
        void UpdateGameplay(double deltaTime, const PlayerInput& input);
        bool HasSnapshot() const{ return hasSnapshot; }
        bool UDPReady() const{ return udpReady; }
        double GetPingMilliseconds() const;
        double GetSnapshotAgeMilliseconds() const;
        unsigned GetTickRate() const{ return tickRate; }
        bool MatchFinished() const{ return hasSnapshot && latest.finished; }
        float RemainingTime() const{ return hasSnapshot ? latest.remaining : 0; }
        const Game& GetGame() const{ return renderedGame; }
        std::size_t GetOwnSlot() const{ return ownSlot; }
        const Protocol::Json& GetPublicLobbies() const{ return publicLobbies; }
        bool HasLobbyList() const{ return listingReceived; }
        std::string GetServerAddress() const{ return serverHost + ":" + std::to_string(serverPort); }
        const Protocol::Json& GetLobby() const{ return lobby; }
        const Protocol::Json& GetResults() const{ return results; }
        const std::string& GetNotice() const{ return notice; }
        State GetState() const{ return state; }
        const std::string& GetStatus() const{ return status; }
        std::uint64_t GetPlayerId() const{ return playerId; }
        std::size_t GetPeerCount() const{ return peers.size(); }
        bool ProbeComplete() const{ return receivedPlayers && receivedPong; }
};
