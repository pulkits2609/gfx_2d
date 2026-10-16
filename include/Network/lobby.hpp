#pragma once

#include<string>
#include<algorithm>
#include<cctype>

namespace LobbyRules{
    constexpr int MinimumMinutes = 1;
    constexpr int MaximumMinutes = 30;
    constexpr int DefaultMinutes = 5;
    constexpr double RejoinWindow = 60;
    constexpr double MinimumRejoinTime = 120;

    inline bool CanRejoin(double secondsAway, double remaining, bool finished){
        return !finished && secondsAway >= 0 && secondsAway < RejoinWindow && remaining >= MinimumRejoinTime;
    }

    inline int PlayersPerTeam(int teams){ return teams == 2 ? 4 : teams == 3 ? 3 : 2; }

    inline std::string NormalizeCode(std::string code){
        for(char& c : code) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return code;
    }

    inline bool ValidCode(const std::string& code){
        const std::string alphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
        return code.size() == 6 && std::all_of(code.begin(), code.end(), [&](char c){
            return alphabet.find(c) != std::string::npos;
        });
    }
}
