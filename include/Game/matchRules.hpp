#pragma once

//the arena is 3 units wide and 2 units high
//All shapes share a radius of 0.14. At visual scale 0.6, their current geometry fits inside that radius even when rotated. This keeps their movement limits and hitboxes identical.

namespace MatchRules{
    constexpr float MovementSpeed = 0.6f;

    constexpr int StartingHealth = 100;
    constexpr int ShotDamage = 25;
    constexpr double FireCooldown = 0.25;
    constexpr double RespawnDelay = 3.0;
    constexpr double SpawnProtection = 1.0;

    //the same circular hitbox is used for every shape
    constexpr float PlayerRadius = 0.14f;
    constexpr float PlayerVisualScale = 0.6f;

    constexpr float ArenaHalfWidth = 1.5f;
    constexpr float ArenaHalfHeight = 1.0f;
    constexpr float ShotRange = 4.0f;
    constexpr double ShotTraceDuration = 0.08;

    //local simulation runs independently of rendering
    constexpr double SimulationStep = 1.0 / 120.0;
    constexpr double TimerTolerance = 1e-9;
}