#include"Network/gameClient.hpp"
#include<charconv>
#include<chrono>
#include<iostream>
#include<thread>

int main(int argc, char* argv[]){
    try{
        if(argc > 4) throw std::invalid_argument("Usage: gfx_control_client [host] [port] [name]");
        std::string host = argc > 1 ? argv[1] : "127.0.0.1";
        unsigned short port = Protocol::ControlPort;
        if(argc > 2){
            std::string value = argv[2];
            unsigned number = 0;
            auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
            if(parsed.ec != std::errc() || parsed.ptr != value.data() + value.size() || number == 0 || number > 65535)
                throw std::invalid_argument("Invalid port");
            port = static_cast<unsigned short>(number);
        }
        GameClient client;
        client.Connect(host, port, argc > 3 ? argv[3] : "Control Tester");
        auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(8);
        while(!client.ProbeComplete() && std::chrono::steady_clock::now() < limit){
            client.Poll();
            if(client.GetState() == GameClient::State::Failed) throw std::runtime_error(client.GetStatus());
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if(!client.ProbeComplete()) throw std::runtime_error("Control probe timed out");
        std::cout << "Session " << client.GetPlayerId() << ": hello, ping and player list passed ("
            << client.GetPeerCount() << " connected players)\n";
    }
    catch(const std::exception& error){ std::cerr << error.what() << '\n'; return 1; }
}
