#include"Game/game.hpp"
#include"Network/rejoinInfo.hpp"
#include<iostream>
#include<stdexcept>

void Check(bool condition, const char* message){ if(!condition) throw std::runtime_error(message); }
void Tick(Game& game, int ticks, const std::array<PlayerInput, Game::PlayerCount>& input = {}){
    for(int i = 0; i < ticks; i++) game.Update(MatchRules::ServerStep, input);
}
Game ArenaGame(){
    Game game(3, false);
    game.AddPlayer(1, "Shooter", 0); game.AddPlayer(2, "Opponent", 1); game.AddPlayer(3, "Teammate", 0);
    auto state = game.GetState(); state.time = 2;
    state.players[0].position = {-0.6f, 0}; state.players[1].position = {0.6f, 0}; state.players[2].position = {0, 0};
    for(auto& player : state.players){ player.protectionEndTime = player.nextShotTime = 0; player.spawnPosition = player.position; }
    game.ApplyState(state);
    return game;
}

int main(){
    try{
        auto game = ArenaGame();
        std::array<PlayerInput, Game::PlayerCount> input{};
        input[0].hasAim = input[0].shoot = true; input[0].aimPosition = {1, 0}; input[0].shotId = 1;
        Tick(game, 1, input);
        Check(game.GetPlayers()[1].health == 100, "damage waits for bullet travel");
        Tick(game, 7, input);
        Check(game.GetPlayers()[0].ammunition == 29, "duplicate shot request fires only once");
        Check(game.GetPlayers()[1].health == 75 && game.GetPlayers()[2].health == 100, "enemy hit and friendly fire disabled");
        auto cooldown = ArenaGame(); Tick(cooldown, 1, input);
        input[0].shotId = 2; Tick(cooldown, 1, input);
        Check(cooldown.GetPlayers()[0].ammunition == 29, "new request cannot bypass cooldown");
        auto protectedShooter = ArenaGame(); auto state = protectedShooter.GetState();
        state.players[0].protectionEndTime = 10; protectedShooter.ApplyState(state);
        Tick(protectedShooter, 1, input);
        Check(protectedShooter.GetProjectiles().size() == 1 && protectedShooter.GetPlayers()[0].ammunition == 29,
            "protected players can fire");
        Check(protectedShooter.GetPlayers()[0].protectionEndTime == 10,
            "firing does not cancel protection");
        Game initialSpawn(2, false); initialSpawn.AddPlayer(1, "New player", 0);
        Check(initialSpawn.GetPlayers()[0].protectionEndTime == 3.0, "initial spawn provides three seconds of protection");
        Tick(initialSpawn, 1, input);
        Check(initialSpawn.GetPlayers()[0].ammunition == 29, "freshly spawned player can fire immediately");
        Tick(initialSpawn, 88);
        Check(initialSpawn.GetPlayers()[0].IsSpawnProtected(initialSpawn.GetTime()), "protection remains before three seconds");
        Tick(initialSpawn, 1);
        Check(!initialSpawn.GetPlayers()[0].IsSpawnProtected(initialSpawn.GetTime()), "protection expires after three seconds");
        auto protectedTarget = ArenaGame(); state = protectedTarget.GetState();
        state.players[1].protectionEndTime = state.time + MatchRules::SpawnProtection;
        protectedTarget.ApplyState(state); Tick(protectedTarget, 1, input); Tick(protectedTarget, 8);
        Check(protectedTarget.GetPlayers()[1].health == 100, "spawn protection blocks damage");
        Check(protectedTarget.GetProjectiles().empty(), "protected enemy still stops the bullet");
        Tick(protectedTarget, 81);
        input[0].shotId++; Tick(protectedTarget, 1, input); Tick(protectedTarget, 8);
        Check(protectedTarget.GetPlayers()[1].health == 75, "damage resumes when protection expires");
        auto kill = ArenaGame();
        for(std::uint32_t shot = 1; shot <= 4; shot++){
            input[0].shotId = shot; Tick(kill, 1, input); Tick(kill, 8);
        }
        Check(!kill.GetPlayers()[1].IsAlive() && kill.GetPlayers()[1].deaths == 1
            && kill.GetPlayers()[0].kills == 1 && kill.GetTeamScores()[0] == 1, "death and score counted once");
        for(int tick = 0; tick < 90 && !kill.GetPlayers()[1].IsAlive(); tick++) Tick(kill, 1);
        Check(kill.GetPlayers()[1].IsAlive() && kill.GetPlayers()[1].IsSpawnProtected(kill.GetTime()), "respawn restores spawn protection");
        double respawnProtectionLeft = kill.GetPlayers()[1].protectionEndTime - kill.GetTime();
        Check(respawnProtectionLeft >= 3.0 - MatchRules::TimerTolerance && respawnProtectionLeft <= 3.0 + MatchRules::TimerTolerance,
            "respawn also provides three seconds of protection");
        auto winner = ArenaGame(); state = winner.GetState(); state.scores = {4, 2, 1, 99}; winner.ApplyState(state);
        Check(winner.GetWinningTeam() == 0, "inactive team score is ignored");
        state.scores = {4, 4, 1, 0}; winner.ApplyState(state);
        Check(winner.GetWinningTeam() == -1, "tie at the top is a draw");
        state.scores = {}; winner.ApplyState(state);
        Check(winner.GetWinningTeam() == -1, "scoreless match is a draw");
        auto resume = ArenaGame(); state = resume.GetState();
        state.players[1].kills = 3; state.players[1].deaths = 2; resume.ApplyState(state);
        resume.RemovePlayer(2);
        Check(!resume.GetPlayers()[1].IsAlive(), "disconnected player cannot be hit");
        Check(resume.ResumePlayer(2, 99) && !resume.GetPlayers()[1].IsAlive(), "rejoin waits for respawn");
        Check(resume.GetPlayers()[1].team == 1 && resume.GetPlayers()[1].kills == 3
            && resume.GetPlayers()[1].deaths == 2, "rejoin keeps team and stats");
        Tick(resume, 91);
        Check(resume.GetPlayers()[1].IsAlive() && resume.GetPlayers()[1].IsSpawnProtected(resume.GetTime()), "rejoin respawn is protected");
        Check(LobbyRules::CanRejoin(59.99, 120, false), "exactly two minutes remains eligible");
        Check(!LobbyRules::CanRejoin(60, 180, false), "sixty-second window expires");
        Check(!LobbyRules::CanRejoin(10, 119.99, false), "cutoff is rechecked during the window");
        Check(!LobbyRules::CanRejoin(1, 180, true), "finished match cannot be rejoined");
        RejoinInfo info; info.token = "private"; info.offeredAt = 10; info.remaining = 125;
        Check(info.SecondsLeft(12) == 3 && info.SecondsLeft(16) == 0, "home timer also respects two-minute cutoff");
        std::cout << "Combat authority and result checks passed\n";
    }
    catch(const std::exception& error){ std::cerr << error.what() << '\n'; return 1; }
}
