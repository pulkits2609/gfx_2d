#include"Core/clientSettings.hpp"
#include<nlohmann/json.hpp>
#include<algorithm>
#include<cmath>
#include<cstdlib>
#include<fstream>

std::filesystem::path ClientSettings::FilePath(){
#ifdef _WIN32
    if(const wchar_t* folder = _wgetenv(L"LOCALAPPDATA"))
        return std::filesystem::path(folder) / "GFX_2D" / "settings.json";
#else
    if(const char* folder = std::getenv("XDG_CONFIG_HOME"))
        return std::filesystem::path(folder) / "gfx_2d" / "settings.json";
    if(const char* folder = std::getenv("HOME"))
        return std::filesystem::path(folder) / ".config" / "gfx_2d" / "settings.json";
#endif
    return "client-settings.json";
}

void ClientSettings::Load(const std::filesystem::path& path){
    //Missing or damaged preferences should never prevent opening the game.
    *this = ClientSettings{};
    std::ifstream input(path);
    if(!input) return;
    try{
        auto data = nlohmann::json::parse(input);
        if(!data.is_object()) return;
        if(data.contains("uiScale") && data["uiScale"].is_number()){
            float scale = data["uiScale"].get<float>();
            if(std::isfinite(scale)) uiScale = std::clamp(scale, MinimumUiScale, MaximumUiScale);
        }
        auto binding = [&](const char* name, int fallback){
            if(!data.contains(name) || !data[name].is_number_integer() || data[name] < 0 || data[name] >= 10)
                return fallback;
            return data[name].get<int>();
        };
        boostBinding = binding("boostBinding", 0);
        fireBinding = binding("fireBinding", 1);
        if(fireBinding == boostBinding) fireBinding = boostBinding == 1 ? 0 : 1;
        if(data.contains("controllerId") && data["controllerId"].is_number_integer()
            && data["controllerId"] >= -1 && data["controllerId"] <= 15)
            controllerId = data["controllerId"].get<int>();
    }
    catch(const std::exception&){ *this = ClientSettings{}; }
}

bool ClientSettings::Save(const std::filesystem::path& path) const{
    try{
        if(!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path);
        if(!output) return false;
        nlohmann::json data{{"uiScale", uiScale}, {"boostBinding", boostBinding},
            {"fireBinding", fireBinding}, {"controllerId", controllerId}};
        output << data.dump(2) << '\n';
        output.flush();
        return output.good();
    }
    catch(const std::exception&){ return false; }
}
