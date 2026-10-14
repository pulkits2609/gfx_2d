#pragma once

#include<array>
#include<cstddef>
#include<cstdint>
#include<vector>
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
        std::vector<Projectile> projectiles;
        std::uint32_t nextProjectileId = 1;
        double elapsedTime = 0.0;
        int teamCount;
        std::array<int, 4> teamScores{};

        void Respawn(Player& player);
        void Fire(std::size_t shooterIndex);
        void UpdateProjectiles(double deltaTime, const std::array<glm::vec2, PlayerCount>& oldPositions);
        void ResolveCollisions();
        glm::vec2 FindSpawnPosition(const Player& player) const;
        void UpdatePlayer(Player& player, const PlayerInput& input, double deltaTime);

        static bool RayHitsPlayer(
            const glm::vec2& origin,
            const glm::vec2& direction,
            const glm::vec2& centre,
            float& distance
        );

    public:
        explicit Game(int numberOfTeams = 4);

        //all intentions are applied together before collisions are resolved
        void Update(double deltaTime, const std::array<PlayerInput, PlayerCount>& inputs);

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

        const std::vector<Projectile>& GetProjectiles() const{
            return projectiles;
        }

        double GetTime() const{
            return elapsedTime;
        }

        int GetTeamCount() const{ return teamCount; }
        const std::array<int, 4>& GetTeamScores() const{ return teamScores; }
        void SetPlayerName(std::size_t index, const std::string& name){
            if(index < players.size() && !name.empty() && name.size() <= 24) players[index].name = name;
        }
};
