#include"Game/game.hpp"
#include<algorithm>
#include<cmath>
#include<limits>

// Each player starts on a different team and at a different spawn position.
Game::Game(int numberOfTeams, bool localPractice):teamCount(std::clamp(numberOfTeams, 2, 4)){
    if(!localPractice) return;
    const std::array<glm::vec2, 4> spawnPositions{
        glm::vec2(-0.8f, 0.5f),
        glm::vec2(0.8f, 0.5f),
        glm::vec2(-0.8f, -0.5f),
        glm::vec2(0.8f, -0.5f)
    };

    for(std::size_t i = 0; i < 4; i++){
        players[i].active = true;
        players[i].id = i + 1;
        players[i].name = "Player " + std::to_string(i + 1);
        players[i].team = static_cast<int>(i) % teamCount;
        players[i].health = 0;
        players[i].spawnPosition = spawnPositions[i];
    }
    for(std::size_t i = 0; i < 4; i++){
        Respawn(players[i]);
    }
}

// Respawning restores health and position while retaining kills and deaths. Initial spawning and later respawning both provide one second of protection.
// Protected players cannot shoot or receive damage during that period.
void Game::Respawn(Player& player){
    player.position = FindSpawnPosition(player);

    player.health = MatchRules::StartingHealth;
    player.aimDirection = glm::vec2(0.0f, 1.0f);
    player.respawnTime = 0.0;
    player.protectionEndTime = elapsedTime + MatchRules::SpawnProtection;
    player.nextShotTime = player.protectionEndTime;
    player.ammunition = MatchRules::MagazineSize;
    player.reloading = false;
    player.reloadEndTime = 0.0;
    player.stamina = MatchRules::MaximumStamina;
    player.boostExhausted = false;
    player.boosting = false;
    player.staminaRecoveryTime = elapsedTime;
}

glm::vec2 Game::FindSpawnPosition(const Player& player) const{
    auto isFree = [&](const glm::vec2& position){
        for(const Player& other : players){
            if(&other == &player || !other.IsAlive()) continue;
            glm::vec2 difference = position - other.position;
            float spacing = 2.0f * MatchRules::PlayerRadius + 0.005f;
            if(glm::dot(difference, difference) < spacing * spacing) return false;
        }
        return true;
    };

    glm::vec2 preferred = arena.ClampPosition(player.spawnPosition, MatchRules::PlayerRadius);
    if(isFree(preferred)) return preferred;

    //if somebody occupies the spawn, choose the nearest free point on a small grid
    glm::vec2 best = preferred;
    float bestDistance = std::numeric_limits<float>::max();
    glm::vec2 half = arena.GetHalfSize();
    float spacing = 2.0f * MatchRules::PlayerRadius + 0.01f;
    for(float y = -half.y + MatchRules::PlayerRadius; y <= half.y - MatchRules::PlayerRadius; y += spacing){
        for(float x = -half.x + MatchRules::PlayerRadius; x <= half.x - MatchRules::PlayerRadius; x += spacing){
            glm::vec2 candidate(x, y);
            glm::vec2 difference = candidate - preferred;
            float distance = glm::dot(difference, difference);
            if(distance < bestDistance && isFree(candidate)){
                best = candidate;
                bestDistance = distance;
            }
        }
    }
    return best;
}

// This updates respawns first, then applies input to the selected living player.
// Aim is recalculated after movement, so it points from the player’s updated position toward the mouse.
void Game::Update(
    double deltaTime,
    std::size_t controlledPlayer,
    const PlayerInput& input
){
    std::array<PlayerInput, PlayerCount> inputs{};
    if(controlledPlayer < inputs.size()) inputs[controlledPlayer] = input;
    Update(deltaTime, inputs);
}

void Game::Update(double deltaTime, const std::array<PlayerInput, PlayerCount>& inputs){
    if(!std::isfinite(deltaTime) || deltaTime <= 0.0) return;
    elapsedTime += deltaTime;

    //all players' respawn timers advance, even if they are not selected
    for(Player& player : players){
        if(player.active && !player.IsAlive()
            && elapsedTime + MatchRules::TimerTolerance >= player.respawnTime){
            Respawn(player);
        }
    }

    std::array<glm::vec2, PlayerCount> oldPositions{};
    for(std::size_t i = 0; i < players.size(); i++){
        oldPositions[i] = players[i].position;
        if(players[i].IsAlive()) UpdatePlayer(players[i], inputs[i], deltaTime);
    }

    ResolveCollisions();

    //aim and fire after every player has moved and contacts have been solved
    for(std::size_t i = 0; i < players.size(); i++){
        Player& player = players[i];
        if(!player.IsAlive()) continue;
        if(inputs[i].hasAim){
            glm::vec2 direction = inputs[i].aimPosition - player.position;
            if(std::isfinite(direction.x) && std::isfinite(direction.y)
                && glm::dot(direction, direction) > 0.0f){
                player.aimDirection = glm::normalize(direction);
            }
        }
        if(inputs[i].shoot) Fire(i, inputs[i].shotId);
    }
    UpdateProjectiles(deltaTime, oldPositions);
}

void Game::UpdatePlayer(Player& player, const PlayerInput& input, double deltaTime){
    if(player.reloading && elapsedTime + MatchRules::TimerTolerance >= player.reloadEndTime){
        player.ammunition = MatchRules::MagazineSize;
        player.reloading = false;
    }
    if(input.reload && !player.reloading && player.ammunition < MatchRules::MagazineSize){
        player.reloading = true;
        player.reloadEndTime = elapsedTime + MatchRules::ReloadDuration;
    }

    PredictMovement(player, input, deltaTime, elapsedTime);
}

//The same movement and stamina calculation is reused for client prediction.
//Prediction never decides hits, health or scores.
void Game::PredictMovement(Player& player, const PlayerInput& input, double deltaTime, double time){
    if(!player.IsAlive() || deltaTime <= 0) return;
    if(!input.boost) player.boostExhausted = false;
    glm::vec2 movement = input.movement;
    if(!std::isfinite(movement.x) || !std::isfinite(movement.y)) movement = glm::vec2(0.0f);
    bool moving = glm::dot(movement, movement) > 0.0f;
    player.boosting = moving && input.boost && !player.boostExhausted && player.stamina > 0.0f;
    float speed = MatchRules::MovementSpeed;
    if(player.boosting){
        float used = std::min(player.stamina, static_cast<float>(deltaTime));
        speed *= 1.0f + (MatchRules::BoostMultiplier - 1.0f)
            * used / static_cast<float>(deltaTime);
        player.stamina = std::max(0.0f, player.stamina - used);
        if(player.stamina < 0.00001f){
            player.stamina = 0.0f;
            player.boostExhausted = true;
        }
        player.staminaRecoveryTime = time + MatchRules::StaminaRecoveryDelay;
    }
    else if(time >= player.staminaRecoveryTime){
        player.stamina = std::min(MatchRules::MaximumStamina,
            player.stamina + MatchRules::StaminaRecovery * static_cast<float>(deltaTime));
    }

    if(moving){
        movement = glm::normalize(movement);
        player.position += movement * speed * static_cast<float>(deltaTime);
    }

    player.position = Arena().ClampPosition(
        player.position,
        MatchRules::PlayerRadius
    );

}

void Game::ResolveCollisions(){
    float diameter = 2.0f * MatchRules::PlayerRadius;
    //equal mass: share overlap equally. Multiple passes let pushes reach a chain of players.
    for(int pass = 0; pass < MatchRules::CollisionPasses; pass++){
        for(std::size_t i = 0; i < players.size(); i++){
            if(!players[i].IsAlive()) continue;
            for(std::size_t j = i + 1; j < players.size(); j++){
                if(!players[j].IsAlive()) continue;
                glm::vec2 difference = players[j].position - players[i].position;
                float distance = glm::length(difference);
                if(distance >= diameter) continue;
                glm::vec2 normal = distance > 0.000001f
                    ? difference / distance : glm::vec2(1.0f, 0.0f);
                glm::vec2 correction = normal * ((diameter - distance) * 0.5f);
                players[i].position = arena.ClampPosition(players[i].position - correction, MatchRules::PlayerRadius);
                players[j].position = arena.ClampPosition(players[j].position + correction, MatchRules::PlayerRadius);
            }
        }
    }
}

// this function finds where that ray intersects the targets circular hitbox
//Negative discriminant: the ray misses.
// Both intersections behind the origin: the target is behind the shot.
// Otherwise: return the nearest forward intersection.  
bool Game::RayHitsPlayer(
    const glm::vec2& origin,
    const glm::vec2& direction,
    const glm::vec2& centre,
    float& distance
){
    glm::vec2 offset = origin - centre;

    float b = glm::dot(offset, direction);
    float c = glm::dot(offset, offset)
        - MatchRules::PlayerRadius * MatchRules::PlayerRadius;

    float discriminant = b * b - c;

    if(discriminant < 0.0f){
        return false;
    }

    float root = std::sqrt(discriminant);
    float nearDistance = -b - root;
    float farDistance = -b + root;

    //both intersections behind the shooter means there is no forward hit
    if(farDistance < 0.0f){
        return false;
    }

    //a ray beginning inside a target hits immediately
    distance = std::max(0.0f, nearDistance);
    return true;
}


//Firing creates a bullet. Its later movement decides whether anybody is hit.
void Game::Fire(std::size_t shooterIndex, std::uint32_t shotId){
    Player& shooter = players[shooterIndex];
    if(shotId && static_cast<std::int32_t>(shotId - shooter.lastShotId) <= 0) return;
    if(!shooter.IsAlive() || shooter.IsSpawnProtected(elapsedTime)
        || shooter.reloading || shooter.ammunition <= 0
        || elapsedTime + MatchRules::TimerTolerance < shooter.nextShotTime
        || projectiles.size() >= MatchRules::MaximumProjectiles) return;

    shooter.nextShotTime = elapsedTime + MatchRules::FireCooldown;
    shooter.ammunition--;
    shooter.lastShotId = shotId;
    Projectile bullet;
    bullet.id = nextProjectileId++;
    bullet.shotId = shotId;
    bullet.owner = shooterIndex;
    bullet.team = shooter.team;
    bullet.position = shooter.position;
    bullet.direction = shooter.aimDirection;
    bullet.firedAt = elapsedTime;
    projectiles.push_back(bullet);
}

void Game::UpdateProjectiles(double deltaTime, const std::array<glm::vec2, PlayerCount>& oldPositions){
    for(auto bullet = projectiles.begin(); bullet != projectiles.end();){
        //A new bullet starts at the end of this tick, so it travels next tick.
        if(bullet->firedAt >= elapsedTime){ ++bullet; continue; }
        float travel = std::min(bullet->remainingDistance,
            MatchRules::ProjectileSpeed * static_cast<float>(deltaTime));
        float wallDistance = arena.GetRayExitDistance(bullet->position, bullet->direction, bullet->remainingDistance);
        float firstFraction = travel > 0 ? std::min(1.0f, wallDistance / travel) : 0;
        std::size_t hit = players.size();
        for(std::size_t i = 0; i < players.size(); i++){
            const Player& target = players[i];
            if(i == bullet->owner || !target.IsAlive() || target.team == bullet->team) continue;
            //Subtract the target's movement: this checks both moving paths,
            //rather than testing only where the target ended the tick.
            glm::vec2 relativeStep = bullet->direction * travel - (target.position - oldPositions[i]);
            float relativeLength = glm::length(relativeStep);
            float distance = 0;
            if(relativeLength > 0.000001f && RayHitsPlayer(bullet->position,
                relativeStep / relativeLength, oldPositions[i], distance)){
                float fraction = distance / relativeLength;
                if(fraction <= firstFraction){ firstFraction = fraction; hit = i; }
            }
        }
        bullet->position += bullet->direction * (travel * firstFraction);
        bullet->remainingDistance -= travel * firstFraction;
        if(hit != players.size()){
            Player& target = players[hit];
            if(!target.IsSpawnProtected(elapsedTime)){
                target.health = std::max(0, target.health - MatchRules::ShotDamage);
                if(!target.IsAlive()){
                    target.deaths++;
                    players[bullet->owner].kills++;
                    teamScores[static_cast<std::size_t>(bullet->team)]++;
                    target.reloading = false;
                    target.boosting = false;
                    target.respawnTime = elapsedTime + MatchRules::RespawnDelay;
                }
            }
        }
        if(hit != players.size() || wallDistance <= travel + 0.000001f
            || bullet->remainingDistance <= 0.000001f) bullet = projectiles.erase(bullet);
        else ++bullet;
    }
}

bool Game::AddPlayer(std::uint64_t id, const std::string& name, int team){
    if(id == 0 || team < 0 || team >= teamCount) return false;
    for(const auto& player : players) if(player.active && player.id == id) return false;
    for(Player& player : players){
        if(player.active) continue;
        player = Player{};
        player.active = true;
        player.id = id;
        player.name = name;
        player.team = team;
        player.health = 0;
        static const glm::vec2 corners[]{{-1.1f, 0.65f}, {1.1f, -0.65f}, {-1.1f, -0.65f}, {1.1f, 0.65f}};
        player.spawnPosition = corners[team];
        Respawn(player);
        return true;
    }
    return false;
}

void Game::RemovePlayer(std::uint64_t id){
    for(std::size_t i = 0; i < players.size(); i++){
        if(!players[i].active || players[i].id != id) continue;
        players[i].active = false;
        projectiles.erase(std::remove_if(projectiles.begin(), projectiles.end(),
            [i](const Projectile& bullet){ return bullet.owner == i; }), projectiles.end());
    }
}

Game::State Game::GetState() const{ return {elapsedTime, teamCount, teamScores, players, projectiles}; }

int Game::GetWinningTeam() const{
    int best = -1, winner = -1;
    bool tied = false;
    for(int team = 0; team < teamCount; team++){
        if(teamScores[team] > best){ best = teamScores[team]; winner = team; tied = false; }
        else if(teamScores[team] == best) tied = true;
    }
    return tied ? -1 : winner;
}

void Game::ApplyState(const State& state){
    elapsedTime = state.time;
    teamCount = state.teams;
    teamScores = state.scores;
    players = state.players;
    projectiles = state.projectiles;
}
