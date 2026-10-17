#include"Network/voiceChat.hpp"
#include"Network/gameClient.hpp"
#include<algorithm>

VoiceChat::VoiceChat(std::string path, bool useAudio):executable(std::move(path)), audio(useAudio){}

std::string VoiceChat::Send(const std::string& operation, Json fields){
    const auto id = std::to_string(++nextRequest);
    fields.update({{"v", 2}, {"id", id}, {"op", operation}});
    if(!process.Send(fields)){
        failed = true; connected = connecting = false;
        status = "Voice process is unavailable. Use Retry voice.";
    }
    return id;
}

void VoiceChat::SendMutes(){
    if(processReady) muteRequest = Send("set_mutes", {{"microphone", microphone}, {"all", incomingAll}, {"team", incomingTeam}});
}

void VoiceChat::SetMicrophone(bool muted){ microphone = muted; SendMutes(); }
void VoiceChat::SetIncomingAll(bool muted){ incomingAll = muted; SendMutes(); }
void VoiceChat::SetIncomingTeam(bool muted){ incomingTeam = muted; SendMutes(); }

void VoiceChat::Retry(){
    if(!Available()) return;
    failed = false;
    connected = connecting = false;
    session = 0;
    reportedConnection = 0;
    minimumConnection = connectionId + 1;
    if(process.Running()) Send("disconnect");
    else{ started = processReady = false; connectionId = 0; minimumConnection = 1; }
}

void VoiceChat::Connect(){
    if(!processReady || configuration.is_null() || connecting || connected || failed) return;
    connecting = true;
    status = "Connecting voice...";
    connectRequest = Send("connect", configuration);
}

void VoiceChat::ReportSession(GameClient* game){
    if(!game || !session || !managerReady || connectionId == reportedConnection || configuration.is_null()) return;
    game->BindVoice(session, configuration.at("server_instance").get<std::string>(), configuration.at("group_id").get<std::uint32_t>());
    reportedConnection = connectionId;
}

void VoiceChat::Update(GameClient* game){
    Json wanted;
    bool nextManagerReady = false;
    playing = false;
    if(game && game->GetState() == GameClient::State::Connected && game->GetLobby().is_object()){
        const auto& lobby = game->GetLobby();
        playing = lobby.value("playing", false);
        try{
            const auto& voice = lobby.at("voice");
            const auto availability = voice.at("state").get<std::string>();
            if(availability == "ready" || availability == "recovering"){
                auto host = voice.at("host").get<std::string>();
                if(host.empty()) host = game->GetServerHost();
                wanted = {{"host", host}, {"port", voice.at("port")}, {"name", game->GetName()},
                    {"member_id", std::to_string(game->GetPlayerId())}, {"server_instance", voice.at("instance")},
                    {"group_id", voice.at("group")}, {"channel_id", voice.at("all")}, {"audio", audio}};
                if(!wanted["port"].is_number_integer() || wanted["port"] < 1 || wanted["port"] > 65535
                    || !wanted["group_id"].is_number_unsigned() || wanted["group_id"] == 0
                    || !wanted["channel_id"].is_number_unsigned() || wanted["channel_id"] == 0
                    || !wanted["server_instance"].is_string() || wanted["server_instance"].get_ref<const std::string&>().size() != 16)
                    throw std::runtime_error("Invalid voice configuration");
                nextManagerReady = voice.value("managerReady", false);
            }
            else status = voice.value("reason", "Voice is unavailable");
        }
        catch(const std::exception&){ wanted = nullptr; status = "Voice configuration is unavailable"; }
    }
    else status = "Voice joins automatically with an online lobby";

    if(wanted != configuration){
        if(started && processReady && process.Running()) Send("disconnect");
        configuration = std::move(wanted);
        connected = connecting = failed = false;
        session = 0; reportedConnection = 0;
        minimumConnection = connectionId + 1;
        state = nullptr;
    }
    if(nextManagerReady && !managerReady) reportedConnection = 0;
    managerReady = nextManagerReady;
    if(!configuration.is_null() && !started){
        started = true;
        process.Start(executable);
    }
    for(const auto& event : process.Poll()){
        try{ Receive(event, game); }
        catch(const std::exception&){ failed = true; connected = connecting = false; status = "Invalid voice response"; }
    }
    if(started && !process.Running() && !failed){
        failed = true; connected = connecting = processReady = false;
        status = "Voice client stopped. Use Retry voice.";
    }
    if(game){
        auto approval = game->TakeVoiceTransmit();
        if(approval.is_object() && connected && session == approval.value("session", 0u)
            && approval.value("instance", "") == configuration.value("server_instance", "")
            && approval.value("group", 0u) == configuration.value("group_id", 0u))
            Send("set_transmit", {{"channel_id", approval.at("channel")}});
        if(!game->GetVoiceNotice().empty()){
            status = game->GetVoiceNotice();
            game->ClearVoiceNotice();
            //A report sent during recovery may be rejected. Retry explicitly,
            //rather than flooding the gameplay control connection every frame.
        }
    }
    ReportSession(game);
    gameApproved = game && game->VoiceSessionReady(session);
    Connect();
}

void VoiceChat::Receive(const Json& message, GameClient* game){
    if(message.contains("event")){
        const auto event = message.at("event").get<std::string>();
        const auto& data = message.at("data");
        if(event == "ready"){
            processReady = true;
            SendMutes();
            return;
        }
        if(event == "process_error"){
            failed = true; connected = connecting = false; processReady = false;
            status = data.value("message", "Voice client stopped"); return;
        }
        if(configuration.is_null() || !data.contains("connection_id")) return;
        const auto incoming = data.at("connection_id").get<std::uint64_t>();
        if(incoming < minimumConnection || incoming < connectionId) return;
        if(data.contains("server_instance") && data["server_instance"] != configuration["server_instance"]) return;
        connectionId = incoming;
        if(event == "session_ready"){
            if(data.at("requested_group_id") != configuration.at("group_id")
                || data.at("member_id") != configuration.at("member_id")) return;
            session = data.at("session_id").get<std::uint32_t>();
            status = "Waiting for the game's voice approval...";
            ReportSession(game);
        }
        if(event == "connected" || event == "status" || event == "transmit_changed" || event == "mute_changed"){
            state = data;
            if(data.value("state", "") == "joined"){
                connected = true; connecting = false;
                teamTransmit = data.at("transmit_channel_id") != configuration.at("channel_id");
                status = teamTransmit ? "VOICE / TEAM" : "VOICE / ALL";
            }
        }
        if(event == "error" || event == "group_deleted" || event == "membership_removed"
            || event == "server_disconnected" || event == "disconnected"){
            connected = connecting = false;
            if(event == "disconnected" && failed) return;
            failed = true;
            status = event == "error" ? data.value("message", "Voice connection failed") : "Voice disconnected; gameplay continues";
        }
        return;
    }
    const auto id = message.at("id").get<std::string>();
    if(!message.at("ok").get<bool>()){
        status = "Voice: " + message.at("error").value("message", "Request failed");
        if(id == connectRequest){ failed = true; connected = connecting = false; }
        return;
    }
    if(id == connectRequest && configuration.is_object()){
        connectionId = std::max(connectionId, message.at("data").at("connection_id").get<std::uint64_t>());
        minimumConnection = connectionId;
    }
    if(id == muteRequest && message.at("data").contains("mutes")){
        const auto& mutes = message.at("data").at("mutes");
        microphone = mutes.at("microphone").get<bool>();
        incomingAll = mutes.at("all").get<bool>();
        incomingTeam = mutes.at("team").get<bool>();
    }
}

void VoiceChat::SelectTransmit(GameClient& game, bool team){
    if(Connected() && (!team || playing)) game.SelectVoiceTransmit(team);
}
