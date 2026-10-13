#include"Game/game.hpp"
#include<algorithm>
#include<cmath>

// Each player starts on a different team and at a different spawn position.
Game::Game(){
    const std::array<glm::vec2, PlayerCount> spawnPositions{
        glm::vec2(-0.8f, 0.5f),
        glm::vec2(0.8f, 0.5f),
        glm::vec2(-0.8f, -0.5f),
        glm::vec2(0.8f, -0.5f)
    };

    for(std::size_t i = 0; i < players.size(); i++){
        players[i].team = static_cast<int>(i);
        players[i].spawnPosition = spawnPositions[i];
        Respawn(players[i]);
    }
}

// Respawning restores health and position while retaining kills and deaths. Initial spawning and later respawning both provide one second of protection.
// Protected players cannot shoot or receive damage during that period.
void Game::Respawn(Player& player){
    player.position = arena.ClampPosition(
        player.spawnPosition,
        MatchRules::PlayerRadius
    );

    player.health = MatchRules::StartingHealth;
    player.aimDirection = glm::vec2(0.0f, 1.0f);
    player.respawnTime = 0.0;
    player.protectionEndTime = elapsedTime + MatchRules::SpawnProtection;
    player.nextShotTime = player.protectionEndTime;
}

// This updates respawns first, then applies input to the selected living player.
// Aim is recalculated after movement, so it points from the player’s updated position toward the mouse.
void Game::Update(
    double deltaTime,
    std::size_t controlledPlayer,
    const PlayerInput& input
){
    if(deltaTime <= 0.0){
        return;
    }

    elapsedTime += deltaTime;

    //all players' respawn timers advance, even if they are not selected
    for(Player& player : players){
        if(!player.IsAlive()
            && elapsedTime + MatchRules::TimerTolerance >= player.respawnTime){
            Respawn(player);
        }
    }

    if(controlledPlayer >= players.size()){
        return;
    }

    Player& player = players[controlledPlayer];

    if(!player.IsAlive()){
        return;
    }

    glm::vec2 movement = input.movement;

    if(glm::dot(movement, movement) > 0.0f){
        movement = glm::normalize(movement);

        player.position += movement
            * MatchRules::MovementSpeed
            * static_cast<float>(deltaTime);
    }

    player.position = arena.ClampPosition(
        player.position,
        MatchRules::PlayerRadius
    );

    //calculate aim after movement, using the current player position
    if(input.hasAim){
        glm::vec2 direction = input.aimPosition - player.position;

        if(glm::dot(direction, direction) > 0.0f){
            player.aimDirection = glm::normalize(direction);
        }
    }

    if(input.shoot){
        Fire(controlledPlayer);
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


// The shot begins with the distance to the arena wall. Any closer living opponent becomes the current target.
// Only the nearest target receives damage. Dead players and teammates are excluded. A protected opponent stops the ray but receives no damage.

void Game::Fire(std::size_t shooterIndex){
    Player& shooter = players[shooterIndex];

    if(!shooter.IsAlive() || shooter.IsSpawnProtected(elapsedTime)){
        return;
    }

    if(elapsedTime + MatchRules::TimerTolerance < shooter.nextShotTime){
        return;
    }

    //a valid shot consumes the cooldown, even if it misses
    shooter.nextShotTime = elapsedTime + MatchRules::FireCooldown;

    float nearestDistance = arena.GetRayExitDistance(
        shooter.position,
        shooter.aimDirection,
        MatchRules::ShotRange
    );

    std::size_t hitIndex = players.size();

    for(std::size_t i = 0; i < players.size(); i++){
        const Player& target = players[i];

        if(i == shooterIndex
            || !target.IsAlive()
            || target.team == shooter.team){
            continue;
        }

        float hitDistance = 0.0f;

        if(RayHitsPlayer(
            shooter.position,
            shooter.aimDirection,
            target.position,
            hitDistance
        ) && hitDistance < nearestDistance){
            nearestDistance = hitDistance;
            hitIndex = i;
        }
    }

    shots[shooterIndex].start = shooter.position;
    shots[shooterIndex].end = shooter.position
        + shooter.aimDirection * nearestDistance;
    shots[shooterIndex].expiresAt = elapsedTime
        + MatchRules::ShotTraceDuration;

    if(hitIndex == players.size()){
        return;
    }

    Player& target = players[hitIndex];

    //protected players still stop the ray, but receive no damage
    if(target.IsSpawnProtected(elapsedTime)){
        return;
    }

    target.health = std::max(0, target.health - MatchRules::ShotDamage);

    if(!target.IsAlive()){
        target.deaths++;
        shooter.kills++;
        target.respawnTime = elapsedTime + MatchRules::RespawnDelay;
    }
}