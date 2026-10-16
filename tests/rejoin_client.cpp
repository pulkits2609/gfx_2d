#include"Network/gameClient.hpp"
#include<chrono>
#include<iostream>
#include<memory>
#include<thread>

int main(int argc, char* argv[]){
    try{
        if(argc != 2) throw std::runtime_error("Expected server port");
        auto port = static_cast<unsigned short>(std::stoi(argv[1]));
        GameClient host;
        auto guest = std::make_unique<GameClient>();
        host.Connect("127.0.0.1", port, "Host");
        guest->Connect("127.0.0.1", port, "Guest");
        auto previous = std::chrono::steady_clock::now();
        auto wait = [&](auto ready){
            auto end = std::chrono::steady_clock::now() + std::chrono::seconds(7);
            while(!ready() && std::chrono::steady_clock::now() < end){
                auto now = std::chrono::steady_clock::now();
                double dt = std::chrono::duration<double>(now - previous).count(); previous = now;
                host.Poll(); guest->Poll();
                host.UpdateGameplay(dt, {}); guest->UpdateGameplay(dt, {});
                if(host.GetState() == GameClient::State::Failed) throw std::runtime_error(host.GetStatus());
                if(guest->GetState() == GameClient::State::Failed) throw std::runtime_error(guest->GetStatus());
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            if(!ready()) throw std::runtime_error("C++ rejoin client timed out");
        };
        wait([&]{ return host.UDPReady() && guest->UDPReady(); });
        host.CreateLobby(2, 3);
        wait([&]{ return host.GetLobby().is_object(); });
        guest->JoinLobby(host.GetLobby()["code"].get<std::string>());
        wait([&]{ return host.GetLobby()["members"].size() == 2 && guest->GetLobby().is_object(); });
        host.StartMatch();
        wait([&]{ return host.HasSnapshot() && guest->HasSnapshot(); });
        //Exercise the actual client's home-screen rejoin offer after a graceful leave.
        guest->LeaveLobby();
        wait([&]{ return !guest->GetLobby().is_object(); });
        if(guest->RejoinSecondsLeft() <= 0) throw std::runtime_error("Home screen lost the rejoin offer");
        guest->RejoinMatch();
        wait([&]{ return guest->HasSnapshot(); });
        guest->UpdateGameplay(0, {});
        if(guest->GetGame().GetPlayers()[guest->GetOwnSlot()].IsAlive()) throw std::runtime_error("Rejoin skipped respawn");
        //Then replace the TCP/UDP client entirely, as happens after a connection failure.
        guest->Disconnect();
        RejoinInfo ticket = guest->GetRejoinInfo();
        if(ticket.token.empty()) throw std::runtime_error("Disconnect lost the private ticket");
        guest = std::make_unique<GameClient>();
        guest->SetRejoinInfo(ticket);
        guest->Connect(ticket.host, ticket.port, ticket.name);
        wait([&]{ return guest->UDPReady(); });
        guest->RejoinMatch();
        wait([&]{ return guest->HasSnapshot(); });
        guest->UpdateGameplay(0, {});
        if(guest->GetGame().GetPlayers()[guest->GetOwnSlot()].IsAlive() || guest->RejoinSecondsLeft() != 0)
            throw std::runtime_error("Reconnected state or consumed ticket is wrong");
        std::cout << "C++ client rejoin passed: home offer, fresh TCP/UDP session and respawn\n";
    }
    catch(const std::exception& error){ std::cerr << error.what() << '\n'; return 1; }
}
