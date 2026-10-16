#pragma once

#include"Network/gameplayPackets.hpp"

//Only predicts what the shooter sees. It never changes anybody's health.
class ShotPreview{
    private:
        struct Visual{
            Projectile bullet;
            double started = 0;
            bool hidden = false;
        };
        std::vector<Visual> visuals;
        std::uint32_t request = 0, accepted = 0;
        double nextFire = 0, reloadBlock = 0;

    public:
        void Reset();
        void Update(const Player& player, const PlayerInput& input, std::size_t slot, double gameTime, double now);
        void Reconcile(const GameplayPackets::Snapshot& snapshot, std::size_t slot, double now);
        void Draw(Game::State& display, std::size_t slot, double now) const;
        int Ammunition(int serverAmmo) const;
        std::uint32_t GetRequest() const{ return request; }
};
