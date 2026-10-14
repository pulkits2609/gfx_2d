#pragma once

#include<array>
#include<cstdint>
#include<string>
#include<vector>
#include<nlohmann/json.hpp>

//TCP is a byte stream: every JSON body starts with its four-byte length.
namespace Protocol{
    using Json = nlohmann::json;
    using Header = std::array<std::uint8_t, 4>;
    constexpr int Version = 1;
    constexpr std::uint32_t MaximumBodySize = 16 * 1024;
    constexpr unsigned short ControlPort = 7777;
    constexpr std::size_t MaximumSessions = 64;

    Header EncodeLength(std::uint32_t length);
    std::uint32_t DecodeLength(const Header& header);
    std::vector<std::uint8_t> Encode(const Json& message);
    bool Parse(const std::string& body, Json& message, std::string& error);
    bool ValidName(const std::string& name);
    Json Message(const std::string& type);
}
