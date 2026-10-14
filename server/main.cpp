#include"Network/gameServer.hpp"
#include<charconv>
#include<csignal>
#include<iostream>

int main(int argc, char* argv[]){
    try{
        std::string address = "127.0.0.1";
        unsigned short port = Protocol::ControlPort;
        unsigned short gameplayPort = GameplayPackets::Port;
        for(int i = 1; i < argc; i++){
            std::string option = argv[i];
            if(option == "--help"){
                std::cout << "gfx_server [--bind IP] [--port 0-65535] [--udp-port 0-65535]\n";
                return 0;
            }
            if(i + 1 >= argc) throw std::invalid_argument("Missing server option value");
            std::string value = argv[++i];
            if(option == "--bind") address = value;
            else if(option == "--port" || option == "--udp-port"){
                unsigned number = 0;
                auto result = std::from_chars(value.data(), value.data() + value.size(), number);
                if(result.ec != std::errc() || result.ptr != value.data() + value.size() || number > 65535)
                    throw std::invalid_argument("Invalid control port");
                if(option == "--port") port = static_cast<unsigned short>(number);
                else gameplayPort = static_cast<unsigned short>(number);
            }
            else throw std::invalid_argument("Unknown server option");
        }
        boost::asio::io_context context;
        GameServer server(context, address, port, gameplayPort);
        boost::asio::signal_set signals(context, SIGINT, SIGTERM);
        signals.async_wait([&](const boost::system::error_code&, int){ server.Stop(); });
        server.Start();
        std::cout << "GFX server listening on " << address << ':' << server.GetPort() << std::endl;
        context.run();
    }
    catch(const std::exception& error){ std::cerr << error.what() << '\n'; return 1; }
}
