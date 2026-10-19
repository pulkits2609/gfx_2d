#include"Game/game.hpp"
#include<array>
#include<cstdint>
#include<iostream>
#include<string>

namespace{
    //Keep the original solver here so an optimization cannot silently change pushes.
    void ResolveOriginalCollisions(std::array<Player, Game::PlayerCount>& players){
        Arena arena;
        float diameter = 2.0f * MatchRules::PlayerRadius;
        for(int pass = 0; pass < 24; pass++){
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

    Game::State MakeState(std::initializer_list<glm::vec2> positions){
        Game::State state;
        std::size_t index = 0;
        for(const glm::vec2& position : positions){
            Player& player = state.players[index];
            player.active = true;
            player.id = index + 1;
            player.position = position;
            index++;
        }
        return state;
    }

    bool CompareWithOriginal(const std::string& name, const Game::State& initial){
        Game game(2, false);
        game.ApplyState(initial);
        auto expected = initial.players;
        Arena arena;
        const std::array<PlayerInput, Game::PlayerCount> noInput{};

        //Several ticks also check that resting contacts remain stable afterwards.
        for(int tick = 0; tick < 8; tick++){
            for(Player& player : expected){
                if(player.IsAlive()){
                    player.position = arena.ClampPosition(player.position, MatchRules::PlayerRadius);
                }
            }
            ResolveOriginalCollisions(expected);
            game.Update(MatchRules::ServerStep, noInput);
            const auto& actual = game.GetPlayers();
            for(std::size_t i = 0; i < actual.size(); i++){
                //Exact comparison catches even small changes to calculation order.
                if(actual[i].position.x != expected[i].position.x
                    || actual[i].position.y != expected[i].position.y){
                    std::cerr << name << ": position differs at tick " << tick
                        << ", player " << i << '\n';
                    return false;
                }
            }
        }
        return true;
    }
}

int main(){
    const float edgeX = MatchRules::ArenaHalfWidth - MatchRules::PlayerRadius;
    const float edgeY = MatchRules::ArenaHalfHeight - MatchRules::PlayerRadius;
    const float diameter = 2.0f * MatchRules::PlayerRadius;
    bool passed = true;
    auto check = [&](const std::string& name, const Game::State& state){
        if(!CompareWithOriginal(name, state)) passed = false;
    };

    check("empty arena", MakeState({}));
    check("separated players", MakeState({{-1.0f, 0.5f}, {1.0f, -0.5f}}));
    check("touching players", MakeState({{0.0f, 0.0f}, {diameter, 0.0f}}));
    check("overlapping players", MakeState({{0.0f, 0.0f}, {0.05f, 0.02f}}));
    check("coincident players", MakeState({{0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}}));
    check("chained pushes", MakeState({{-0.25f, 0.0f}, {-0.1f, 0.0f}, {0.05f, 0.0f}, {0.2f, 0.0f}}));
    check("wall contact", MakeState({{edgeX, 0.0f}, {edgeX - 0.04f, 0.0f}, {edgeX - 0.1f, 0.03f}}));
    check("corner contact", MakeState({{edgeX, edgeY}, {edgeX, edgeY}, {edgeX - 0.03f, edgeY - 0.03f}}));
    check("outside boundary", MakeState({{-2.0f, -2.0f}, {-edgeX, -edgeY}}));

    auto ignoredPlayers = MakeState({{0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}});
    ignoredPlayers.players[1].health = 0;
    ignoredPlayers.players[1].respawnTime = 1000.0;
    ignoredPlayers.players[2].active = false;
    check("dead and inactive players", ignoredPlayers);

    //A fixed seed makes these crowded nine-player cases reproducible on every run.
    std::uint32_t seed = 42;
    auto nextCoordinate = [&](){
        seed = seed * 1664525u + 1013904223u;
        return static_cast<float>((seed >> 16) % 1001) / 1000.0f - 0.5f;
    };
    for(int sample = 0; sample < 128; sample++){
        Game::State state;
        for(std::size_t i = 0; i < state.players.size(); i++){
            Player& player = state.players[i];
            player.active = true;
            player.id = i + 1;
            player.position = glm::vec2(nextCoordinate(), nextCoordinate());
            if(sample % 3 == 0){
                player.position += glm::vec2(edgeX, edgeY);
            }
        }
        check("deterministic layout " + std::to_string(sample), state);
    }

    if(!passed) return 1;
    std::cout << "Collision results match the original 24-pass solver in 138 layouts.\n";
    return 0;
}
