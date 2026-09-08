#include <iostream>
#include "Core/application.hpp"

int main(){

    try{
        Application app;
        app.RunApplication();
    }
    catch(const std::exception& e){
        std::cerr<<e.what()<<"\n";
    }
    return 0;
}