#pragma once

#include"Game/game.hpp"
#include<array>
#include<string>
#include<vector>

//UDP uses a small explicit binary format, not the memory layout of C++ structs.
//Each packet carries the TCP session ID and 32-byte token. Maximum: 1200 bytes.
namespace GameplayPackets{
    constexpr std::size_t MaximumSize = 1200;
    constexpr unsigned short Port = 7778;
    enum class Type : std::uint8_t{ Register = 1, Registered, Input, Snapshot };

    struct Identity{ std::uint64_t id = 0; std::string token; };
    struct InputCommand{
        std::uint32_t sequence = 0;
        std::uint32_t reload = 0;
        PlayerInput input;
    };
    struct Snapshot{
        std::uint32_t match = 0;
        std::uint32_t tick = 0;
        std::uint32_t acknowledged = 0;
        float remaining = 0;
        bool finished = false;
        Game::State state;
    };
    struct Packet{
        Type type = Type::Register;
        Identity identity;
        std::uint32_t match = 0;
        std::vector<InputCommand> commands;
        Snapshot snapshot;
    };

    inline bool Newer(std::uint32_t value, std::uint32_t previous){
        return static_cast<std::int32_t>(value - previous) > 0;
    }
    std::vector<std::uint8_t> Registration(Type type, const Identity& identity);
    std::vector<std::uint8_t> Inputs(const Identity& identity, std::uint32_t match,
        const std::vector<InputCommand>& commands);
    std::vector<std::uint8_t> State(const Identity& identity, const Snapshot& snapshot);
    bool Decode(const std::uint8_t* bytes, std::size_t size, Packet& packet);
}
