#include"Network/gameServer.hpp"
#include<charconv>
#include<csignal>
#include<iostream>

int main(int argc, char* argv[]){
    try{
        std::string address = "127.0.0.1";
        unsigned short port = Protocol::ControlPort;
        unsigned short gameplayPort = GameplayPackets::Port;
        VoiceServerSettings voice;
        unsigned tickRate = MatchRules::DefaultTickRate;
        for(int i = 1; i < argc; i++){
            std::string option = argv[i];
            if(option == "--help"){
                std::cout << "gfx_server [--bind IP] [--port 0-65535] [--udp-port 0-65535] [--tick-rate 1-180]\n"
                    "           [--voice-host HOST] [--voice-port PORT] [--voice-public-host HOST] [--no-voice]\n";
                return 0;
            }
            if(option == "--no-voice"){ voice.enabled = false; continue; }
            if(i + 1 >= argc) throw std::invalid_argument("Missing server option value");
            std::string value = argv[++i];
            if(option == "--bind") address = value;
            else if(option == "--voice-host") voice.host = value;
            else if(option == "--voice-public-host") voice.publicHost = value;
            else if(option == "--tick-rate"){
                auto result = std::from_chars(value.data(), value.data() + value.size(), tickRate);
                if(result.ec != std::errc() || result.ptr != value.data() + value.size()
                    || tickRate == 0 || tickRate > MatchRules::MaximumTickRate)
                    throw std::invalid_argument("Tick rate must be 1-180");
            }
            else if(option == "--port" || option == "--udp-port" || option == "--voice-port"){
                unsigned number = 0;
                auto result = std::from_chars(value.data(), value.data() + value.size(), number);
                if(result.ec != std::errc() || result.ptr != value.data() + value.size() || number > 65535)
                    throw std::invalid_argument("Invalid control port");
                if(option == "--port") port = static_cast<unsigned short>(number);
                else if(option == "--voice-port"){
                    if(!number) throw std::invalid_argument("Voice port must be 1-65535");
                    voice.port = static_cast<unsigned short>(number);
                }
                else gameplayPort = static_cast<unsigned short>(number);
            }
            else throw std::invalid_argument("Unknown server option");
        }
        boost::asio::io_context context;
        GameServer server(context, address, port, gameplayPort, voice, tickRate);
        boost::asio::signal_set signals(context, SIGINT, SIGTERM);
        signals.async_wait([&](const boost::system::error_code&, int){ server.Stop(); });
        server.Start();
        std::cout << "GFX server listening on " << address << ':' << server.GetPort() << std::endl;
        std::cout << "Simulation tick rate: " << tickRate << " Hz" << std::endl;
        context.run();
    }
    catch(const std::exception& error){ std::cerr << error.what() << '\n'; return 1; }
}
