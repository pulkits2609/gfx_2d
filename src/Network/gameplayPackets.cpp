#include"Network/gameplayPackets.hpp"
#include<algorithm>
#include<cmath>
#include<cstring>
#include<stdexcept>

namespace{
    //All integers and float bit patterns are written in network byte order.
    struct Writer{
        std::vector<std::uint8_t> bytes;
        void U8(std::uint8_t value){ bytes.push_back(value); }
        void U16(std::uint16_t value){ U8(value >> 8); U8(value & 255); }
        void U32(std::uint32_t value){ U16(value >> 16); U16(value & 65535); }
        void U64(std::uint64_t value){ U32(static_cast<std::uint32_t>(value >> 32)); U32(static_cast<std::uint32_t>(value)); }
        void Float(float value){ std::uint32_t bits; std::memcpy(&bits, &value, 4); U32(bits); }
        void Vector(glm::vec2 value){ Float(value.x); Float(value.y); }
        void Timer(double end, double now){ U16(static_cast<std::uint16_t>(std::clamp((end - now) * 1000.0, 0.0, 65535.0))); }
    };
    struct Reader{
        const std::uint8_t* bytes;
        std::size_t size, cursor = 0;
        bool valid = true;
        std::uint8_t U8(){ if(cursor >= size){ valid = false; return 0; } return bytes[cursor++]; }
        std::uint16_t U16(){ auto high = U8(); auto low = U8(); return (high << 8) | low; }
        std::uint32_t U32(){ auto high = U16(); auto low = U16(); return (static_cast<std::uint32_t>(high) << 16) | low; }
        std::uint64_t U64(){ auto high = U32(); auto low = U32(); return (static_cast<std::uint64_t>(high) << 32) | low; }
        float Float(){ auto bits = U32(); float value; std::memcpy(&value, &bits, 4); if(!std::isfinite(value)) valid = false; return value; }
        glm::vec2 Vector(){ float x = Float(); float y = Float(); return {x, y}; }
        double Timer(double now){ return now + U16() / 1000.0; }
    };
    Writer Header(GameplayPackets::Type type, const GameplayPackets::Identity& identity){
        if(identity.id == 0 || identity.token.size() != 64) throw std::invalid_argument("Invalid gameplay identity");
        Writer out;
        out.U8('G'); out.U8('2'); out.U8(2); out.U8(static_cast<std::uint8_t>(type));
        out.U64(identity.id);
        for(std::size_t i = 0; i < 64; i += 2) out.U8(static_cast<std::uint8_t>(std::stoul(identity.token.substr(i, 2), nullptr, 16)));
        return out;
    }
    bool Position(glm::vec2 point, float limit){ return std::abs(point.x) <= limit && std::abs(point.y) <= limit; }
}

std::vector<std::uint8_t> GameplayPackets::Registration(Type type, const Identity& identity){
    return Header(type, identity).bytes;
}

std::vector<std::uint8_t> GameplayPackets::Inputs(const Identity& identity, std::uint32_t match,
    const std::vector<InputCommand>& commands){
    if(commands.empty() || commands.size() > 4) throw std::length_error("Expected 1-4 recent inputs");
    auto out = Header(Type::Input, identity);
    out.U32(match);
    out.U8(static_cast<std::uint8_t>(commands.size()));
    for(const auto& command : commands){
        out.U32(command.sequence); out.U32(command.reload); out.U32(command.shot);
        out.U8(static_cast<std::uint8_t>(static_cast<int>(command.input.movement.x) + 1));
        out.U8(static_cast<std::uint8_t>(static_cast<int>(command.input.movement.y) + 1));
        out.Vector(command.input.aimPosition);
        out.U8((command.input.hasAim ? 1 : 0) | (command.input.shoot ? 2 : 0) | (command.input.boost ? 4 : 0));
    }
    return out.bytes;
}

std::vector<std::uint8_t> GameplayPackets::State(const Identity& identity, const Snapshot& snapshot){
    auto out = Header(Type::Snapshot, identity);
    out.U32(snapshot.match); out.U32(snapshot.tick); out.U32(snapshot.acknowledged);
    out.Float(static_cast<float>(snapshot.state.time)); out.Float(snapshot.remaining);
    out.U8(snapshot.finished ? 1 : 0); out.U8(static_cast<std::uint8_t>(snapshot.state.teams));
    for(int score : snapshot.state.scores) out.U16(static_cast<std::uint16_t>(score));
    std::size_t count = 0;
    for(const auto& player : snapshot.state.players) if(player.active) count++;
    out.U8(static_cast<std::uint8_t>(count));
    for(std::size_t slot = 0; slot < snapshot.state.players.size(); slot++){
        const auto& player = snapshot.state.players[slot];
        if(!player.active) continue;
        out.U8(static_cast<std::uint8_t>(slot)); out.U64(player.id);
        out.U8(static_cast<std::uint8_t>(player.team));
        out.Vector(player.position); out.Float(std::atan2(player.aimDirection.y, player.aimDirection.x));
        out.U8(static_cast<std::uint8_t>(player.health)); out.U8(static_cast<std::uint8_t>(player.ammunition));
        out.U32(player.lastShotId);
        out.Float(player.stamina);
        out.U8((player.reloading ? 1 : 0) | (player.boostExhausted ? 2 : 0) | (player.boosting ? 4 : 0));
        out.U16(static_cast<std::uint16_t>(player.kills)); out.U16(static_cast<std::uint16_t>(player.deaths));
        out.Timer(player.reloadEndTime, snapshot.state.time); out.Timer(player.respawnTime, snapshot.state.time);
        out.Timer(player.protectionEndTime, snapshot.state.time); out.Timer(player.staminaRecoveryTime, snapshot.state.time);
        out.Timer(player.nextShotTime, snapshot.state.time);
    }
    out.U8(static_cast<std::uint8_t>(snapshot.state.projectiles.size()));
    for(const auto& bullet : snapshot.state.projectiles){
        out.U32(bullet.id); out.U32(bullet.shotId);
        out.U8(static_cast<std::uint8_t>(bullet.owner)); out.U8(static_cast<std::uint8_t>(bullet.team));
        out.Vector(bullet.position);
        //Small measurements keep the largest snapshot below 1200 bytes.
        out.U16(static_cast<std::uint16_t>((std::atan2(bullet.direction.y, bullet.direction.x) + 3.142f) * 10000));
        out.U16(static_cast<std::uint16_t>(std::clamp(bullet.remainingDistance * 1000, 0.0f, 4000.0f)));
        out.U16(static_cast<std::uint16_t>(std::clamp((snapshot.state.time - bullet.firedAt) * 1000, 0.0, 1000.0)));
    }
    if(out.bytes.size() > MaximumSize) throw std::length_error("Gameplay snapshot exceeds datagram limit");
    return out.bytes;
}

bool GameplayPackets::Decode(const std::uint8_t* bytes, std::size_t size, Packet& packet){
    if(size < 44 || size > MaximumSize) return false;
    Reader in{bytes, size};
    if(in.U8() != 'G' || in.U8() != '2' || in.U8() != 2) return false;
    packet = Packet{};
    packet.type = static_cast<Type>(in.U8());
    packet.identity.id = in.U64();
    const char* hex = "0123456789abcdef";
    for(int i = 0; i < 32; i++){ auto byte = in.U8(); packet.identity.token += hex[byte >> 4]; packet.identity.token += hex[byte & 15]; }
    if(packet.identity.id == 0) return false;
    if(packet.type == Type::Input){
        packet.match = in.U32();
        auto count = in.U8();
        if(count == 0 || count > 4) return false;
        for(int i = 0; i < count; i++){
            InputCommand command;
            command.sequence = in.U32(); command.reload = in.U32(); command.shot = in.U32();
            auto x = in.U8(); auto y = in.U8();
            command.input.movement = {static_cast<int>(x) - 1, static_cast<int>(y) - 1};
            command.input.aimPosition = in.Vector();
            auto flags = in.U8();
            if(x > 2 || y > 2 || flags > 7 || !Position(command.input.aimPosition, 16)) return false;
            command.input.hasAim = (flags & 1) != 0; command.input.shoot = (flags & 2) != 0; command.input.boost = (flags & 4) != 0;
            packet.commands.push_back(command);
        }
    }
    else if(packet.type == Type::Snapshot){
        auto& snap = packet.snapshot;
        snap.match = in.U32(); snap.tick = in.U32(); snap.acknowledged = in.U32();
        snap.state.time = in.Float(); snap.remaining = in.Float();
        auto finished = in.U8(); snap.finished = finished == 1; snap.state.teams = in.U8();
        if(finished > 1 || snap.state.teams < 2 || snap.state.teams > 4 || snap.state.time < 0
            || snap.state.time > 1801 || snap.remaining < 0 || snap.remaining > 1800) return false;
        for(int& score : snap.state.scores) score = in.U16();
        auto count = in.U8();
        if(count > Game::PlayerCount) return false;
        for(int i = 0; i < count; i++){
            auto slot = in.U8();
            if(slot >= Game::PlayerCount || snap.state.players[slot].active) return false;
            auto& player = snap.state.players[slot];
            player.active = true; player.id = in.U64(); player.team = in.U8();
            player.position = in.Vector(); float angle = in.Float();
            player.aimDirection = {std::cos(angle), std::sin(angle)};
            player.health = in.U8(); player.ammunition = in.U8(); player.lastShotId = in.U32(); player.stamina = in.Float();
            auto flags = in.U8();
            player.reloading = (flags & 1) != 0; player.boostExhausted = (flags & 2) != 0; player.boosting = (flags & 4) != 0;
            player.kills = in.U16(); player.deaths = in.U16();
            player.reloadEndTime = in.Timer(snap.state.time); player.respawnTime = in.Timer(snap.state.time);
            player.protectionEndTime = in.Timer(snap.state.time); player.staminaRecoveryTime = in.Timer(snap.state.time);
            player.nextShotTime = in.Timer(snap.state.time);
            if(player.id == 0 || player.team >= snap.state.teams || flags > 7 || player.health > 100
                || player.ammunition > 30 || player.stamina < 0 || player.stamina > 3.001f
                || std::abs(player.position.x) > MatchRules::ArenaHalfWidth
                || std::abs(player.position.y) > MatchRules::ArenaHalfHeight || std::abs(angle) > 3.142f) return false;
            for(std::size_t j = 0; j < slot; j++)
                if(snap.state.players[j].active && snap.state.players[j].id == player.id) return false;
        }
        count = in.U8();
        if(count > MatchRules::MaximumProjectiles) return false;
        for(int i = 0; i < count; i++){
            Projectile bullet;
            bullet.id = in.U32(); bullet.shotId = in.U32();
            bullet.owner = in.U8(); bullet.team = in.U8(); bullet.position = in.Vector();
            float angle = in.U16() / 10000.0f - 3.142f; bullet.direction = {std::cos(angle), std::sin(angle)};
            bullet.remainingDistance = in.U16() / 1000.0f;
            float age = in.U16() / 1000.0f; bullet.firedAt = snap.state.time - age;
            if(bullet.owner >= Game::PlayerCount || bullet.team >= snap.state.teams || !Position(bullet.position, 2)
                || bullet.remainingDistance < 0 || bullet.remainingDistance > MatchRules::ShotRange
                || age < 0 || age > 1 || std::abs(angle) > 3.142f) return false;
            snap.state.projectiles.push_back(bullet);
        }
    }
    else if(packet.type != Type::Register && packet.type != Type::Registered) return false;
    return in.valid && in.cursor == size;
}
