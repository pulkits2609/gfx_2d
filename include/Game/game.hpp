#pragma once

#include<array>
#include<cstddef>
#include"Game/arena.hpp"
#include"Game/player.hpp"

//Game owns the arena, players, combat decisions, and gameplay clock. The application reads its state through const references.

class Game{
    public:
        //four players for this local checkpoint, one per team
        static constexpr std::size_t PlayerCount = 4;

    private:
        Arena arena;
        std::array<Player, PlayerCount> players;
        std::array<ShotTrace, PlayerCount> shots;
        double elapsedTime = 0.0;

        void Respawn(Player& player);
        void Fire(std::size_t shooterIndex);

        static bool RayHitsPlayer(
            const glm::vec2& origin,
            const glm::vec2& direction,
            const glm::vec2& centre,
            float& distance
        );

    public:
        Game();

        void Update(
            double deltaTime,
            std::size_t controlledPlayer,
            const PlayerInput& input
        );

        const Arena& GetArena() const{
            return arena;
        }

        const std::array<Player, PlayerCount>& GetPlayers() const{
            return players;
        }

        const std::array<ShotTrace, PlayerCount>& GetShots() const{
            return shots;
        }

        double GetTime() const{
            return elapsedTime;
        }
};