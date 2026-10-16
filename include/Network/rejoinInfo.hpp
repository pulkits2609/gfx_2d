#pragma once

#include"Network/lobby.hpp"

//Kept in memory while this game application is open. Never print the token.
struct RejoinInfo{
    std::string token, code, host, name;
    unsigned short port = 0; //filled with the original server's port when an offer is saved
    double offeredAt = 0, remaining = 0;

    double SecondsLeft(double now) const{
        if(token.empty()) return 0;
        double away = std::max(0.0, now - offeredAt);
        if(!LobbyRules::CanRejoin(away, remaining - away, false)) return 0;
        return std::min(LobbyRules::RejoinWindow - away, remaining - away - LobbyRules::MinimumRejoinTime);
    }
};
