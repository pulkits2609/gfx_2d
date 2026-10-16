#include"Network/gameplayPackets.hpp"
#include<iostream>
#include<stdexcept>

void Check(bool condition, const char* message){ if(!condition) throw std::runtime_error(message); }

int main(){
    try{
        using namespace GameplayPackets;
        Identity identity{0x12345678abcdef01ULL, std::string(64, 'a')};
        Packet packet;
        auto bytes = Registration(Type::Register, identity);
        Check(Decode(bytes.data(), bytes.size(), packet) && packet.identity.id == identity.id, "64-bit session identity");
        bytes.push_back(0);
        Check(!Decode(bytes.data(), bytes.size(), packet), "trailing bytes rejected");
        InputCommand command;
        command.sequence = 12; command.reload = 3; command.input.movement = {-1, 1};
        command.input.aimPosition = {1.2f, -0.4f}; command.input.boost = true;
        bytes = Inputs(identity, 7, {command});
        Check(Decode(bytes.data(), bytes.size(), packet) && packet.match == 7
            && packet.commands[0].reload == 3 && packet.commands[0].input.boost, "input round trip");
        bytes.resize(bytes.size() - 1);
        Check(!Decode(bytes.data(), bytes.size(), packet), "truncated input rejected");
        Game game(3, false);
        for(int i = 0; i < 9; i++) Check(game.AddPlayer(i + 1, "Player", i % 3), "nine online players");
        Snapshot snapshot;
        snapshot.state = game.GetState(); snapshot.match = 2; snapshot.tick = 40; snapshot.remaining = 60;
        for(std::size_t i = 0; i < MatchRules::MaximumProjectiles; i++){
            Projectile bullet; bullet.id = static_cast<std::uint32_t>(i + 1); snapshot.state.projectiles.push_back(bullet);
        }
        bytes = State(identity, snapshot);
        Check(bytes.size() <= MaximumSize, "largest snapshot fits one 1200-byte datagram");
        Check(Decode(bytes.data(), bytes.size(), packet) && packet.snapshot.state.players[8].id == 9
            && packet.snapshot.state.projectiles.size() == MatchRules::MaximumProjectiles, "full snapshot round trip");
        snapshot.state.time = 5;
        snapshot.state.projectiles.clear();
        for(auto& player : snapshot.state.players) player.protectionEndTime = 0;
        auto expired = State(identity, snapshot);
        Check(Decode(expired.data(), expired.size(), packet) && packet.snapshot.state.players[0].protectionEndTime == 0,
            "expired protection stays expired when rendering behind the newest snapshot");
        for(std::size_t size = 0; size < bytes.size(); size++)
            Check(!Decode(bytes.data(), size, packet), "every snapshot truncation is rejected");
        bytes[2] = 99;
        Check(!Decode(bytes.data(), bytes.size(), packet), "wrong UDP version rejected");
        Check(Newer(1, 0xffffffffu) && !Newer(5, 5) && !Newer(4, 5), "sequence wrap and duplicate handling");
        std::cout << "Gameplay packet checks passed\n";
    }
    catch(const std::exception& error){ std::cerr << error.what() << '\n'; return 1; }
}
