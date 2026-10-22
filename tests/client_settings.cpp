#include"Core/clientSettings.hpp"
#include<chrono>
#include<fstream>
#include<iostream>
#include<stdexcept>

void Check(bool condition, const char* message){
    if(!condition) throw std::runtime_error(message);
}

int main(){
    auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() / ("gfx-settings-" + std::to_string(unique)) / "settings.json";
    try{
        ClientSettings settings;
        settings.Load(path);
        Check(settings.uiScale == 1 && settings.boostBinding == 0 && settings.fireBinding == 1, "Missing settings changed defaults");
        settings.uiScale = 1.15f; settings.boostBinding = 2; settings.fireBinding = 7; settings.controllerId = 3;
        Check(settings.Save(path), "Could not save preferences");
        ClientSettings restored; restored.Load(path);
        Check(restored.uiScale == settings.uiScale && restored.boostBinding == 2
            && restored.fireBinding == 7 && restored.controllerId == 3, "Preferences did not survive reload");
        std::ofstream(path) << R"({"uiScale":99,"boostBinding":1,"fireBinding":1,"controllerId":999})";
        restored.Load(path);
        Check(restored.uiScale == ClientSettings::MaximumUiScale && restored.fireBinding != restored.boostBinding
            && restored.controllerId == -1, "Invalid settings were not bounded");
        std::ofstream(path) << R"({"uiScale":0,"boostBinding":"oops","fireBinding":-8})";
        restored.Load(path);
        Check(restored.uiScale == ClientSettings::MinimumUiScale && restored.boostBinding == 0 && restored.fireBinding == 1,
            "Bad types changed bindings");
        std::ofstream(path) << "{broken";
        restored.Load(path);
        Check(restored.uiScale == 1 && restored.boostBinding == 0, "Damaged preferences did not restore defaults");
        std::filesystem::remove(path);
        std::filesystem::remove(path.parent_path());
        std::cout << "Client settings: persistence, defaults, bounds, duplicate bindings and damaged files passed\n";
    }
    catch(const std::exception& error){
        std::filesystem::remove(path);
        std::filesystem::remove(path.parent_path());
        std::cerr << error.what() << '\n'; return 1;
    }
}
