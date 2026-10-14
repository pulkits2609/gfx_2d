#include <iostream>
#include <stdexcept>
#include <string>
#include "Core/application.hpp"

int main(int argc, char* argv[]){

    try{
        int teams = 4;
        if(argc != 1){
            if(argc != 3 || std::string(argv[1]) != "--teams"
                || (std::string(argv[2]) != "2" && std::string(argv[2]) != "3" && std::string(argv[2]) != "4")){
                throw std::invalid_argument("Usage: gfx [--teams 2|3|4]");
            }
            teams = argv[2][0] - '0';
        }
        Application app(teams);
        app.RunApplication();
    }
    catch(const std::exception& e){
        std::cerr<<e.what()<<"\n";
        return 1;
    }
    return 0;
}
