#pragma once
#include "window.hpp"

//this will be the main application, that owns the window, renderer, game etc

class Application{
    private:
        Window window; //this should internally call its constructor

    public:
        Application();
        void RunApplication();
};
