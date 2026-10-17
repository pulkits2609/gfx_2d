#include"Network/gameClient.hpp"
#include"Network/voiceChat.hpp"
#include<chrono>
#include<iostream>
#include<thread>

namespace{
    void Check(bool result, const char* message){ if(!result) throw std::runtime_error(message); }
}

int main(int argc, char* argv[]){
    try{
        Check(argc == 4, "Expected control port, role, and voice client executable");
        const std::string role = argv[2];
        const bool host = role == "host" || role == "host_timed";
        const bool timed = role == "host_timed" || role == "guest_timed";
        GameClient game;
        VoiceChat voice(argv[3], false); //Real child/network/codec, no audio devices.
        game.Connect("127.0.0.1", static_cast<unsigned short>(std::stoi(argv[1])), host ? "Host" : "Guest");
        bool requested = false, joined = false, ready = false, selectedAll = false;
        bool left = false, rejoining = false, rejoined = false;
        bool sawResult = false, returned = false, checkedReturn = false;
        std::uint64_t originalConnection = 0;
        std::uint32_t originalSession = 0;
        auto previous = std::chrono::steady_clock::now(), started = previous;
        auto joinedAt = previous, leftAt = previous, rejoinedAt = previous;
        auto resultAt = previous;
        while(std::chrono::steady_clock::now() - started < std::chrono::seconds(timed ? 80 : 20)){
            const auto now = std::chrono::steady_clock::now();
            const double delta = std::chrono::duration<double>(now - previous).count();
            previous = now;
            game.Poll();
            voice.Update(&game);
            game.UpdateGameplay(delta, {});
            Check(game.GetState() != GameClient::State::Failed, game.GetStatus().c_str());
            if(game.GetState() == GameClient::State::Connected && !requested){
                if(host) game.CreateLobby(2, timed ? 1 : 3, true);
                else game.FetchLobbies();
                requested = true;
            }
            if(!host && !joined && game.HasLobbyList() && !game.GetPublicLobbies().empty()){
                game.JoinPublicLobby(game.GetPublicLobbies().front().at("lobbyId").get<std::uint64_t>());
                joined = true;
            }
            if(!host && !joined && game.HasLobbyList() && game.GetPublicLobbies().empty()){
                game.FetchLobbies();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            if(voice.Connected() && !ready){
                Check(!voice.TeamEnabled(), "Team sending enabled before match start");
                Check(!voice.TransmittingTeam(), "Waiting lobby did not select All");
                Check(voice.MicrophoneMuted() && voice.AllMuted() && voice.TeamMuted(), "Fresh voice did not start fully muted");
                Check(voice.State().at("receive_channels").size() == 2, "Missing All + own-Team receive routes");
                originalConnection = voice.State().at("connection_id").get<std::uint64_t>();
                originalSession = voice.State().at("session_id").get<std::uint32_t>();
                voice.SetMicrophone(false); voice.SetIncomingAll(false); voice.SetIncomingTeam(false);
                game.SetReady(true);
                ready = true;
            }
            const auto& lobby = game.GetLobby();
            if(host && ready && lobby.is_object() && !lobby.at("playing").get<bool>() && lobby.at("members").size() == 2){
                bool allReady = true;
                for(const auto& member : lobby["members"]) allReady = allReady && member.at("ready").get<bool>();
                if(allReady) game.StartMatch();
            }
            if(voice.Connected() && voice.TeamEnabled() && voice.TransmittingTeam() && !selectedAll){
                voice.SelectTransmit(game, false);
                selectedAll = true;
                joinedAt = now;
            }
            if(timed && game.GetResults().is_object()){
                if(!sawResult) resultAt = now;
                sawResult = true;
                Check(voice.Connected(), "Match completion removed voice");
                Check(voice.State().at("session_id") == originalSession, "Match completion replaced voice membership");
                if(host && !returned && now - resultAt > std::chrono::milliseconds(500)){
                    game.ReturnToLobby(); returned = true;
                }
            }
            if(timed && sawResult && !checkedReturn && voice.Connected() && !voice.TeamEnabled()){
                if(voice.TransmittingTeam()) continue; //Wait for the asynchronous forced-All event.
                Check(voice.State().at("session_id") == originalSession, "Lobby return replaced voice membership");
                Check(!voice.MicrophoneMuted() && !voice.AllMuted() && !voice.TeamMuted(), "Lobby return reset mute preferences");
                checkedReturn = true;
                rejoinedAt = now;
            }
            if(timed && checkedReturn && (!host || now - rejoinedAt > std::chrono::milliseconds(800))){
                if(host) game.LeaveLobby();
                for(int i = 0; i < 30; i++){
                    game.Poll(); voice.Update(&game);
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                std::cout << "Voice client lifecycle passed through results and lobby return" << std::endl;
                return 0;
            }
            if(!timed && !host && selectedAll && !left && now - joinedAt > std::chrono::milliseconds(700)){
                Check(!voice.TransmittingTeam(), "Server-approved All switch was not applied");
                Check(!voice.MicrophoneMuted() && !voice.AllMuted() && !voice.TeamMuted(), "Phase or switch reset mute choices");
                game.LeaveLobby();
                left = true;
                leftAt = now;
            }
            if(!host && left && !rejoining && game.GetLobby().is_null() && now - leftAt > std::chrono::milliseconds(200)){
                Check(!voice.Connected(), "Departed guest stayed connected to voice");
                Check(game.RejoinSecondsLeft() > 0, "Expected an immediate rejoin window");
                game.RejoinMatch();
                rejoining = true;
            }
            if(!host && rejoining && !rejoined && voice.Connected()){
                Check(voice.State().at("connection_id").get<std::uint64_t>() > originalConnection, "Rejoin reused local connection identity");
                Check(voice.State().at("session_id").get<std::uint32_t>() != originalSession, "Rejoin reused remote voice session");
                Check(voice.TransmittingTeam(), "Rejoining a playing match did not select Team");
                Check(!voice.MicrophoneMuted() && !voice.AllMuted() && !voice.TeamMuted(), "Guest rejoin forgot mute preferences");
                rejoined = true;
                rejoinedAt = now;
                std::cout << "Guest rejoined voice" << std::endl;
            }
            if(!host && rejoined && now - rejoinedAt > std::chrono::milliseconds(500)){
                game.LeaveLobby();
                //Drive the leave acknowledgement and child disconnect before exit.
                for(int i = 0; i < 50; i++){
                    game.Poll(); voice.Update(&game);
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                std::cout << "Voice client lifecycle passed" << std::endl;
                return 0;
            }
            if(!timed && host && selectedAll && now - joinedAt > std::chrono::seconds(3)){
                Check(voice.Connected(), "Guest leave/rejoin removed the host's voice");
                Check(!voice.TransmittingTeam(), "Guest leave/rejoin changed the host's transmit selection");
                game.LeaveLobby();
                for(int i = 0; i < 50; i++){
                    game.Poll(); voice.Update(&game);
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                Check(!voice.Connected(), "Host departure did not disconnect local voice");
                std::cout << "Voice client lifecycle passed" << std::endl;
                return 0;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        throw std::runtime_error("Voice client checkpoint timed out: " + voice.Status());
    }
    catch(const std::exception& error){ std::cerr << error.what() << '\n'; return 1; }
}
