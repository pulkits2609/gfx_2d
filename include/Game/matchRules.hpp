#pragma once
#include<cstddef>

//the arena is 3 units wide and 2 units high
//all shapes share the same hitbox, including when they are rotated

namespace MatchRules{
    constexpr float MovementSpeed = 0.6f;

    constexpr int StartingHealth = 100;
    constexpr int ShotDamage = 25;
    constexpr double FireCooldown = 0.25;
    constexpr double RespawnDelay = 3.0;
    constexpr double SpawnProtection = 1.0;

    //the same circular hitbox is used for every shape
    constexpr float PlayerRadius = 0.14f * 0.75f;
    constexpr float PlayerVisualScale = 0.6f * 0.75f;

    constexpr int MagazineSize = 30;
    constexpr double ReloadDuration = 1.4;
    constexpr float MaximumStamina = 3.0f;
    constexpr float BoostMultiplier = 1.75f;
    constexpr float StaminaRecovery = 1.0f;
    constexpr double StaminaRecoveryDelay = 0.75;
    constexpr int CollisionPasses = 24;

    constexpr float ArenaHalfWidth = 1.5f;
    constexpr float ArenaHalfHeight = 1.0f;
    constexpr float ShotRange = 4.0f;
    constexpr float ProjectileSpeed = 6.0f;
    constexpr float TracerLength = 0.12f;
    constexpr std::size_t MaximumProjectiles = 28;

    //local simulation runs independently of rendering
    constexpr double SimulationStep = 1.0 / 120.0;
    constexpr unsigned DefaultTickRate = 30;
    constexpr unsigned MaximumTickRate = 180;
    constexpr double ServerStep = 1.0 / DefaultTickRate;
    constexpr double SnapshotStep = 1.0 / 20.0;
    constexpr double InputTimeout = 0.25;
    constexpr double TimerTolerance = 1e-9;
}
