#include <iostream>
#include <stdexcept>
#include <string>
#include <charconv>
#include "Core/application.hpp"

int main(int argc, char* argv[]){

    try{
        int teams = 4;
        std::string host;
        std::string name = "Player 1";
        unsigned short port = Protocol::ControlPort;
        for(int i = 1; i < argc; i++){
            std::string option = argv[i];
            if(option == "--help"){
                std::cout << "gfx [--teams 2|3|4] [--server HOST] [--port PORT] [--name NAME]\n";
                return 0;
            }
            if(i + 1 >= argc) throw std::invalid_argument("Missing option value");
            std::string value = argv[++i];
            if(option == "--teams"){
                if(value != "2" && value != "3" && value != "4") throw std::invalid_argument("Teams must be 2-4");
                teams = value[0] - '0';
            }
            else if(option == "--server") host = value;
            else if(option == "--name") name = value;
            else if(option == "--port"){
                unsigned number = 0;
                auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
                if(parsed.ec != std::errc() || parsed.ptr != value.data() + value.size() || number == 0 || number > 65535)
                    throw std::invalid_argument("Invalid control port");
                port = static_cast<unsigned short>(number);
            }
            else throw std::invalid_argument("Unknown option: " + option);
        }
        Application app(teams, host, port, name);
        app.RunApplication();
    }
    catch(const std::exception& e){
        std::cerr<<e.what()<<"\n";
        return 1;
    }
    return 0;
}
