#pragma once

#include<filesystem>

//Presentation and local input preferences never change server gameplay rules.
struct ClientSettings{
    static constexpr float MinimumUiScale = 0.85f;
    static constexpr float MaximumUiScale = 1.20f;
    float uiScale = 1.0f;
    int boostBinding = 0; //Left trigger; see ControllerInput::BindingName.
    int fireBinding = 1; //Right trigger.
    int controllerId = -1; //Automatically choose a supported controller.

    static std::filesystem::path FilePath();
    void Load(const std::filesystem::path& path = FilePath());
    bool Save(const std::filesystem::path& path = FilePath()) const;
};
