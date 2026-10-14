#include"Game/game.hpp"
#include"UI/viewLayout.hpp"
#include<cmath>
#include<iostream>
#include<stdexcept>

void Check(bool condition, const char* message){
    if(!condition) throw std::runtime_error(message);
}

void Step(Game& game, int ticks, const std::array<PlayerInput, Game::PlayerCount>& inputs = {}){
    for(int i = 0; i < ticks; i++) game.Update(MatchRules::SimulationStep, inputs);
}

int main(){
    try{
        for(glm::vec2 screen : {glm::vec2(960, 640), glm::vec2(1920, 1080), glm::vec2(3840, 2160)}){
            ViewLayout view = ViewLayout::Calculate(screen.x, screen.y);
            float arenaTop = view.top + (1 - MatchRules::ArenaHalfHeight / view.halfHeight) * view.height * 0.5f;
            float arenaBottom = view.top + (1 + MatchRules::ArenaHalfHeight / view.halfHeight) * view.height * 0.5f;
            Check(arenaTop > 110 * view.scale, "arena stays below scoreboard after resizing");
            Check(arenaBottom < screen.y - 122 * view.scale, "arena stays above bottom HUD");
            Check(glm::length(view.ToWorld(view.left + view.width * 0.5f,
                view.top + view.height * 0.5f)) < 0.00001f, "mouse uses the arena viewport");
        }
        Game game;
        Check(game.GetPlayers()[0].name == "Player 1", "default player name");
        Check(std::abs(MatchRules::PlayerRadius - 0.105f) < 0.00001f, "smaller shared hitbox");
        for(int teams = 2; teams <= 4; teams++){
            Game match(teams);
            Check(match.GetTeamCount() == teams, "team count");
            for(const Player& player : match.GetPlayers()) Check(player.team < teams, "valid team assignment");
        }

        //opposite, equal movement must not give either player a pushing advantage
        std::array<PlayerInput, Game::PlayerCount> inputs{};
        inputs[0].movement = {1, 0};
        inputs[1].movement = {-1, 0};
        Step(game, 400, inputs);
        const auto& players = game.GetPlayers();
        Check(glm::length(players[0].position - players[1].position) >= 2 * MatchRules::PlayerRadius - 0.0001f,
            "opposing players do not overlap");
        Check(std::abs(players[0].position.x + players[1].position.x) < 0.0001f, "equal forces cancel");

        Game push;
        inputs = {};
        inputs[0].movement = {1, 0};
        Step(push, 1000, inputs);
        Check(push.GetPlayers()[1].position.x > 0.8f, "a moving player pushes an idle player");
        Check(glm::length(push.GetPlayers()[0].position - push.GetPlayers()[1].position)
            >= 2 * MatchRules::PlayerRadius - 0.0001f, "wall contact does not leave overlap");
        Check(push.GetPlayers()[1].position.x <= MatchRules::ArenaHalfWidth - MatchRules::PlayerRadius + 0.0001f,
            "push stops at wall");

        Game boost;
        inputs = {};
        inputs[0].movement = {1, 0};
        inputs[0].boost = true;
        Step(boost, 360, inputs);
        Check(boost.GetPlayers()[0].stamina == 0, "boost runs out after three seconds");
        Check(boost.GetPlayers()[0].boostExhausted, "holding shift does not retrigger boost");
        Step(boost, 120, inputs);
        Check(!boost.GetPlayers()[0].boosting, "boost stays exhausted until release");
        inputs = {};
        Step(boost, 500, inputs);
        Check(std::abs(boost.GetPlayers()[0].stamina - MatchRules::MaximumStamina) < 0.0001f,
            "stamina recovers");

        Game weapon;
        Step(weapon, 121);
        inputs = {};
        inputs[0].hasAim = true;
        inputs[0].aimPosition = {-1.5f, 1};
        inputs[0].shoot = true;
        Step(weapon, 1000, inputs);
        Check(weapon.GetPlayers()[0].ammunition == 0, "magazine holds exactly thirty shots");
        inputs[0].shoot = false;
        inputs[0].reload = true;
        Step(weapon, 1, inputs);
        Check(weapon.GetPlayers()[0].reloading, "reload starts");
        inputs[0].reload = false;
        Step(weapon, 169, inputs);
        Check(!weapon.GetPlayers()[0].reloading && weapon.GetPlayers()[0].ammunition == 30, "reload completes");

        Game travelling;
        Step(travelling, 121);
        inputs = {};
        inputs[0].hasAim = true;
        inputs[0].aimPosition = travelling.GetPlayers()[1].position;
        inputs[0].shoot = true;
        Step(travelling, 1, inputs);
        Check(travelling.GetPlayers()[1].health == 100, "firing does not cause instant damage");
        inputs[0].shoot = false;
        Step(travelling, 15, inputs);
        Check(travelling.GetPlayers()[1].health == 100, "no damage while bullet is travelling");
        Step(travelling, 20, inputs);
        Check(travelling.GetPlayers()[1].health == 75, "damage occurs on bullet arrival");

        Game dodge;
        Step(dodge, 121);
        inputs = {};
        inputs[0].hasAim = true;
        inputs[0].aimPosition = dodge.GetPlayers()[1].position;
        inputs[0].shoot = true;
        Step(dodge, 1, inputs);
        inputs[0].shoot = false;
        inputs[1].movement = {0, -1};
        Step(dodge, 60, inputs);
        Check(dodge.GetPlayers()[1].health == 100, "a target can dodge a travelling bullet");

        //At 30 Hz a bullet travels 0.2 units per tick. Sweeping its whole
        //segment must still hit a small circle between its two endpoints.
        Game serverCombat(2, false);
        serverCombat.AddPlayer(101, "One", 0);
        serverCombat.AddPlayer(202, "Two", 1);
        auto serverState = serverCombat.GetState();
        serverState.players[0].position = {-0.25f, 0};
        serverState.players[1].position = {0.25f, 0};
        for(auto& player : serverState.players){ player.protectionEndTime = 0; player.nextShotTime = 0; }
        serverCombat.ApplyState(serverState);
        inputs = {};
        inputs[0].hasAim = true;
        inputs[0].aimPosition = {1, 0};
        inputs[0].shoot = true;
        serverCombat.Update(MatchRules::ServerStep, inputs);
        Check(serverCombat.GetPlayers()[1].health == 100, "30 Hz shot has travel time");
        inputs = {};
        serverCombat.Update(MatchRules::ServerStep, inputs);
        Check(serverCombat.GetPlayers()[1].health == 100, "30 Hz bullet has not arrived yet");
        serverCombat.Update(MatchRules::ServerStep, inputs);
        Check(serverCombat.GetPlayers()[1].health == 75, "swept bullet cannot tunnel through a small hitbox");

        Game combat;
        Step(combat, 121);
        inputs = {};
        inputs[0].hasAim = true;
        inputs[0].aimPosition = combat.GetPlayers()[1].position;
        inputs[0].shoot = true;
        Step(combat, 125, inputs);
        Check(!combat.GetPlayers()[1].IsAlive(), "four hits kill");
        Check(combat.GetTeamScores()[0] == 1 && combat.GetPlayers()[0].kills == 1, "kill updates team score once");
        inputs = {};
        //occupy the dead player's spawn to exercise safe spawn selection
        inputs[0].movement = {1, 0};
        Step(combat, 300, inputs);
        inputs = {};
        Step(combat, 80, inputs);
        Check(combat.GetPlayers()[1].IsAlive(), "respawn completes");
        for(std::size_t i = 0; i < Game::PlayerCount; i++){
            for(std::size_t j = i + 1; j < Game::PlayerCount; j++){
                if(!combat.GetPlayers()[i].active || !combat.GetPlayers()[j].active) continue;
                Check(glm::length(combat.GetPlayers()[i].position - combat.GetPlayers()[j].position)
                    >= 2 * MatchRules::PlayerRadius - 0.0001f, "respawn does not overlap another player");
            }
        }
        std::cout << "Gameplay checks passed\n";
    }
    catch(const std::exception& error){ std::cerr << error.what() << '\n'; return 1; }
}
