#include"Network/shotPreview.hpp"
#include<iostream>
#include<stdexcept>

void Check(bool condition, const char* message){ if(!condition) throw std::runtime_error(message); }

int main(){
    try{
        Game game(2);
        auto state = game.GetState(); state.time = 2;
        state.players[0].protectionEndTime = 0;
        PlayerInput input; input.shoot = input.hasAim = true; input.aimPosition = {1, 0};
        ShotPreview preview;
        preview.Update(state.players[0], input, 0, 2, 10);
        auto display = state;
        preview.Draw(display, 0, 10);
        Check(display.projectiles.size() == 1 && preview.GetRequest() == 1, "shot appears before a server round trip");
        Check(display.players[0].ammunition == 29 && state.players[0].ammunition == 30, "only displayed ammo is predicted");
        Check(state.players[1].health == 100, "preview never decides damage");
        preview.Update(state.players[0], input, 0, 2, 10.1);
        Check(preview.GetRequest() == 1, "visual cooldown is enforced");
        GameplayPackets::Snapshot snapshot; snapshot.state = state;
        snapshot.state.players[0].lastShotId = 1;
        Projectile confirmed = display.projectiles[0]; confirmed.id = 99;
        snapshot.state.projectiles.push_back(confirmed);
        preview.Reconcile(snapshot, 0, 10.1);
        display = snapshot.state;
        preview.Draw(display, 0, 10.1);
        Check(display.projectiles.size() == 1, "server echo does not draw a second bullet");
        snapshot.state.projectiles.clear();
        preview.Reconcile(snapshot, 0, 10.15);
        display = snapshot.state; preview.Draw(display, 0, 10.15);
        Check(display.projectiles.empty(), "server impact removes the preview");
        preview.Reset();
        preview.Update(state.players[0], input, 0, 2, 20);
        snapshot.state.players[0].lastShotId = 0;
        preview.Reconcile(snapshot, 0, 20.8);
        display = snapshot.state; preview.Draw(display, 0, 20.8);
        Check(display.projectiles.empty(), "rejected shot is cleaned up");
        preview.Reset();
        state.players[0].protectionEndTime = state.time + MatchRules::SpawnProtection;
        preview.Update(state.players[0], input, 0, state.time, 30);
        display = state; preview.Draw(display, 0, 30);
        Check(preview.GetRequest() == 1 && display.projectiles.size() == 1,
            "protected players receive immediate shot feedback");
        Check(display.players[0].IsSpawnProtected(display.time)
            && state.players[0].protectionEndTime == state.time + MatchRules::SpawnProtection,
            "shot preview keeps spawn protection active");
        std::cout << "Immediate shot preview checks passed\n";
    }
    catch(const std::exception& error){ std::cerr << error.what() << '\n'; return 1; }
}
