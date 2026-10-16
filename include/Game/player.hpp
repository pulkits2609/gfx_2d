#pragma once

#include<glm/glm.hpp>
#include<string>
#include<cstdint>
#include<cstddef>
#include"Game/matchRules.hpp"

//Player stores the actual gameplay state
//PlayerInput describes movement, aiming, and firing intentions

//plain gameplay data, no graphics resources
struct Player{
    bool active = false;
    std::uint64_t id = 0;
    std::string name;
    glm::vec2 position{0.0f, 0.0f};
    glm::vec2 spawnPosition{0.0f, 0.0f};
    glm::vec2 aimDirection{0.0f, 1.0f};

    int team = 0;
    int health = MatchRules::StartingHealth;
    int kills = 0;
    int deaths = 0;
    int ammunition = MatchRules::MagazineSize;
    std::uint32_t lastShotId = 0;
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
        return active && health > 0;
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
    std::uint32_t shotId = 0; //zero keeps offline hold-to-fire behaviour
};

//a real travelling bullet; damage is decided when its path reaches a hitbox
struct Projectile{
    std::uint32_t id = 0;
    std::uint32_t shotId = 0; //matches the shooter's local visual with the server shot
    std::size_t owner = 0;
    int team = 0;
    glm::vec2 position{0.0f, 0.0f};
    glm::vec2 direction{0.0f, 1.0f};
    float remainingDistance = MatchRules::ShotRange;
    double firedAt = 0.0;
};
