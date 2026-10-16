#include"Network/shotPreview.hpp"
#include<algorithm>

void ShotPreview::Reset(){
    visuals.clear(); request = accepted = 0; nextFire = reloadBlock = 0;
}

int ShotPreview::Ammunition(int serverAmmo) const{
    int unconfirmed = 0;
    for(const auto& visual : visuals)
        if(!visual.hidden && GameplayPackets::Newer(visual.bullet.shotId, accepted)) unconfirmed++;
    return std::max(0, serverAmmo - unconfirmed);
}

void ShotPreview::Update(const Player& player, const PlayerInput& input, std::size_t slot, double gameTime, double now){
    if(input.reload && player.ammunition < MatchRules::MagazineSize) reloadBlock = now + 0.5;
    if(!input.shoot || !input.hasAim || !player.IsAlive() || player.IsSpawnProtected(gameTime)
        || player.reloading || now < reloadBlock || now < nextFire || Ammunition(player.ammunition) == 0) return;
    glm::vec2 direction = input.aimPosition - player.position;
    if(glm::length(direction) < 0.00001f) return;
    nextFire = now + MatchRules::FireCooldown;
    lastAim = input.aimPosition;
    Visual visual;
    visual.started = now;
    visual.bullet.shotId = ++request;
    visual.bullet.owner = slot;
    visual.bullet.team = player.team;
    visual.bullet.position = player.position;
    visual.bullet.direction = glm::normalize(direction);
    visuals.push_back(visual);
}

void ShotPreview::Reconcile(const GameplayPackets::Snapshot& snapshot, std::size_t slot, double now){
    accepted = snapshot.state.players[slot].lastShotId;
    for(auto& visual : visuals){
        bool exists = std::any_of(snapshot.state.projectiles.begin(), snapshot.state.projectiles.end(), [&](const Projectile& bullet){
            return bullet.owner == slot && bullet.shotId == visual.bullet.shotId;
        });
        if(!snapshot.state.players[slot].IsAlive() || (!exists && !GameplayPackets::Newer(visual.bullet.shotId, accepted)))
            visual.hidden = true;
        //Remove rejected or lost requests rather than leaving a permanent ghost.
        if(!exists && now - visual.started > 0.75) visual.hidden = true;
    }
    visuals.erase(std::remove_if(visuals.begin(), visuals.end(), [now](const Visual& visual){
        return now - visual.started > 1.5;
    }), visuals.end());
}

void ShotPreview::Draw(Game::State& display, std::size_t slot, double now) const{
    //Never draw both the predicted shot and its delayed server copy.
    auto& bullets = display.projectiles;
    bullets.erase(std::remove_if(bullets.begin(), bullets.end(), [slot](const Projectile& bullet){ return bullet.owner == slot; }), bullets.end());
    for(const auto& visual : visuals){
        if(visual.hidden) continue;
        Projectile bullet = visual.bullet;
        float age = static_cast<float>(std::max(0.0, now - visual.started));
        float distance = MatchRules::PlayerRadius + 0.015f + age * MatchRules::ProjectileSpeed;
        float wall = Arena().GetRayExitDistance(bullet.position, bullet.direction, MatchRules::ShotRange);
        if(distance >= wall) continue;
        bullet.position += bullet.direction * distance;
        bullet.firedAt = display.time - age - 0.005; //a short muzzle streak is visible on the first frame
        bullets.push_back(bullet);
    }
    display.players[slot].ammunition = Ammunition(display.players[slot].ammunition);
}
