#pragma once

#include"Network/voiceProcess.hpp"

class GameClient;

//This object belongs to Application, so replacing a gameplay connection does
//not forget microphone/listening choices during the same application run.
class VoiceChat{
    public:
        explicit VoiceChat(std::string executable = VoiceProcess::DefaultExecutable(), bool audio = true);
        void Update(GameClient* game);
        void Retry();
        void SetMicrophone(bool muted);
        void SetIncomingAll(bool muted);
        void SetIncomingTeam(bool muted);
        void SelectTransmit(GameClient& game, bool team);
        bool Connected() const{ return connected && gameApproved; }
        bool Available() const{ return !configuration.is_null(); }
        bool TeamEnabled() const{ return playing; }
        bool TransmittingTeam() const{ return teamTransmit; }
        bool MicrophoneMuted() const{ return microphone; }
        bool AllMuted() const{ return incomingAll; }
        bool TeamMuted() const{ return incomingTeam; }
        const std::string& Status() const{ return status; }
        const Protocol::Json& State() const{ return state; }

    private:
        using Json = Protocol::Json;
        VoiceProcess process;
        std::string executable;
        bool audio = true, started = false, processReady = false;
        bool connected = false, connecting = false, failed = false;
        bool gameApproved = false;
        bool microphone = true, incomingAll = true, incomingTeam = true;
        bool playing = false, teamTransmit = false, managerReady = false;
        std::uint64_t nextRequest = 0, minimumConnection = 1, connectionId = 0;
        std::string status = "Voice is unavailable", muteRequest, connectRequest;
        Json configuration, state;
        std::uint32_t session = 0;
        std::uint64_t reportedConnection = 0;
        std::string Send(const std::string& operation, Json fields = Json::object());
        void SendMutes();
        void Connect();
        void Receive(const Json& message, GameClient* game);
        void ReportSession(GameClient* game);
};
