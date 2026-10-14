#pragma once

#include<glm/glm.hpp>
#include<string>
#include"Game/matchRules.hpp"

//Player stores the actual gameplay state
//PlayerInput describes movement, aiming, and firing intentions

//plain gameplay data, no graphics resources
struct Player{
    std::string name;
    glm::vec2 position{0.0f, 0.0f};
    glm::vec2 spawnPosition{0.0f, 0.0f};
    glm::vec2 aimDirection{0.0f, 1.0f};

    int team = 0;
    int health = MatchRules::StartingHealth;
    int kills = 0;
    int deaths = 0;
    int ammunition = MatchRules::MagazineSize;
    float stamina = MatchRules::MaximumStamina;
    bool reloading = false;
    bool boostExhausted = false;
    bool boosting = false;
    double reloadEndTime = 0.0;
    double staminaRecoveryTime = 0.0;

    double nextShotTime = 0.0;
    double respawnTime = 0.0;
    double protectionEndTime = 0.0;

    bool IsAlive() const{
        return health > 0;
    }

    bool IsSpawnProtected(double time) const{
        return IsAlive()
            && time + MatchRules::TimerTolerance < protectionEndTime;
    }
};

//the application supplies intentions; Game decides what happens
struct PlayerInput{
    glm::vec2 movement{0.0f, 0.0f};
    glm::vec2 aimPosition{0.0f, 0.0f};
    bool hasAim = false;
    bool shoot = false;
    bool reload = false;
    bool boost = false;
};

//visual feedback for an already calculated hitscan shot
struct ShotTrace{
    glm::vec2 start{0.0f, 0.0f};
    glm::vec2 end{0.0f, 0.0f};
    double firedAt = 0.0;
    double expiresAt = 0.0;
};
